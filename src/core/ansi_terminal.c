/* Oscar64 C128 VDC terminal. Its parser and keyboard behaviour are ported
 * from the proven cc65 terminal, but writes directly to the active VDC page. */
#include <string.h>
#include "ansi_terminal.h"
#include "vdc_screen.h"

#ifdef __OSCAR64C__
/* Keep the shared parser within the resident region below adapter buffers. */
#pragma optimize(push, size, outline)
#endif

#ifdef T80_APP
/* Part of the uiterm overlay (overlay.h): only needed during a session. */
#pragma code(termcode)
#pragma data(termdata)
#endif
#define W 80
#define H height
#define ST_CHARSET 5
#define ESC 27
#define ST_TEXT 0
#define ST_ESC 1
#define ST_CSI 2
#define ST_STRING 3
#define ST_STRING_ESC 4

/* Two page pairs: the visible one (chars/attrs) and a hidden one, where
 * scrolls are built before the display switches to it. Characters and
 * attributes therefore always change together on screen. In ANSI mode the
 * attribute's bit 7 selects the font half, so a moved graphics character
 * under an old attribute would show as an ASCII digit or symbol. */
#define PAGE_FLIP 0x1000
#define CELLS 2000
static uint16_t chars, attrs, original_chars, original_attrs;
static uint8_t x, y, saved_x, saved_y, state, params[8], count, private_mark;
static uint8_t attr, wrap, autowrap, cursor_visible, application_cursor;
static uint8_t newline_mode, scroll_top, scroll_bottom;
static uint8_t batch_update, height, vt100_enabled, charset_target;
static uint8_t g0_graphics, g1_graphics, active_charset, origin_mode, insert_mode;
static uint8_t tabs[10], last_printable;
static uint8_t fg, bg, bright, reverse, under, blink;
static uint8_t saved_modes[14];

static void save_state(void)
{
    saved_x = x; saved_y = y;
    saved_modes[0] = autowrap; saved_modes[1] = origin_mode;
    saved_modes[2] = insert_mode; saved_modes[3] = newline_mode;
    saved_modes[4] = g0_graphics; saved_modes[5] = g1_graphics;
    saved_modes[6] = active_charset; saved_modes[7] = fg;
    saved_modes[8] = bg; saved_modes[9] = bright;
    saved_modes[10] = reverse; saved_modes[11] = under;
    saved_modes[12] = blink; saved_modes[13] = attr;
}
static void restore_state(void)
{
    x = saved_x; y = saved_y; wrap = 0;
    autowrap = saved_modes[0]; origin_mode = saved_modes[1];
    insert_mode = saved_modes[2]; newline_mode = saved_modes[3];
    g0_graphics = saved_modes[4]; g1_graphics = saved_modes[5];
    active_charset = saved_modes[6]; fg = saved_modes[7];
    bg = saved_modes[8]; bright = saved_modes[9];
    reverse = saved_modes[10]; under = saved_modes[11];
    blink = saved_modes[12]; attr = saved_modes[13];
}
static void reset_tabs(void)
{
    uint8_t i;
    memset(tabs, 0, sizeof(tabs));
    for (i = 8; i < W; i += 8) tabs[i >> 3] |= 1 << (i & 7);
}
static void reset_state(void)
{
    x = y = wrap = application_cursor = newline_mode = origin_mode = insert_mode = 0;
    g0_graphics = g1_graphics = active_charset = charset_target = 0;
    autowrap = cursor_visible = 1; scroll_top = 0; scroll_bottom = H - 1;
    fg = 7; bg = reverse = under = blink = 0; bright = 1; attr = 15;
    last_printable = ' '; reset_tabs(); save_state();
}
static ansi_send_callback send_bytes;

