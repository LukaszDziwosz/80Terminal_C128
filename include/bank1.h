#ifndef T80_BANK1_H
#define T80_BANK1_H
#include <stdint.h>

/* C128 RAM bank 1 map. Bank 1 $0000-$03FF is common RAM shared with bank 0,
 * and $FF00-$FFFF holds the MMU registers and the KERNAL's interrupt
 * trampoline copy, so only $0400-$FDFF is used. These are disk-independent
 * C128 RAM banks, unlike Oscar64 overlay numbers. */
#define BANK1_START          0x0400u
#define BANK1_OVERLAY_CACHE  0x0400u  /* UI overlays, BANK1_OVERLAY_SLOTS slots */
#define BANK1_OVERLAY_SLOTS  4u
#define BANK1_END            0xfe00u

/* Byte copies through the KERNAL's INDFET ($FF74) and INDSTA ($FF77).
 * They switch the MMU from common code, so they are safe with interrupts
 * enabled and leave the caller's configuration (and I/O) unchanged. */
void bank1_read(void *destination, uint16_t source, uint16_t length);
void bank1_write(uint16_t destination, const void *source, uint16_t length);
#endif
