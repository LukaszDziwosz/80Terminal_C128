#ifndef T80_MEMORY_MAP_H
#define T80_MEMORY_MAP_H
/* Bank 0 addresses shared by src/app/layout.h and the code that loads
 * overlays. Keep these as plain numbers: layout.h passes them to #pragma. */

/* Free C128 RAM below the program: $1300-$1BFF is unused by BASIC 7 and
 * the KERNAL. BSS only, so it is never part of a loaded file. It holds all
 * resident BSS: the startup code still zeroes the default bss section here,
 * but not lowbss, whose users initialize it explicitly. */
#define LOWBSS_START   0x1300
#define LOWBSS_END     0x1c00

/* Cassette buffer, RS-232 buffers and VIC sprite area: unused here (no
 * tape or RS-232, VIC blanked). BSS of the UI overlays (uibss, bookbss),
 * which they set explicitly before use. */
#define UIBSS_START    0x0b00
#define UIBSS_END      0x1000

#define MAIN_START     0x1c80
/* UI overlays (overlay.h) take turns in this slot. */
#define UI_SLOT_START  0x3600
#define UI_SLOT_END    0x6000
#define UI_SLOT_SIZE   (UI_SLOT_END - UI_SLOT_START)
#endif
