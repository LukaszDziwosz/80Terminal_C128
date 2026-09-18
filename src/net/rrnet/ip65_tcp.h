#ifndef T80_IP65_TCP_H
#define T80_IP65_TCP_H
#include <stdint.h>
#define RR_TCP_MSS 536U
#define RR_TCP_RX_CAPACITY 2048U
enum rr_tcp_failure { RR_TCP_NO_ERROR, RR_TCP_REFUSED, RR_TCP_RESET, RR_TCP_TIMEOUT, RR_TCP_IO_ERROR };
extern uint8_t rr_tcp_error;
void rr_tcp_init(void);
int rr_tcp_connect(const uint8_t *ip, uint16_t port, uint16_t ticks);
void rr_tcp_input(uint16_t ticks);
void rr_tcp_poll(uint16_t ticks);
int rr_tcp_read(uint8_t *buffer, uint16_t capacity);
int rr_tcp_write(const uint8_t *buffer, uint16_t length);
int rr_tcp_state(void);
void rr_tcp_close(uint16_t ticks);
void rr_tcp_abort(void);
#endif
