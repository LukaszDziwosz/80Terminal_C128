#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t bridge_image[] = {
    #embed "../build/wic64bridge.bin"
};

static uint8_t available(void)
{
    return __asm volatile { jsr $9006; sta accu; lda #0; sta accu + 1; };
}

int main(void)
{
    uint8_t *entry, *reply;
    uint16_t count;
    memcpy((void *)0x9000, bridge_image, sizeof(bridge_image));
    entry = (uint8_t *)(*(uint16_t *)0x9007);
    /* The real macro loads request and response pointers, then calls the
     * hardware library. Replace only that call with a simulated response. */
    assert(entry[0] == 0xa9 && entry[10] == 0xa9);
    assert(entry[15] == 0xa9 && entry[20] == 0x20);
    reply = (uint8_t *)((uint16_t)entry[11] | ((uint16_t)entry[16] << 8));
    entry[21] = 0x00; entry[22] = 0x88;
    *(uint8_t *)0x8800 = 0xa9; /* LDA #status */
    *(uint8_t *)0x8801 = 0;
    *(uint8_t *)0x8802 = 0x18; /* CLC */
    *(uint8_t *)0x8803 = 0x60; /* RTS */
    for (count = 0; count <= 2560; ++count) {
        reply[0] = (uint8_t)count;
        reply[1] = (uint8_t)(count >> 8);
        assert(available() == 0);
        assert(*(volatile uint16_t *)0x8f82 == count);
    }
    for (count = 1; count <= 5; ++count) {
        *(uint8_t *)0x8801 = (uint8_t)count;
        assert(available() == count);
    }
    *(uint8_t *)0x8802 = 0x38; /* SEC: user-port timeout */
    assert(available() == 0x80);
    puts("WiC64 assembled bridge status/count tests passed");
    return 0;
}
