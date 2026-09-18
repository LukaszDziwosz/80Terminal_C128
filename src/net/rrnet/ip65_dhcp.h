#ifndef T80_IP65_DHCP_H
#define T80_IP65_DHCP_H
#include <stdint.h>

enum rrnet_dhcp_phase {
    RR_DHCP_OFF, RR_DHCP_SELECTING, RR_DHCP_REQUESTING, RR_DHCP_BOUND,
    RR_DHCP_RENEWING, RR_DHCP_REBINDING, RR_DHCP_TIMEOUT, RR_DHCP_IO_ERROR
};
extern uint8_t rrnet_dhcp_state;
extern uint8_t rrnet_dhcp_server[4];
extern uint8_t rrnet_dhcp_dns[4];
/* Seconds, as supplied by the server; 0xffffffff means an infinite lease. */
extern uint32_t rrnet_dhcp_lease;
/* Diagnostic totals for the current acquisition/lease lifecycle. */
extern uint16_t rrnet_dhcp_sent, rrnet_dhcp_received, rrnet_dhcp_replies;
/* Call at 1 MHz; poll frequently with the C128 60 Hz clock, including idle.
 * Each poll consumes at most one Ethernet frame. No IP config before ACK. */
void rrnet_dhcp_start(uint16_t ticks);
void rrnet_dhcp_poll(uint16_t ticks);
void rrnet_dhcp_tick(uint16_t ticks);
/* Input must already have passed rr_ip_parse() and rr_udp_parse(). */
void rrnet_dhcp_input(uint16_t ticks);
#endif