static uint16_t address(uint8_t col, uint8_t row)
{ return chars + (uint16_t)row * W + col; }
static uint16_t attr_address(uint8_t col, uint8_t row)
{ return attrs + (uint16_t)row * W + col; }
static uint8_t parameter(uint8_t n, uint8_t fallback)
{ return n >= count || !params[n] ? fallback : params[n]; }
static void cursor(void)
{ if (!batch_update) screen_set_cursor(address(x, y), cursor_visible); }
void ansi_terminal_begin_update(void)
{
    if (!batch_update) screen_set_cursor(address(x, y), 0);
    batch_update = 1;
}
void ansi_terminal_end_update(void)
{
    if (!batch_update || state != ST_TEXT) return;
    batch_update = 0;
    cursor();
}
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
/* Copy rows from the visible page to the same or another row of the hidden one. */
static void copy_rows(uint8_t to, uint8_t from, uint8_t rows)
{
    uint16_t length = (uint16_t)rows * W;
    if (!rows) return;
    screen_copy_run((chars ^ PAGE_FLIP) + (uint16_t)to * W, chars + (uint16_t)from * W, length);
    screen_copy_run((attrs ^ PAGE_FLIP) + (uint16_t)to * W, attrs + (uint16_t)from * W, length);
}
static void blank_hidden_rows(uint8_t first, uint8_t rows)
{
    uint16_t length = (uint16_t)rows * W;
    screen_fill_run((chars ^ PAGE_FLIP) + (uint16_t)first * W, 0x20, length);
    screen_fill_run((attrs ^ PAGE_FLIP) + (uint16_t)first * W, attr | 0x80, length);
}
/* Rows outside the scroll region, including VT100's blank 25th row, are
 * copied unchanged; then the hidden page becomes the visible one. */
