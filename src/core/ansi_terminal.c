/* Oscar64 C128 VDC terminal. Its parser and keyboard behaviour are ported
 * from the proven cc65 terminal, but writes directly to the active VDC page. */
#include <string.h>
#include "ansi_terminal.h"
#include "vdc_screen.h"

#define W 80
#define H 25
#define CELLS 2000
#define ESC 27
#define ST_TEXT 0
#define ST_ESC 1
#define ST_CSI 2
#define ST_STRING 3
#define ST_STRING_ESC 4

static uint16_t chars, attrs;
static uint8_t x, y, saved_x, saved_y, state, params[8], count, private_mark;
static uint8_t attr, wrap, autowrap, cursor_visible, application_cursor;
static uint8_t newline_mode, scroll_top, scroll_bottom;
static ansi_send_callback send_bytes;

static uint16_t address(uint8_t col, uint8_t row)
{ return chars + (uint16_t)row * W + col; }
static uint16_t attr_address(uint8_t col, uint8_t row)
{ return attrs + (uint16_t)row * W + col; }
static uint8_t parameter(uint8_t n, uint8_t fallback)
{ return n >= count || !params[n] ? fallback : params[n]; }
static void cursor(void)
{ screen_set_cursor(address(x, y), cursor_visible); }
static uint8_t color(uint8_t n, uint8_t bright)
{
    static const uint8_t normal[8] = {0,8,4,12,2,10,6,14};
    static const uint8_t intense[8] = {1,9,5,13,3,11,7,15};
    return bright ? intense[n & 7] : normal[n & 7];
}
static void clear_line(uint8_t row, uint8_t first, uint8_t last)
{
    if (first > last || row >= H) return;
    screen_fill_run(address(first, row), 0x20, (uint16_t)(last - first + 1));
    screen_fill_run(attr_address(first, row), attr | 0x80, (uint16_t)(last - first + 1));
}
static void clear_screen(uint8_t first, uint8_t last)
{
    if (first > last || last >= H) return;
    screen_fill_run(address(0, first), 0x20, (uint16_t)(last - first + 1) * W);
    screen_fill_run(attr_address(0, first), attr | 0x80, (uint16_t)(last - first + 1) * W);
}
static void scroll_up(uint8_t top, uint8_t bottom, uint8_t lines)
{
    uint16_t length;
    if (!lines) lines = 1;
    if (lines > bottom - top + 1) lines = bottom - top + 1;
    length = (uint16_t)(bottom - top + 1 - lines) * W;
    if (length) {
        screen_copy_run(address(0, top), address(0, top + lines), length);
        screen_copy_run(attr_address(0, top), attr_address(0, top + lines), length);
    }
    clear_screen((uint8_t)(bottom - lines + 1), bottom);
}
static void line_feed(void)
{
    wrap = 0;
    if (newline_mode) x = 0;
    if (y == scroll_bottom) scroll_up(scroll_top, scroll_bottom, 1);
    else if (y < H - 1) ++y;
}
static void put(uint8_t value)
{
    uint8_t glyph, a;
    if (wrap) { x = 0; line_feed(); }
    glyph = value >= 128 ? value & 0x7f : value;
    a = attr | (value < 128 ? 0x80 : 0);
    screen_write_run(address(x, y), &glyph, 1);
    screen_write_run(attr_address(x, y), &a, 1);
    if (x == W - 1) wrap = autowrap;
    else ++x;
}
static void move(uint8_t amount, int8_t dx, int8_t dy)
{
    if (!amount) amount = 1;
    if (dx > 0) x = amount >= W - x ? W - 1 : x + amount;
    if (dx < 0) x = amount > x ? 0 : x - amount;
    if (dy > 0) y = amount >= H - y ? H - 1 : y + amount;
    if (dy < 0) y = amount > y ? 0 : y - amount;
}
static uint8_t decimal(uint8_t *out, uint8_t n, uint8_t v)
{
    if (v >= 10) out[n++] = '0' + v / 10;
    out[n++] = '0' + v % 10;
    return n;
}
static void report(uint8_t kind)
{
    uint8_t out[12], n = 0;
    if (!send_bytes) return;
    if (kind == 'c') { static const uint8_t da[] = {ESC,'[','?','1',';','0','c'}; send_bytes(da, sizeof(da)); return; }
    if (kind == 5) { static const uint8_t ok[] = {ESC,'[','0','n'}; send_bytes(ok, sizeof(ok)); return; }
    out[n++] = ESC; out[n++] = '['; n = decimal(out, n, y + 1); out[n++] = ';';
    n = decimal(out, n, x + 1); out[n++] = 'R'; send_bytes(out, n);
}
static void sgr(void)
{
    uint8_t i, fg = 7, bg = 0, bright = 1, reverse = 0, under = 0, blink = 0;
    /* Preserve current state only for successive SGRs; SGR 0 resets. */
    for (i = 0; i < count; ++i) {
        uint8_t p = params[i];
        if (!p) { fg = 7; bg = 0; bright = 1; reverse = under = blink = 0; }
        else if (p == 1) bright = 1; else if (p == 22) bright = 0;
        else if (p == 4) under = 1; else if (p == 24) under = 0;
        else if (p == 5) blink = 1; else if (p == 25) blink = 0;
        else if (p == 7) reverse = 1; else if (p == 27) reverse = 0;
        else if (p >= 30 && p <= 37) { fg = p - 30; bright = 0; }
        else if (p >= 90 && p <= 97) { fg = p - 90; bright = 1; }
        else if (p >= 40 && p <= 47) bg = p - 40;
        else if (p >= 100 && p <= 107) bg = p - 100;
    }
    attr = color(fg, bright);
    if (bg || reverse) attr = color(bg, 0) | 0x40;
    if (under) attr |= 0x20;
    if (blink) attr |= 0x10;
}
static void csi(uint8_t final)
{
    uint8_t n = parameter(0, 1), row, col;
    wrap = 0;
    switch (final) {
    case 'A': move(n, 0, -1); break; case 'B': move(n, 0, 1); break;
    case 'C': move(n, 1, 0); break; case 'D': move(n, -1, 0); break;
    case 'E': x = 0; move(n, 0, 1); break; case 'F': x = 0; move(n, 0, -1); break;
    case 'G': x = parameter(0, 1) - 1; if (x >= W) x = W - 1; break;
    case 'H': case 'f': row = parameter(0, 1); col = parameter(1, 1); y = row > H ? H - 1 : row - 1; x = col > W ? W - 1 : col - 1; break;
    case 'J': if (!params[0]) { clear_line(y, x, W - 1); clear_screen(y + 1, H - 1); } else if (params[0] == 1) { clear_screen(0, y - (y != 0)); clear_line(y, 0, x); } else clear_screen(0, H - 1); break;
    case 'K': if (!params[0]) clear_line(y, x, W - 1); else if (params[0] == 1) clear_line(y, 0, x); else clear_line(y, 0, W - 1); break;
    case 'S': scroll_up(scroll_top, scroll_bottom, n); break;
    case 'm': sgr(); break;
    case 'n': report(params[0]); break;
    case 'c': report('c'); break;
    case 's': saved_x = x; saved_y = y; break;
    case 'u': x = saved_x; y = saved_y; break;
    case 'r': scroll_top = parameter(0, 1) - 1; scroll_bottom = parameter(1, H) - 1; if (scroll_top >= scroll_bottom || scroll_bottom >= H) { scroll_top = 0; scroll_bottom = H - 1; } x = y = 0; break;
    case 'h': case 'l': if (private_mark == '?') { if (params[0] == 1) application_cursor = final == 'h'; if (params[0] == 7) autowrap = final == 'h'; if (params[0] == 25) cursor_visible = final == 'h'; } else if (params[0] == 20) newline_mode = final == 'h'; break;
    }
}
uint8_t ansi_terminal_init(ansi_send_callback send, uint8_t device)
{
    if (!screen_load_cp437(device)) return 0;
    chars = ((uint16_t)screen_reg_read(12) << 8) | screen_reg_read(13);
    attrs = ((uint16_t)screen_reg_read(20) << 8) | screen_reg_read(21);
    send_bytes = send; x = y = saved_x = saved_y = 0; state = ST_TEXT; attr = 0x0f;
    wrap = 0; autowrap = 1; cursor_visible = 1; application_cursor = newline_mode = 0;
    scroll_top = 0; scroll_bottom = H - 1; clear_screen(0, H - 1); cursor(); return 1;
}
void ansi_terminal_shutdown(void) { screen_set_cursor(address(x, y), 0); screen_restore_font(); }
void ansi_terminal_receive(uint8_t value)
{
    if (state == ST_ESC) {
        if (value == '[') { memset(params, 0, sizeof(params)); count = 1; private_mark = 0; state = ST_CSI; return; }
        if (value == ']' || value == 'P' || value == '^' || value == '_') { state = ST_STRING; return; }
        if (value == '7') { saved_x = x; saved_y = y; } else if (value == '8') { x = saved_x; y = saved_y; }
        else if (value == 'D') line_feed(); else if (value == 'E') { x = 0; line_feed(); }
        else if (value == 'M' && y) --y; else if (value == 'Z' || value == 'c') report('c');
        state = ST_TEXT; cursor(); return;
    }
    if (state == ST_CSI) {
        if (value >= '0' && value <= '9') { uint8_t *p = &params[count - 1]; *p = *p > 25 ? 255 : (uint8_t)(*p * 10 + value - '0'); }
        else if (value == ';' && count < sizeof(params)) ++count;
        else if ((value == '?' || value == '>' || value == '!') && count == 1 && !params[0]) private_mark = value;
        else if (value >= '@' && value <= '~') { csi(value); state = ST_TEXT; cursor(); }
        return;
    }
    if (state == ST_STRING) { if (value == 7) state = ST_TEXT; else if (value == ESC) state = ST_STRING_ESC; return; }
    if (state == ST_STRING_ESC) { state = value == '\\' ? ST_TEXT : ST_STRING; return; }
    if (value == ESC) state = ST_ESC;
    else if (value == 8) { wrap = 0; if (x) --x; }
    else if (value == 9) x = (uint8_t)((x + 8) & 0xf8), x = x >= W ? W - 1 : x;
    else if (value == 10 || value == 11 || value == 12) line_feed();
    else if (value == 13) { wrap = 0; x = 0; }
    else if (value == 5) report('c'); else if (value >= 32 && value != 127) put(value);
    if (state == ST_TEXT) cursor();
}
void ansi_terminal_receive_buffer(const uint8_t *data, uint8_t length)
{ while (length--) ansi_terminal_receive(*data++); }
uint8_t ansi_terminal_key(uint8_t key, uint8_t *out)
{
    uint8_t final;
    if (key == 0x91 || key == 0x11 || key == 0x1d || key == 0x9d) { final = key == 0x91 ? 'A' : key == 0x11 ? 'B' : key == 0x1d ? 'C' : 'D'; out[0] = ESC; out[1] = application_cursor ? 'O' : '['; out[2] = final; return 3; }
    if (key == 0x13) { out[0]=ESC; out[1]='['; out[2]='H'; return 3; }
    if (key >= 0x41 && key <= 0x5a) key += 32; else if (key >= 0xc1 && key <= 0xda) key -= 0x80; else if (key == 0x14) key = 8;
    if (key == 13) { out[0] = 13; if (newline_mode) { out[1] = 10; return 2; } return 1; }
    if (key < 128) { out[0] = key; return 1; } return 0;
}
