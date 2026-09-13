#include <assert.h>
#include <stdio.h>
#include "keyboard.h"
int main(void)
{
    uint8_t out[3];
    unsigned key;
    assert(keyboard_encode(0, 0, out) == 0);
    for (key = 0x41; key <= 0x5a; ++key) {
        assert(keyboard_encode(key, 0, out) == 1 && out[0] == key + 32);
        assert(keyboard_encode(key + 128, 0, out) == 1 && out[0] == key);
    }
    assert(keyboard_encode(13, 0, out) == 1 && out[0] == 13);
    assert(keyboard_encode(20, 0, out) == 1 && out[0] == 8);
    assert(keyboard_encode(0x91, 0, out) == 3);
    assert(out[0] == 27 && out[1] == '[' && out[2] == 'A');
    for (key = 1; key < 256; ++key)
        assert(keyboard_encode(key, 1, out) == 1 && out[0] == key);
    puts("Keyboard encoding tests passed");
    return 0;
}
