#ifndef T80_UI_DRAW_H
#define T80_UI_DRAW_H
/* Screen drawing for UI overlays. Header-only: overlay code cannot be
 * shared between overlays, so each overlay source includes this once, after
 * its section pragmas, and gets its own static copy (about 700 bytes).
 * Text is ASCII, converted to screen codes, written straight into the
 * KERNAL's 80-column page ($0000 characters, $0800 attributes). */
#include <stdint.h>
#include "version.h"
#include "vdc_screen.h"

/* VDC attribute bits and RGBI colours (the low nibble). */
#define ATTR_ALT      0x80  /* alternate font: the KERNAL's lower/upper set */
#define ATTR_REVERSE  0x40
#define VDC_BLACK       0
#define VDC_DARK_GREY   1
#define VDC_LIGHT_GREEN 5
#define VDC_LIGHT_CYAN  7
#define VDC_LIGHT_RED   9
#define VDC_LIGHT_YELLOW 13
#define VDC_LIGHT_GREY  14
#define VDC_WHITE       15

/* Roles matching the Flutter app's theme (lib/ui/terminal_theme.dart):
 * amber actions, cyan connection metadata, green carrier, light text,
 * muted hints and dark panel rims, on the VDC's black background. */
#define UI_TEXT    (ATTR_ALT | VDC_LIGHT_GREY)
#define UI_HEADING (ATTR_ALT | VDC_WHITE)
#define UI_MUTED   (ATTR_ALT | VDC_DARK_GREY)
#define UI_RIM     (ATTR_ALT | VDC_DARK_GREY)
#define UI_INFO    (ATTR_ALT | VDC_LIGHT_CYAN)
#define UI_GOOD    (ATTR_ALT | VDC_LIGHT_GREEN)
#define UI_ERROR   (ATTR_ALT | VDC_LIGHT_RED)
#define UI_ACTION  (ATTR_ALT | VDC_LIGHT_YELLOW)
#define UI_BAR     (ATTR_ALT | ATTR_REVERSE | VDC_LIGHT_YELLOW)

#define UI_COLS 80
#define UI_ROWS 25
#define UI_PANEL_COL 2
#define UI_PANEL_WIDTH 76

/* ui_text returns the column after the text. ui_text_field pads with spaces
 * (or cuts) to exactly width columns. ui_title: row 0, app name and version,
 * then the screen's name. ui_keys: row 24, pairs of key cap and label ending
 * with 0. ui_panel: a rim with its heading in the top edge, height rows
 * including edges. */
#define UI_CHARS 0x0000
#define UI_ATTRS 0x0800

/* PETSCII screen codes, valid in both KERNAL font sets. */
#define SC_HLINE 64
#define SC_VLINE 93
#define SC_TOP_LEFT 112
#define SC_TOP_RIGHT 110
#define SC_BOTTOM_LEFT 109
#define SC_BOTTOM_RIGHT 125

static uint8_t run[UI_COLS];  /* in the including overlay's BSS */

static uint8_t screen_code(char c)
{
    uint8_t v = (uint8_t)c;
    if (v >= 'a' && v <= 'z') return (uint8_t)(v - 96);
    if (v >= 'A' && v <= 'Z') return v;          /* capitals in the lower/upper set */
    if (v == '@') return 0;
    if (v == '_') return 100;                    /* the set's underline glyph */
    if (v >= 91 && v <= 95) return (uint8_t)(v - 64);
    return v < 64 ? v : 32;
}
static uint16_t at(uint8_t col, uint8_t row) { return (uint16_t)row * UI_COLS + col; }
static void put_run(uint8_t col, uint8_t row, uint8_t length, uint8_t attr)
{
    if (!length) return;
    screen_write_run(UI_CHARS + at(col, row), run, length);
    screen_fill_run(UI_ATTRS + at(col, row), attr, length);
}
static void repeat(uint8_t col, uint8_t row, uint8_t length, uint8_t code, uint8_t attr)
{
    if (!length) return;
    screen_fill_run(UI_CHARS + at(col, row), code, length);
    screen_fill_run(UI_ATTRS + at(col, row), attr, length);
}

