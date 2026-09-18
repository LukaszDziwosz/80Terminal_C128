#ifndef T80_IP65_DNS_H
#define T80_IP65_DNS_H
#include <stdint.h>
enum rr_dns_phase { RR_DNS_IDLE, RR_DNS_WAIT, RR_DNS_DONE, RR_DNS_ERROR, RR_DNS_TIMEOUT };
extern uint8_t rr_dns_state, rr_dns_address[4];
int rr_dns_start(const char *hostname, uint16_t ticks);
void rr_dns_poll(uint16_t ticks);
/* Input already validated by the shared IPv4/UDP parser. */
void rr_dns_input(uint16_t ticks);
void rr_dns_cancel(void);
#endif
