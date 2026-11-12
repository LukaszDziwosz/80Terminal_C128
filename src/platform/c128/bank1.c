#include "bank1.h"

/* Oscar64's c128/bank1.h helpers leave the I/O area switched off after each
 * access, so the KERNAL's own bank-switching routines are used instead.
 * Results come back through accu: a store from __asm into a local can be
 * dropped by the -O2 optimizer. */
__zeropage uint16_t bank1_pointer;
static volatile uint8_t bank1_byte;

void bank1_read(void *destination, uint16_t source, uint16_t length)
{
    uint8_t *out = (uint8_t *)destination;
    bank1_pointer = source;
    while (length) {
        *out++ = (uint8_t)(__asm volatile {
            lda #bank1_pointer
            ldx #1
            ldy #0
            jsr $ff74
            sta accu
        });
        ++bank1_pointer;
        --length;
    }
}

void bank1_write(uint16_t destination, const void *source, uint16_t length)
{
    const uint8_t *in = (const uint8_t *)source;
    bank1_pointer = destination;
    /* STAVEC: INDSTA takes its zero-page pointer address from $02B9. */
    *(volatile uint8_t *)0x02b9 = (uint8_t)(uint16_t)&bank1_pointer;
    while (length) {
        bank1_byte = *in++;
        __asm volatile {
            lda bank1_byte
            ldx #1
            ldy #0
            jsr $ff77
        }
        ++bank1_pointer;
        --length;
    }
}
