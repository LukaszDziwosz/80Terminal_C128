#ifndef T80_BANK1_H
#define T80_BANK1_H
#include <stdint.h>

/* C128 RAM bank 1 map. Bank 1 $0000-$03FF is common RAM shared with bank 0,
 * and $FF00-$FFFF holds the MMU registers and the KERNAL's interrupt
 * trampoline copy, so only $0400-$FDFF is used. These are disk-independent
 * C128 RAM banks, unlike Oscar64 overlay numbers. */
#define BANK1_START          0x0400u
/* Overlay copies, packed at their real lengths (overlay.c). */
#define BANK1_OVERLAY_CACHE  0x0400u
#define BANK1_OVERLAY_END    0x8000u
/* Phonebook and recent connections: struct phonebook_entry records. */
#define BANK1_ENTRY_STRIDE   96u
#define BANK1_PHONEBOOK      0x8000u  /* PHONEBOOK_MAX entries */
#define BANK1_RECENT         0x9400u  /* RECENT_MAX entries, newest first */
#define BANK1_RECENT_END     0x9800u
#define BANK1_FONT           0x9800u  /* CP437 font after its first load: 4 KB */
#define BANK1_SCREEN         0xa800u  /* terminal screen during a transfer: 2000 + 2000 */
#define BANK1_DIRECTORY      0xb800u  /* disk directory for the upload list */
#define BANK1_DIRECTORY_END  0xc800u
/* $C800-$FDFF: free. */
#define BANK1_END            0xfe00u

/* Byte copies through the KERNAL's INDFET ($FF74) and INDSTA ($FF77).
 * They switch the MMU from common code, so they are safe with interrupts
 * enabled and leave the caller's configuration (and I/O) unchanged. */
void bank1_read(void *destination, uint16_t source, uint16_t length);
void bank1_write(uint16_t destination, const void *source, uint16_t length);
#endif
