#ifndef T80_IP65_RRNET_H
#define T80_IP65_RRNET_H

#include <stdint.h>

#define RRNET_IP65_FRAME_CAPACITY 1518U

/* Init returns zero on success; EEPROM MAC falls back to the cc65 address. */
#define RRNET_IP65_NO_CHIP 1U
#define RRNET_IP65_RESET_TIMEOUT 2U
uint8_t rrnet_ip65_init(void);
uint16_t rrnet_ip65_receive(uint8_t *buffer, uint16_t capacity);
uint8_t rrnet_ip65_send(const uint8_t *buffer, uint16_t length);
void rrnet_ip65_shutdown(void);
extern uint8_t rrnet_ip65_mac[6];

#ifdef RRNET_IP65_TEST
uint8_t rrnet_test_read(uint16_t address);
void rrnet_test_write(uint16_t address, uint8_t value);
#endif

#endif
