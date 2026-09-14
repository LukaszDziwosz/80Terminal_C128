#ifndef VDC_TEST
#include <conio.h>
#include <c64/kernalio.h>
#endif
#include "platform.h"
#include "vdc_screen.h"

#define VDC_INDEX (*(volatile uint8_t *)0xd600)
#define VDC_DATA (*(volatile uint8_t *)0xd601)
#ifndef VDC_TEST
static uint8_t font_chunk[256];
static uint8_t saved_font_reg;
static uint8_t font_active;

static void ready(void) { while (!(VDC_INDEX & 0x80)) {} }
void screen_reg_write(uint8_t reg, uint8_t value)
{
    ready(); VDC_INDEX = reg; ready(); VDC_DATA = value;
}
uint8_t screen_reg_read(uint8_t reg)
{
    ready(); VDC_INDEX = reg; ready(); return VDC_DATA;
}
#endif
static void update_address(uint16_t address)
{
#ifdef VDC_TEST
    screen_reg_write(18, (uint8_t)(address >> 8));
    screen_reg_write(19, (uint8_t)address);
#else
    /* Keep these as direct volatile accesses.  The pinned Oscar64 incorrectly
     * reused the high-byte argument across two screen_reg_write() calls. */
    ready(); VDC_INDEX = 18;
    ready(); VDC_DATA = (uint8_t)(address >> 8);
    ready(); VDC_INDEX = 19;
    ready(); VDC_DATA = (uint8_t)address;
#endif
}
#ifndef VDC_TEST
void screen_write_run(uint16_t address, const uint8_t *data, uint16_t length)
{
    if (!length) return;
    update_address(address);
    ready(); VDC_INDEX = 31;
    while (length--) { ready(); VDC_DATA = *data++; }
}
#endif
void screen_fill_run(uint16_t address, uint8_t value, uint16_t length)
{
    uint16_t chunk;
    while (length) {
        chunk = length > 256 ? 256 : length;
        update_address(address);
        screen_reg_write(24, screen_reg_read(24) & 0x7f);
        screen_reg_write(31, value);
        /* Writing DATA already writes one byte and advances the address. */
        if (chunk > 1) screen_reg_write(30, (uint8_t)(chunk - 1));
        address += chunk; length -= chunk;
    }
}
void screen_copy_run(uint16_t destination, uint16_t source, uint16_t length)
{
    uint16_t chunk;
    uint8_t control = screen_reg_read(24) & 0x7f;
    while (length) {
        chunk = length > 256 ? 256 : length;
        update_address(destination);
        screen_reg_write(24, control | 0x80);
        screen_reg_write(32, (uint8_t)(source >> 8));
        screen_reg_write(33, (uint8_t)source);
        screen_reg_write(30, chunk == 256 ? 0 : (uint8_t)chunk);
        destination += chunk; source += chunk; length -= chunk;
    }
    screen_reg_write(24, control);
}
void screen_set_cursor(uint16_t address, uint8_t visible)
{
    uint8_t start = screen_reg_read(10) & 0x9f;
    uint8_t end = screen_reg_read(11) & 0xe0;
    screen_reg_write(14, (uint8_t)(address >> 8));
    screen_reg_write(15, (uint8_t)address);
    screen_reg_write(11, end | (screen_reg_read(9) & 0x1f));
    screen_reg_write(10, start | (visible ? 0x60 : 0x20) |
                     (screen_reg_read(9) & 0x1f));
}
#ifndef VDC_TEST
void screen_restore_font(void)
{
    if (!font_active) return;
    screen_reg_write(28, saved_font_reg);
    __asm { jsr $ff62 }  /* C128 KERNAL DLCHR, available with c128e. */
    font_active = 0;
}
uint8_t screen_load_cp437(uint8_t device)
{
    uint16_t offset;
    uint8_t speed;
    uint8_t ok = 1;
    if (font_active) screen_restore_font();
    saved_font_reg = screen_reg_read(28);
    speed = platform_slow();
    krnio_setbnk(0, 0);
    krnio_setnam(p"cp437font,s,r");
    if (!krnio_open(2, device, 2)) {
        krnio_close(2);
        platform_restore_speed(speed);
        return 0;
    }
    font_active = 1;
    for (offset = 0; offset < 4096; offset += 256) {
        if (krnio_read(2, (char *)font_chunk, 256) != 256) {
            ok = 0;
            break;
        }
        /* VDC alternate characters 0..127 are normal ASCII, while the
         * normal bank holds CP437 128..255 as character codes 0..127. */
        if (offset < 2048)
            screen_write_run(0x3000 + offset, font_chunk, 256);
        else
            screen_write_run(0x2000 + offset - 2048, font_chunk, 256);
    }
    /* A trailing byte or I/O error also rejects a malformed font. */
    if (ok && krnio_read(2, (char *)font_chunk, 1) != 0) ok = 0;
    krnio_close(2);
    platform_restore_speed(speed);
    if (!ok) screen_restore_font();
    else screen_reg_write(28, (saved_font_reg & 0x1f) | 0x20);
    return ok;
}
void screen_font_preview(uint8_t device)
{
    uint16_t display, attributes, i;
    uint8_t row;
    clrscr();
    if (!screen_load_cp437(device)) {
        for (const char *s = "Could not load cp437font. Press a key."; *s; ++s)
            putch(*s);
        getch();
        return;
    }
    display = ((uint16_t)screen_reg_read(12) << 8) | screen_reg_read(13);
    attributes = ((uint16_t)screen_reg_read(20) << 8) | screen_reg_read(21);
    screen_write_run(display + 163, (const uint8_t *)"CP437 - 256 glyphs. Press any key to return.", 42);
    for (i = 0; i < 256; ++i) font_chunk[i] = (uint8_t)i;
    for (row = 0; row < 8; ++row)
        screen_write_run(display + (uint16_t)(row + 5) * 80 + 3,
                         font_chunk + (uint16_t)row * 32, 32);
    for (i = 0; i < 256; ++i) font_chunk[i] = 0x0f;
    for (row = 0; row < 25; ++row)
        screen_write_run(attributes + (uint16_t)row * 80, font_chunk, 80);
    getch();
    screen_restore_font();
}
#endif
