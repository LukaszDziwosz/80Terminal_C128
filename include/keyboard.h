#ifndef T80_KEYBOARD_H
#define T80_KEYBOARD_H
#include <stdint.h>
/* Convert one KERNAL PETSCII key into up to three wire bytes. */
uint8_t keyboard_encode(uint8_t key, uint8_t petscii, uint8_t *output);
#endif
