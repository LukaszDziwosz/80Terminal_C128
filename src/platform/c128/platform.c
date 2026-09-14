#ifndef PLATFORM_TEST
#include <conio.h>
#endif
#include "platform.h"

#ifndef PLATFORM_TEST
static uint8_t boot_device;
static uint8_t entry_speed;

void platform_init(void)
{
    /* Last-used KERNAL device must be captured before opening any files. */
    boot_device = *(volatile uint8_t *)0xba;
    if (boot_device < 8 || boot_device > 30) boot_device = 8;
    entry_speed = *(volatile uint8_t *)0xd030;
    dispmode80col();
    *(volatile uint8_t *)0xd030 = entry_speed | 1;
    iocharmap(IOCHM_PETSCII_2);
    bgcolor(COLOR_BLACK);
    textcolor(COLOR_WHITE);
}
uint8_t platform_device(void) { return boot_device; }
uint8_t platform_slow(void)
{
    uint8_t old = *(volatile uint8_t *)0xd030;
    *(volatile uint8_t *)0xd030 = old & 0xfe;
    return old;
}
void platform_restore_speed(uint8_t value)
{
    *(volatile uint8_t *)0xd030 = value;
}
void platform_exit(void)
{
    clrscr();
    platform_restore_speed(entry_speed);
}
#endif
uint16_t platform_ticks(void)
{
    return __asm volatile {
        jsr $ffde
        sta accu
        stx accu + 1
    };
}
uint8_t platform_key_raw(void)
{
    return __asm volatile { jsr $ffe4; sta accu };
}
