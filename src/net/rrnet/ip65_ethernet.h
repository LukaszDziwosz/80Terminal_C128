#ifndef T80_IP65_ETHERNET_H
#define T80_IP65_ETHERNET_H

#include <stdint.h>
#include "ip65_rrnet.h"

#define RRNET_IP65_ETH_HEADER 14U
#define RRNET_IP65_ETH_IP 0U
#define RRNET_IP65_ETH_ARP 6U

/* IP65's eth_inp / eth_outp buffers, kept in the RR-Net overlay. */
extern uint8_t rrnet_ip65_eth_in[RRNET_IP65_FRAME_CAPACITY];
extern uint8_t rrnet_ip65_eth_out[RRNET_IP65_FRAME_CAPACITY];
extern uint16_t rrnet_ip65_eth_in_length;
extern uint16_t rrnet_ip65_eth_out_length;

/* Direct Oscar64 __asm ports of IP65 ip65/eth.s helpers. */
void rrnet_ip65_eth_set_broadcast_destination(void);
void rrnet_ip65_eth_set_source(void);
void rrnet_ip65_eth_set_protocol(uint8_t protocol);

/* One non-blocking receive attempt / one completed frame transmission. */
uint8_t rrnet_ip65_eth_poll(void);
uint8_t rrnet_ip65_eth_send(void);

#endif
