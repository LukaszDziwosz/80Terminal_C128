#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ansi_terminal.h"

/* VDC RAM, and the page the display currently shows: tests read the
 * visible screen through vdc[], with attributes at vdc[0x800 + n]. */
static uint8_t ram[16384], sent[32], sent_length, restored;
static uint8_t *vdc = ram;
static uint16_t shown_chars, shown_attrs, cursor_address;
static uint8_t cursor_visible, watch_visible;
static unsigned flips, visible_writes;

uint8_t screen_load_cp437(uint8_t device) { return device == 8; }
void screen_restore_font(void) { ++restored; }
uint8_t screen_reg_read(uint8_t reg)
{
    if (reg == 20) return 8;
    return 0;
}
void screen_reg_write(uint8_t reg, uint8_t value) { (void)reg; (void)value; }
static void track(uint16_t at, uint16_t length)
{
    /* A write into the displayed page while a scroll is being built. */
    if (!watch_visible) return;
    if ((at < shown_chars + 2000 && at + length > shown_chars) ||
        (at < shown_attrs + 2000 && at + length > shown_attrs)) ++visible_writes;
}
void screen_write_run(uint16_t at, const uint8_t *data, uint16_t length)
{ track(at, length); memcpy(ram + at, data, length); }
void screen_fill_run(uint16_t at, uint8_t value, uint16_t length)
{ track(at, length); memset(ram + at, value, length); }
void screen_copy_run(uint16_t destination, uint16_t source, uint16_t length)
{
    /* Match real VDC forward-only block copies, not host memmove. */
    track(destination, length);
    while (length--) ram[destination++] = ram[source++];
}
void screen_show(uint16_t chars, uint16_t attrs)
{
    assert(attrs == chars + 0x800);
    shown_chars = chars; shown_attrs = attrs; vdc = ram + chars;
    watch_visible = 0; ++flips;
}
void screen_set_cursor(uint16_t at, uint8_t visible)
{ cursor_address = (uint16_t)(at - shown_chars); cursor_visible = visible; }
static void send(const uint8_t *data, uint8_t length)
{ memcpy(sent + sent_length, data, length); sent_length += length; }
static void feed(const char *text)
{ while (*text) ansi_terminal_receive((uint8_t)*text++); }

