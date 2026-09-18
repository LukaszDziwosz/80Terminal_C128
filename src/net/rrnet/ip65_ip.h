#ifndef T80_IP65_IP_H
#define T80_IP65_IP_H
#include <stdint.h>

/* Views of the current Ethernet input buffer; valid until the next poll. */
extern const uint8_t *rr_ip_header, *rr_ip_payload, *rr_udp_payload;
extern uint16_t rr_ip_length, rr_udp_length, rr_udp_source, rr_udp_destination;
extern uint8_t rr_ip_protocol;
uint16_t rr_get16(const uint8_t *p);
uint32_t rr_get32(const uint8_t *p);
void rr_put16(uint8_t *p, uint16_t value);
void rr_put32(uint8_t *p, uint32_t value);
uint16_t rr_sum(uint16_t sum, const uint8_t *p, uint16_t size);
uint16_t rr_transport_sum(const uint8_t *ip, const uint8_t *data, uint16_t length);
uint8_t rr_ip_parse(void);
uint8_t rr_udp_parse(void);
uint8_t rr_ip_local(void);
/* Prepare Ethernet/IP headers before writing transport data at offset 34.
 * 0=ready, 1=invalid arguments/no route, 2=waiting for ARP (caller retries).
 * The ARP path may overwrite the output buffer with an ARP request. */
uint8_t rr_ip_prepare(const uint8_t *destination, uint8_t protocol,
                      uint16_t length, uint16_t ticks);
/* Header prepared and payload written at offset 42 before this call. */
uint8_t rr_udp_send(uint16_t source, uint16_t destination, uint16_t length);
#endif
