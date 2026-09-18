/* Bounded IPv4 and UDP framing, RFC 791/768. No IP fragment reassembly. */
#include <string.h>
#include "ip65_ip.h"
#include "ip65_ethernet.h"
#include "ip65_arp.h"
#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif
const uint8_t *rr_ip_header, *rr_ip_payload, *rr_udp_payload;
uint16_t rr_ip_length, rr_udp_length, rr_udp_source, rr_udp_destination;
uint8_t rr_ip_protocol;

/* Oscar -O2 miscompiled inlined DNS header reads, using offset 0 for the
 * high bytes at offsets 4 and 6. Preserve the pointer through a real call. */
#ifdef __OSCAR64C__
__noinline
#endif
uint16_t rr_get16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
uint32_t rr_get32(const uint8_t *p) { return ((uint32_t)rr_get16(p) << 16) | rr_get16(p + 2); }
void rr_put16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
void rr_put32(uint8_t *p, uint32_t n) { rr_put16(p, (uint16_t)(n >> 16)); rr_put16(p + 2, (uint16_t)n); }
static uint16_t add(uint16_t a, uint16_t b)
{
    uint16_t n = (uint16_t)(a + b);
    return (uint16_t)(n + (n < a));
}
uint16_t rr_sum(uint16_t sum, const uint8_t *p, uint16_t size)
{
    while (size >= 2) { sum = add(sum, rr_get16(p)); p += 2; size -= 2; }
    if (size) sum = add(sum, (uint16_t)*p << 8);
    return sum;
}
uint16_t rr_transport_sum(const uint8_t *ip, const uint8_t *data, uint16_t length)
{
    return rr_sum(add(add(rr_sum(0, ip + 12, 8), ip[9]), length), data, length);
}
uint8_t rr_ip_parse(void)
{
    uint16_t ihl, total;
    const uint8_t *in = rrnet_ip65_eth_in;
    if (rrnet_ip65_eth_in_length < 34 || in[12] != 8 || in[13] != 0) return 0;
    rr_ip_header = in + 14;
    ihl = (uint16_t)(in[14] & 15) * 4;
    total = rr_get16(in + 16);
    if ((in[14] >> 4) != 4 || ihl < 20 || total < ihl ||
        total > rrnet_ip65_eth_in_length - 14 || (rr_get16(in + 20) & 0xbfff) ||
        !in[22] || rr_sum(0, in + 14, ihl) != 0xffff) return 0;
    rr_ip_protocol = in[23]; rr_ip_payload = in + 14 + ihl;
    rr_ip_length = total - ihl;
    return 1;
}
uint8_t rr_ip_local(void)
{
    return rrnet_ip65_ipv4[0] && !memcmp(rr_ip_header + 16, rrnet_ip65_ipv4, 4);
}
uint8_t rr_udp_parse(void)
{
    uint16_t size;
    if (rr_ip_protocol != 17 || rr_ip_length < 8) return 0;
    size = rr_get16(rr_ip_payload + 4);
    if (size < 8 || size > rr_ip_length || (rr_get16(rr_ip_payload + 6) &&
        rr_transport_sum(rr_ip_header, rr_ip_payload, size) != 0xffff)) return 0;
    rr_udp_source = rr_get16(rr_ip_payload);
    rr_udp_destination = rr_get16(rr_ip_payload + 2);
    rr_udp_payload = rr_ip_payload + 8; rr_udp_length = size - 8;
    return 1;
}
uint8_t rr_ip_prepare(const uint8_t *destination, uint8_t protocol,
                      uint16_t length, uint16_t ticks)
{
    uint8_t mac[6];
    uint8_t *out = rrnet_ip65_eth_out;
    if (length > 1480 || !destination[0]) return 1;
    if (!rrnet_ip65_arp_lookup(destination, mac, ticks)) return 2;
    memcpy(out, mac, 6);
    rrnet_ip65_eth_set_source(); rrnet_ip65_eth_set_protocol(0);
    memset(out + 14, 0, 20);
    out[14] = 0x45; out[22] = 64; out[23] = protocol;
    rr_put16(out + 16, length + 20);
    out[20] = 0x40; /* Don't fragment; TCP advertises a conservative MSS. */
    memcpy(out + 26, rrnet_ip65_ipv4, 4); memcpy(out + 30, destination, 4);
    rr_put16(out + 24, (uint16_t)~rr_sum(0, out + 14, 20));
    rrnet_ip65_eth_out_length = length + 34;
    return 0;
}
uint8_t rr_udp_send(uint16_t source, uint16_t destination, uint16_t length)
{
    uint16_t sum;
    uint8_t *out = rrnet_ip65_eth_out;
    rr_put16(out + 34, source); rr_put16(out + 36, destination);
    rr_put16(out + 38, length + 8); rr_put16(out + 40, 0);
    sum = (uint16_t)~rr_transport_sum(out + 14, out + 34, length + 8);
    rr_put16(out + 40, sum ? sum : 0xffff);
    return rrnet_ip65_eth_send();
}
#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