static void start(uint8_t vt100)
{
    assert(ansi_terminal_init(send, 8, vt100));
    sent_length = 0;
    ansi_terminal_end_update();
}
static void expect_reply(const char *text)
{
    assert(sent_length == strlen(text));
    assert(!memcmp(sent, text, sent_length));
    sent_length = 0;
}
static void test_profiles_and_graphics(void)
{
    uint16_t i;
    start(1);
    feed("\033[25;1HX");
    assert(vdc[23 * 80] == 'X');
    for (i = 24 * 80; i < 25 * 80; ++i) assert(vdc[i] == ' ');
    feed("\033[6n"); expect_reply("\033[24;2R");
    feed("\033[H\033(0lqk\033(Bq\033)0\016x\017q");
    assert(vdc[0] == (0xda & 127) && vdc[1] == (0xc4 & 127));
    assert(vdc[2] == (0xbf & 127) && vdc[3] == 'q');
    assert(vdc[4] == (0xb3 & 127) && vdc[5] == 'q');
    assert(!(vdc[0x800] & 128) && (vdc[0x803] & 128));
    /* A fragmented designation retains parser state between network batches. */
    feed("\033("); ansi_terminal_begin_update();
    feed("0q"); ansi_terminal_end_update();
    assert(vdc[6] == (0xc4 & 127));
    ansi_terminal_shutdown();
    start(0);
    feed("\033(0q\033)0\016q\017q");
    assert(vdc[0] == 'q' && vdc[1] == 'q' && vdc[2] == 'q');
    feed("\033[25;80HZ");
    assert(vdc[1999] == 'Z');
    ansi_terminal_shutdown();
}
static void test_margins_and_reverse_index(void)
{
    start(1);
    feed("TOP\033[2;1HA\033[3;1HB\033[4;1HC\033[5;1HEND");
    feed("\033[2;4r\033[?6h\033[6n"); expect_reply("\033[1;1R");
    feed("\033M");
    assert(vdc[0] == 'T' && vdc[80] == ' ' && vdc[160] == 'A');
    assert(vdc[240] == 'B' && vdc[320] == 'E');
    feed("\033[99;99H\033[6n"); expect_reply("\033[3;80R");
    feed("\n");
    assert(vdc[80] == 'A' && vdc[160] == 'B' && vdc[240] == ' ');
    feed("\033[999A\033[6n"); expect_reply("\033[1;80R");
    feed("\033[?6l\033[24;1HLAST\n");
    assert(vdc[23 * 80] == 'L' && vdc[24 * 80] == ' ');
    ansi_terminal_shutdown();
}
static void test_tabs_modes_and_reset(void)
{
    uint8_t key[3];
    start(1);
    feed("\033[3g\033[1;6H\033H\r\tX"); assert(vdc[5] == 'X');
    feed("\033[1;6H\033[g\r\tY"); assert(vdc[79] == 'Y');
    feed("\033[?1;7h\033[20h");
    assert(ansi_terminal_key(0x91, key) == 3 && !memcmp(key, "\033OA", 3));
    assert(ansi_terminal_key(13, key) == 2 && key[0] == 13 && key[1] == 10);
    assert(ansi_terminal_key(3, key) == 1 && key[0] == 3);
    feed("\033[3;4H\033(0\033[31m\033[4m\0337");
    feed("\033(B\033[0m\033[H\0338q");
    assert(vdc[163] == (0xc4 & 127) && vdc[0x800 + 163] == 0x28);
    feed("\033c");
    assert(sent_length == 0); /* RIS resets; it is not a DA request. */
    assert(cursor_address == 0 && vdc[163] == ' ');
    feed("q\tT"); assert(vdc[0] == 'q' && vdc[8] == 'T');
    assert(ansi_terminal_key(0x91, key) == 3 && !memcmp(key, "\033[A", 3));
    assert(ansi_terminal_key(13, key) == 1);
    feed("\033[6n\033[5n\033Z"); expect_reply("\033[1;10R\033[0n\033[?1;0c");
    feed("\033[42n"); expect_reply("");
    ansi_terminal_shutdown();
}
static void test_editing_and_wrap(void)
{
    start(1);
    feed("ABCDE\033[1;2H\033[2@xy"); assert(!memcmp(vdc, "AxyBCDE", 7));
    feed("\033[1;2H\033[2P"); assert(!memcmp(vdc, "ABCDE", 5));
    feed("\033[4hZ\033[4l"); assert(!memcmp(vdc, "AZBCDE", 6));
    feed("\033[2X"); assert(!memcmp(vdc, "AZ  DE", 6));
    feed("\033[1;3H\033[1J"); assert(!memcmp(vdc, "    DE", 6));
    feed("\033[2;1HB\033[3;1HC\033[4;1HD\033[2;4r\033[3;1H\033[L");
    assert(vdc[80] == 'B' && vdc[160] == ' ' && vdc[240] == 'C');
    feed("\033[M"); assert(vdc[160] == 'C' && vdc[240] == ' ');
    feed("\033c\033[1;80HXZ"); assert(vdc[79] == 'X' && vdc[80] == 'Z');
    feed("\033[?7l\033[2;80HAB"); assert(vdc[159] == 'B' && vdc[160] == ' ');
    feed("\033c\033[24;80HXZ");
    assert(vdc[22 * 80 + 79] == 'X' && vdc[23 * 80] == 'Z' && vdc[24 * 80] == ' ');
    ansi_terminal_shutdown();
}

/* Scrolls are built on the hidden page: the visible one is never written
 * until the display switches, so characters and their attributes (whose
 * bit 7 selects the CP437 font half) always change in the same frame. */