static void finish_scroll(uint8_t top, uint8_t bottom)
{
    copy_rows(0, 0, top);
    copy_rows((uint8_t)(bottom + 1), (uint8_t)(bottom + 1), (uint8_t)(CELLS / W - 1 - bottom));
    chars ^= PAGE_FLIP; attrs ^= PAGE_FLIP;
    screen_show(chars, attrs);
    cursor();
}
static void scroll_up(uint8_t top, uint8_t bottom, uint8_t lines)
{
    uint8_t rows = (uint8_t)(bottom - top + 1);
    if (!lines) lines = 1;
    if (lines > rows) lines = rows;
    copy_rows(top, (uint8_t)(top + lines), (uint8_t)(rows - lines));
    blank_hidden_rows((uint8_t)(bottom - lines + 1), lines);
    finish_scroll(top, bottom);
}
static void scroll_down(uint8_t top, uint8_t bottom, uint8_t lines)
{
    uint8_t rows = (uint8_t)(bottom - top + 1);
    if (!lines) lines = 1;
    if (lines > rows) lines = rows;
    /* Different pages, so one forward block copy is safe for any direction. */
    copy_rows((uint8_t)(top + lines), top, (uint8_t)(rows - lines));
    blank_hidden_rows(top, lines);
    finish_scroll(top, bottom);
}
static void edit_characters(uint8_t n, uint8_t insert)
{
    uint8_t col;
    if (n > W - x) n = W - x;
    if (insert) {
        /* Rightward overlapping copies must also proceed backwards. */
        for (col = W; col > x + n;) {
            --col;
            screen_copy_run(address(col, y), address(col - n, y), 1);
            screen_copy_run(attr_address(col, y), attr_address(col - n, y), 1);
        }
        clear_line(y, x, x + n - 1);
    } else {
        if (n < W - x) {
            screen_copy_run(address(x, y), address(x + n, y), W - x - n);
            screen_copy_run(attr_address(x, y), attr_address(x + n, y), W - x - n);
        }
        clear_line(y, W - n, W - 1);
    }
}
static uint8_t dec_graphics(uint8_t value)
{
    /* Same CP437 approximations as the original cc65 renderer. */
    static const uint8_t graphics[31] = {
        0xf9,0xb1,0x62,0x63,0x64,0x65,0xf8,0xf1,
        0x68,0x69,0xd9,0xbf,0xda,0xc0,0xc5,0xc4,
        0xc4,0xc4,0xc4,0xc4,0xc3,0xb4,0xc1,0xc2,
        0xb3,0x3c,0x3e,0xe3,0x21,0x9c,0xfa
    };
    return value >= 0x60 && value <= 0x7e ? graphics[value - 0x60] : value;
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
    last_printable = value;
    if (vt100_enabled && (active_charset ? g1_graphics : g0_graphics))
        value = dec_graphics(value);
    if (insert_mode) edit_characters(1, 1);
    glyph = value >= 128 ? value & 0x7f : value;
    a = attr | (value < 128 ? 0x80 : 0);
    screen_write_run(address(x, y), &glyph, 1);
    screen_write_run(attr_address(x, y), &a, 1);
    if (x == W - 1) wrap = autowrap;
    else ++x;
}
static void move(uint8_t amount, int8_t dx, int8_t dy)
{
    uint8_t top = origin_mode ? scroll_top : 0;
    uint8_t bottom = origin_mode ? scroll_bottom : H - 1;
    if (!amount) amount = 1;
    if (dx > 0) x = amount >= W - x ? W - 1 : x + amount;
    if (dx < 0) x = amount > x ? 0 : x - amount;
    if (dy > 0) y = y >= bottom || amount >= bottom - y ? bottom : y + amount;
    if (dy < 0) y = y <= top || amount > y - top ? top : y - amount;
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
    out[n++] = ESC; out[n++] = '['; n = decimal(out, n, origin_mode ? y - scroll_top + 1 : y + 1); out[n++] = ';';
    n = decimal(out, n, x + 1); out[n++] = 'R'; send_bytes(out, n);
}
static void sgr(void)
{
    uint8_t i;
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
        else if (p == 39) { fg = 7; bright = 1; }
        else if (p == 49) bg = 0;
        else if (p >= 40 && p <= 47) bg = p - 40;
        else if (p >= 100 && p <= 107) bg = p - 100;
    }
    attr = color(fg, bright);
    if (bg) attr = color(bg, 0) | 0x40;
    if (reverse) attr |= 0x40;
    if (under) attr |= 0x20;
    if (blink) attr |= 0x10;
}
static void csi(uint8_t final)
{
    uint8_t n = parameter(0, 1), row, col, i;
    wrap = 0;
    switch (final) {
    case 'A': move(n, 0, -1); break; case 'B': move(n, 0, 1); break;
    case 'C': move(n, 1, 0); break; case 'D': move(n, -1, 0); break;
    case 'E': x = 0; move(n, 0, 1); break; case 'F': x = 0; move(n, 0, -1); break;
    case 'G': x = parameter(0, 1) - 1; if (x >= W) x = W - 1; break;
    case 'd': case 'H': case 'f':
        row = parameter(0, 1) - 1; col = final == 'd' ? x + 1 : parameter(1, 1);
        if (origin_mode) y = row > scroll_bottom - scroll_top ? scroll_bottom : scroll_top + row;
        else y = row >= H ? H - 1 : row;
        x = col > W ? W - 1 : col - 1; break;
    case 'J': if (!params[0]) { clear_line(y, x, W - 1); clear_screen(y + 1, H - 1); } else if (params[0] == 1) { if (y) clear_screen(0, y - 1); clear_line(y, 0, x); } else clear_screen(0, H - 1); break;
    case 'K': if (!params[0]) clear_line(y, x, W - 1); else if (params[0] == 1) clear_line(y, 0, x); else clear_line(y, 0, W - 1); break;
    case 'S': scroll_up(scroll_top, scroll_bottom, n); break;
    case 'T': scroll_down(scroll_top, scroll_bottom, n); break;
    case 'L': if (y >= scroll_top && y <= scroll_bottom) scroll_down(y, scroll_bottom, n); break;
    case 'M': if (y >= scroll_top && y <= scroll_bottom) scroll_up(y, scroll_bottom, n); break;
    case '@': edit_characters(n, 1); break;
    case 'P': edit_characters(n, 0); break;
    case 'X': clear_line(y, x, n >= W - x ? W - 1 : x + n - 1); break;
    case 'b': while (n--) put(last_printable); break;
    case 'a': move(n, 1, 0); break;
    case 'e': move(n, 0, 1); break;
    case 'g': if (!params[0]) tabs[x >> 3] &= ~(1 << (x & 7));
              else if (params[0] == 3) memset(tabs, 0, sizeof(tabs)); break;
    case 'm': sgr(); break;
    case 'n': if (!private_mark && (params[0] == 5 || params[0] == 6)) report(params[0]); break;
    case 'c': report('c'); break;
    case 's': save_state(); break;
    case 'u': restore_state(); break;
    case 'r': scroll_top = parameter(0, 1) - 1; scroll_bottom = parameter(1, H) - 1; if (scroll_top >= scroll_bottom || scroll_bottom >= H) { scroll_top = 0; scroll_bottom = H - 1; } x = 0; y = origin_mode ? scroll_top : 0; break;
    case 'h': case 'l':
        for (i = 0; i < count; ++i) {
            uint8_t enabled = final == 'h';
            if (private_mark == '?') {
                if (params[i] == 1) application_cursor = enabled;
                if (params[i] == 6) { origin_mode = enabled; x = 0; y = enabled ? scroll_top : 0; }
                if (params[i] == 7) autowrap = enabled;
                if (params[i] == 25) cursor_visible = enabled;
            } else if (!private_mark) {
                if (params[i] == 4) insert_mode = enabled;
                if (params[i] == 20) newline_mode = enabled;
            }
        }
        break;
    }
}
uint8_t ansi_terminal_init(ansi_send_callback send, uint8_t device, uint8_t vt100_mode)
{
    if (!screen_load_cp437(device)) return 0;
    original_chars = ((uint16_t)screen_reg_read(12) << 8) | screen_reg_read(13);
    original_attrs = ((uint16_t)screen_reg_read(20) << 8) | screen_reg_read(21);
    /* KERNAL 80-column pages: $0000/$0800, with $1000/$1800 free below the
     * fonts at $2000. Start on the low pair whatever the editor used. */
    chars = original_chars & ~PAGE_FLIP; attrs = original_attrs & ~PAGE_FLIP;
    screen_show(chars, attrs);
    send_bytes = send; vt100_enabled = vt100_mode; height = vt100_mode ? 24 : 25;
    state = ST_TEXT; reset_state(); batch_update = 1;
    screen_set_cursor(address(x, y), 0);
    /* Clear the physical 25th row too; VT100 never renders into it. */
    screen_fill_run(chars, ' ', CELLS); screen_fill_run(attrs, attr | 0x80, CELLS);
    return 1;
}
void ansi_terminal_shutdown(void)
{
    screen_set_cursor(address(x, y), 0);
    /* The KERNAL screen editor writes to its own page: show that again. */
    screen_show(original_chars, original_attrs);
    screen_restore_font();
}
void ansi_terminal_receive(uint8_t value)
{
    if (value == ESC && state != ST_STRING && state != ST_STRING_ESC) {
        wrap = 0; state = ST_ESC; return;
    }
    if ((value == 24 || value == 26) && state != ST_TEXT) { state = ST_TEXT; return; }
    if (state == ST_ESC) {
        if (value == '[') { memset(params, 0, sizeof(params)); count = 1; private_mark = 0; state = ST_CSI; return; }
        if (value == ']' || value == 'P' || value == '^' || value == '_') { state = ST_STRING; return; }
        if (value >= '(' && value <= '+') { charset_target = value - '('; state = ST_CHARSET; return; }
        if (value == '7') save_state(); else if (value == '8') restore_state();
        else if (value == 'D') line_feed(); else if (value == 'E') { x = 0; line_feed(); }
        else if (value == 'H') tabs[x >> 3] |= 1 << (x & 7);
        else if (value == 'M') { if (y == scroll_top) scroll_down(scroll_top, scroll_bottom, 1); else if (y) --y; }
        else if (value == 'Z') report('c');
        else if (value == 'c') { reset_state(); clear_screen(0, H - 1); }
        state = ST_TEXT; cursor(); return;
    }
    if (state == ST_CHARSET) {
        if (vt100_enabled && charset_target == 0) g0_graphics = value == '0';
        if (vt100_enabled && charset_target == 1) g1_graphics = value == '0';
        state = ST_TEXT; return;
    }
    if (state == ST_CSI) {
        if (value >= '0' && value <= '9') { uint8_t *p = &params[count - 1]; *p = *p > 25 || (*p == 25 && value > '5') ? 255 : (uint8_t)(*p * 10 + value - '0'); }
        else if (value == ';' && count < sizeof(params)) ++count;
        else if ((value == '?' || value == '>' || value == '!') && count == 1 && !params[0]) private_mark = value;
        else if (value >= '@' && value <= '~') { csi(value); state = ST_TEXT; cursor(); }
        return;
    }
    if (state == ST_STRING) { if (value == 7) state = ST_TEXT; else if (value == ESC) state = ST_STRING_ESC; return; }
    if (state == ST_STRING_ESC) { state = value == '\\' ? ST_TEXT : ST_STRING; return; }
    if (value == ESC) state = ST_ESC;
    else if (value == 8) { wrap = 0; if (x) --x; }
    else if (value == 9) {
        wrap = 0;
        do { if (x == W - 1) break; ++x; } while (!(tabs[x >> 3] & (1 << (x & 7))));
    }
    else if (value == 10 || value == 11 || value == 12) line_feed();
    else if (value == 13) { wrap = 0; x = 0; }
    else if ((value == 14 || value == 15) && vt100_enabled) { wrap = 0; active_charset = value == 14; }
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
#ifdef __OSCAR64C__
#pragma optimize(pop)
#endif
#ifdef T80_APP
#pragma code(code)
#pragma data(data)
#endif
