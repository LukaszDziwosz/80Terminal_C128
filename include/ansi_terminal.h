#ifndef T80_ANSI_TERMINAL_H
#define T80_ANSI_TERMINAL_H
#include <stdint.h>
typedef void (*ansi_send_callback)(const uint8_t *data, uint8_t length);
/* ANSI: 80x25 CP437. VT100: 80x24 with DEC G0/G1 graphics. */
uint8_t ansi_terminal_init(ansi_send_callback send, uint8_t device, uint8_t vt100_mode);
void ansi_terminal_shutdown(void);
void ansi_terminal_begin_update(void);
void ansi_terminal_end_update(void);
void ansi_terminal_receive(uint8_t value);
void ansi_terminal_receive_buffer(const uint8_t *data, uint8_t length);
uint8_t ansi_terminal_key(uint8_t key, uint8_t *output);
#endif