static void ui_clear(void)
{
    screen_fill_run(UI_CHARS, 32, UI_COLS * UI_ROWS);
    screen_fill_run(UI_ATTRS, UI_TEXT, UI_COLS * UI_ROWS);
}
/* Oscar64 1.32.273 -O2 computed "return col + n" after put_run() from a
 * caller's zero-page register instead of this parameter, which put_run()
 * may overwrite (the phonebook's key bar then drew at column 130). The end
 * column is therefore stored in memory before the call. */
static uint8_t text_end;
static uint8_t ui_text(uint8_t col, uint8_t row, const char *text, uint8_t attr)
{
    uint8_t n = 0;
    while (text[n] && col + n < UI_COLS) { run[n] = screen_code(text[n]); ++n; }
    text_end = (uint8_t)(col + n);
    put_run(col, row, n, attr);
    return text_end;
}
static void ui_text_field(uint8_t col, uint8_t row, uint8_t width, const char *text, uint8_t attr)
{
    uint8_t n = 0;
    while (n < width && text[n]) { run[n] = screen_code(text[n]); ++n; }
    while (n < width) run[n++] = 32;
    put_run(col, row, width, attr);
}
static void ui_title(const char *screen)
{
    uint8_t n = 0;
    repeat(0, 0, UI_COLS, 32, UI_BAR);
    ui_text(2, 0, "80Terminal " APP_VERSION, UI_BAR);
    while (screen[n]) ++n;
    ui_text((uint8_t)(UI_COLS - 2 - n), 0, screen, UI_BAR);
}
static void ui_keys(const char *const *pairs)
{
    uint8_t col = 1;
    repeat(0, UI_ROWS - 1, UI_COLS, 32, UI_TEXT);
    while (pairs[0] && col < UI_COLS - 4) {
        /* Key cap: reverse amber with a space either side. */
        repeat(col, UI_ROWS - 1, 1, 32, UI_BAR);
        col = ui_text((uint8_t)(col + 1), UI_ROWS - 1, pairs[0], UI_BAR);
        repeat(col, UI_ROWS - 1, 1, 32, UI_BAR);
        col = ui_text((uint8_t)(col + 2), UI_ROWS - 1, pairs[1], UI_TEXT);
        col = (uint8_t)(col + 3);
        pairs += 2;
    }
}
static void ui_panel(uint8_t row, uint8_t height, const char *heading)
{
    uint8_t r, last = (uint8_t)(row + height - 1);
    uint8_t right = UI_PANEL_COL + UI_PANEL_WIDTH - 1;
    repeat(UI_PANEL_COL, row, 1, SC_TOP_LEFT, UI_RIM);
    repeat(UI_PANEL_COL + 1, row, UI_PANEL_WIDTH - 2, SC_HLINE, UI_RIM);
    repeat(right, row, 1, SC_TOP_RIGHT, UI_RIM);
    for (r = (uint8_t)(row + 1); r < last; ++r) {
        repeat(UI_PANEL_COL, r, 1, SC_VLINE, UI_RIM);
        repeat(right, r, 1, SC_VLINE, UI_RIM);
    }
    repeat(UI_PANEL_COL, last, 1, SC_BOTTOM_LEFT, UI_RIM);
    repeat(UI_PANEL_COL + 1, last, UI_PANEL_WIDTH - 2, SC_HLINE, UI_RIM);
    repeat(right, last, 1, SC_BOTTOM_RIGHT, UI_RIM);
    if (heading) {
        r = ui_text(UI_PANEL_COL + 2, row, " ", UI_RIM);
        r = ui_text(r, row, heading, UI_HEADING);
        ui_text(r, row, " ", UI_RIM);
    }
}

#endif
