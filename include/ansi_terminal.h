#ifndef T80_ANSI_TERMINAL_H
#define T80_ANSI_TERMINAL_H
#include <stdint.h>
typedef void (*ansi_send_callback)(const uint8_t *data, uint8_t length);
uint8_t ansi_terminal_init(ansi_send_callback send, uint8_t device);
void ansi_terminal_shutdown(void);
void ansi_terminal_receive(uint8_t value);
void ansi_terminal_receive_buffer(const uint8_t *data, uint8_t length);
uint8_t ansi_terminal_key(uint8_t key, uint8_t *output);
#endif
