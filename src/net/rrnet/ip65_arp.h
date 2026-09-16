#ifndef T80_IP65_ARP_H
#define T80_IP65_ARP_H

#include <stdint.h>

/* Network-order IPv4 values. DHCP will populate these before TCP starts. */
extern uint8_t rrnet_ip65_ipv4[4];
extern uint8_t rrnet_ip65_netmask[4];
extern uint8_t rrnet_ip65_gateway[4];

void rrnet_ip65_arp_set_config(const uint8_t *ipv4, const uint8_t *netmask,
                                const uint8_t *gateway);
void rrnet_ip65_arp_init(void);

/* Returns one when `mac` is resolved. On a miss it sends, or continues to
 * await, one ARP request. `ticks` is the C128 60 Hz system clock. */
uint8_t rrnet_ip65_arp_lookup(const uint8_t *ipv4, uint8_t *mac,
                              uint16_t ticks);

/* Consumes the current rrnet_ip65_eth_in frame when it is an ARP packet. */
void rrnet_ip65_arp_process(void);

#endif
