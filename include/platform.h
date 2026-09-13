#ifndef T80_PLATFORM_H
#define T80_PLATFORM_H
#include <stdint.h>
void platform_init(void);
void platform_exit(void);
uint8_t platform_device(void);
uint8_t platform_slow(void);
void platform_restore_speed(uint8_t value);
uint16_t platform_ticks(void);
uint8_t platform_key_raw(void);
#endif
