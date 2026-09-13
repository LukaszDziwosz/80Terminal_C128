#include "keyboard.h"

uint8_t keyboard_encode(uint8_t key, uint8_t petscii, uint8_t *output)
{
    uint8_t arrow = 0;
    if (!key) return 0;
    if (petscii) { output[0] = key; return 1; }
    switch (key) {
    case 0x91: arrow = 'A'; break;
    case 0x11: arrow = 'B'; break;
    case 0x1d: arrow = 'C'; break;
    case 0x9d: arrow = 'D'; break;
    case 0x13: arrow = 'H'; break;
    case 0x14: key = 8; break;
    default:
        if (key >= 0x41 && key <= 0x5a) key += 0x20;
        else if (key >= 0xc1 && key <= 0xda) key -= 0x80;
        break;
    }
    if (arrow) {
        output[0] = 27; output[1] = '['; output[2] = arrow;
        return 3;
    }
    if (key >= 128) return 0;
    output[0] = key;
    return 1;
}
