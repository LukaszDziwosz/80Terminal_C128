#ifndef T80_VDC_SCREEN_H
#define T80_VDC_SCREEN_H
#include <stdint.h>
uint8_t screen_reg_read(uint8_t reg);
void screen_reg_write(uint8_t reg, uint8_t value);
void screen_write_run(uint16_t address, const uint8_t *data, uint16_t length);
void screen_fill_run(uint16_t address, uint8_t value, uint16_t length);
void screen_copy_run(uint16_t destination, uint16_t source, uint16_t length);
void screen_set_cursor(uint16_t address, uint8_t visible);
/* Loads an exact 4096-byte raw file into both 16-byte/glyph font banks.
 * On failure restores the system fonts. KERNAL screen output must not be
 * used until screen_restore_font() after successful loading. */
uint8_t screen_load_cp437(uint8_t device);
void screen_restore_font(void);
void screen_font_preview(uint8_t device);
#endif
