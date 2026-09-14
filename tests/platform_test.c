#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"

int main(void)
{
    /* Stub RDTIM with deliberately different X and Y return values. */
    static const uint8_t clock_stub[] = {0xa9,0xfe,0xa2,0x12,0xa0,0x79,0x60};
    uint16_t before, after;
    memcpy((void *)0xffde, clock_stub, sizeof(clock_stub));
    before = platform_ticks();
    assert(before == 0x12fe);
    *(volatile uint8_t *)0xffdf = 0x06;
    *(volatile uint8_t *)0xffe1 = 0x13;
    after = platform_ticks();
    assert(after == 0x1306);
    assert((uint16_t)(after - before) == 8);
    assert((int16_t)(after - (uint16_t)(before + 6)) >= 0);
    puts("KERNAL clock register and deadline tests passed");
    return 0;
}