static void test_scroll_page_flip(void)
{
    unsigned before;
    uint16_t i;
    uint8_t red;
    start(0);
    /* Row 2 (index 1): shade blocks B0-B2, stored as $30-$32 with bit 7 clear. */
    feed("\033[2;1H\xb0\xb1\xb2\033[1;31mX\033[0m");
    assert(vdc[80] == 0x30 && vdc[81] == 0x31 && vdc[82] == 0x32 && !(vdc[0x800 + 80] & 0x80));
    assert(vdc[83] == 'X' && (vdc[0x800 + 83] & 0x80));
    red = vdc[0x800 + 83];
    assert(red != vdc[0x800 + 80]);
    feed("\033[25;1HBottom");
    before = flips; watch_visible = 1; visible_writes = 0;
    feed("\n");
    assert(flips == before + 1 && visible_writes == 0);
    /* Everything moved up one row, attributes with their characters. */
    assert(vdc[0] == 0x30 && vdc[1] == 0x31 && vdc[2] == 0x32 && !(vdc[0x800] & 0x80));
    assert(vdc[3] == 'X' && vdc[0x800 + 3] == red);
    assert(vdc[23 * 80] == 'B');
    for (i = 24 * 80; i < 2000; ++i) assert(vdc[i] == ' ' && (vdc[0x800 + i] & 0x80));
    /* The cursor follows the newly visible page. */
    ansi_terminal_end_update();
    assert(cursor_address == 24 * 80 + 6 && cursor_visible);
    /* Reverse index at the top and insert-line also flip, never tear. */
    ansi_terminal_begin_update();
    before = flips; watch_visible = 1; visible_writes = 0;
    feed("\033[1;1H\033M");
    assert(flips == before + 1 && visible_writes == 0);
    assert(vdc[0] == ' ' && vdc[80] == 0x30 && !(vdc[0x800 + 80] & 0x80) && vdc[24 * 80] == 'B');
    watch_visible = 1; visible_writes = 0;
    feed("\033[3;1H\033[2L");
    assert(flips == before + 2 && visible_writes == 0);
    assert(vdc[80] == 0x30 && vdc[160] == ' ' && vdc[240] == ' ' && vdc[24 * 80] == ' ');
    /* Margins: rows outside the region are copied across unchanged. */
    feed("\033[2J\033[1;1HTop\033[25;1HEnd\033[5;10r\033[10;1HIn\n");
    assert(vdc[0] == 'T' && vdc[24 * 80] == 'E' && vdc[8 * 80] == 'I' && vdc[9 * 80] == ' ');
    ansi_terminal_shutdown();
    /* The KERNAL's own page is visible again for the screen editor. */
    assert(shown_chars == 0 && shown_attrs == 0x800);
}
static void test_bold_is_independent_of_colour_order(void)
{
    start(0);
    /* 1;33, 33;1 and separate SGRs are all bright yellow; plain 33 stays brown. */
    feed("\033[1;1H\033[1;33mA\033[0m\033[33;1mB\033[0m\033[1m\033[33mC\033[0m\033[33mD");
    assert((vdc[0x800] & 0x0f) == 13 && (vdc[0x801] & 0x0f) == 13 && (vdc[0x802] & 0x0f) == 13);
    assert((vdc[0x803] & 0x0f) == 12);
    /* Default text stays white, SGR 22 drops bold only, 90-97 stay intense. */
    feed("\033[0mE\033[1;31m\033[22mF\033[91mG");
    assert((vdc[0x804] & 0x0f) == 15 && (vdc[0x805] & 0x0f) == 8 && (vdc[0x806] & 0x0f) == 9);
}

int main(void)
{
    uint8_t key[5];
    assert(ansi_terminal_init(send, 8, 0));
    assert(!cursor_visible);
    assert(vdc[0] == ' ' && vdc[0x800] == 0x8f);
    feed("A\033[3;4HB");
    assert(vdc[0] == 'A' && vdc[163] == 'B');
    assert(!cursor_visible);
    ansi_terminal_end_update();
    assert(cursor_address == 164 && cursor_visible);
    ansi_terminal_begin_update();
    assert(!cursor_visible);
    feed("\033[31mC");
    assert(vdc[164] == 'C' && vdc[0x800 + 164] == 0x88);
    assert(!cursor_visible);
    ansi_terminal_end_update();
    assert(cursor_visible && cursor_address == 165);
    ansi_terminal_begin_update();
    feed("\033[?25l");
    ansi_terminal_end_update();
    assert(!cursor_visible);
    ansi_terminal_begin_update();
    feed("\033[?25h");
    assert(!cursor_visible);
    ansi_terminal_end_update();
    assert(cursor_visible);
    sent_length = 0;
    feed("\033[6n\033[5n\033[c");
    assert(!memcmp(sent, "\033[3;6R\033[0n\033[?1;0c", 18));
    assert(ansi_terminal_key(0x91, key) == 3);
    assert(!memcmp(key, "\033[A", 3));
    assert(ansi_terminal_key('Q', key) == 1 && key[0] == 'q');
    assert(ansi_terminal_key(13, key) == 1 && key[0] == 13);
    ansi_terminal_receive_buffer((const uint8_t *)"\033[2J", 4);
    assert(vdc[0] == ' ' && vdc[163] == ' ');
    ansi_terminal_shutdown();
    assert(restored == 1 && !cursor_visible);
    test_profiles_and_graphics();
    test_margins_and_reverse_index();
    test_bold_is_independent_of_colour_order();
    test_tabs_modes_and_reset();
    test_editing_and_wrap();
    test_scroll_page_flip();
    puts("ANSI and VT100 terminal parser tests passed");
    return 0;
}
