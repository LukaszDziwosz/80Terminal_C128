/*
 * ARP state and packet layout ported from IP65 ip65/arp.s.
 * Original Code: ip65; Initial Developer: Jonno Downes <jonno@jamtronix.com>.
 * This file is a modification of that source, made 2026-09-15.  It is
 * available under MPL 1.1; the unmodified license is in
 * References/80Terminal_cc65/third_party/ip65/LICENSE.txt.
 */
#include <string.h>
#include "ip65_arp.h"
#include "ip65_ethernet.h"

#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif

#define ARP_FRAME_LENGTH 42U
#define ARP_CACHE_SIZE 8U
#define ARP_RETRY_TICKS 6U

#define ARP_HARDWARE 14U
#define ARP_PROTOCOL 16U
#define ARP_HARDWARE_LENGTH 18U
#define ARP_PROTOCOL_LENGTH 19U
#define ARP_OPERATION 20U
#define ARP_SENDER_MAC 22U
#define ARP_SENDER_IP 28U
#define ARP_TARGET_MAC 32U
#define ARP_TARGET_IP 38U

struct arp_cache_entry {
    uint8_t mac[6];
    uint8_t ipv4[4];
};

uint8_t rrnet_ip65_ipv4[4];
uint8_t rrnet_ip65_netmask[4];
uint8_t rrnet_ip65_gateway[4];
static struct arp_cache_entry cache[ARP_CACHE_SIZE];
static uint8_t cache_next, waiting;
static uint8_t pending_ipv4[4];
static uint16_t retry_after;

static uint8_t equal4(const uint8_t *left, const uint8_t *right)
{
    return left[0] == right[0] && left[1] == right[1] &&
           left[2] == right[2] && left[3] == right[3];
}
static uint8_t is_broadcast(const uint8_t *ipv4)
{
    return ipv4[0] == 255 && ipv4[1] == 255 &&
           ipv4[2] == 255 && ipv4[3] == 255;
}
static uint8_t same_subnet(const uint8_t *ipv4)
{
    uint8_t i;
    for (i = 0; i < 4; ++i)
        if ((ipv4[i] & rrnet_ip65_netmask[i]) !=
            (rrnet_ip65_ipv4[i] & rrnet_ip65_netmask[i])) return 0;
    return 1;
}
static const uint8_t *routed_ipv4(const uint8_t *ipv4)
{
    return same_subnet(ipv4) ? ipv4 : rrnet_ip65_gateway;
}
static uint8_t find(const uint8_t *ipv4, uint8_t *mac)
{
    uint8_t i;
    for (i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (equal4(cache[i].ipv4, ipv4)) {
            memcpy(mac, cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}
static void add(const uint8_t *ipv4, const uint8_t *mac)
{
    uint8_t i;
    for (i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (equal4(cache[i].ipv4, ipv4)) {
            memcpy(cache[i].mac, mac, 6);
            return;
        }
    }
    memcpy(cache[cache_next].ipv4, ipv4, 4);
    memcpy(cache[cache_next].mac, mac, 6);
    if (++cache_next == ARP_CACHE_SIZE) cache_next = 0;
}
static void prepare_header(uint8_t operation)
{
    uint8_t i;
    rrnet_ip65_eth_set_source();
    rrnet_ip65_eth_set_protocol(RRNET_IP65_ETH_ARP);
    rrnet_ip65_eth_out[ARP_HARDWARE] = 0;
    rrnet_ip65_eth_out[ARP_HARDWARE + 1] = 1;
    rrnet_ip65_eth_out[ARP_PROTOCOL] = 8;
    rrnet_ip65_eth_out[ARP_PROTOCOL + 1] = 0;
    rrnet_ip65_eth_out[ARP_HARDWARE_LENGTH] = 6;
    rrnet_ip65_eth_out[ARP_PROTOCOL_LENGTH] = 4;
    rrnet_ip65_eth_out[ARP_OPERATION] = 0;
    rrnet_ip65_eth_out[ARP_OPERATION + 1] = operation;
    for (i = 0; i < 6; ++i) rrnet_ip65_eth_out[ARP_SENDER_MAC + i] = rrnet_ip65_mac[i];
    for (i = 0; i < 4; ++i) rrnet_ip65_eth_out[ARP_SENDER_IP + i] = rrnet_ip65_ipv4[i];
    rrnet_ip65_eth_out_length = ARP_FRAME_LENGTH;
}
static void request(const uint8_t *ipv4)
{
    uint8_t i;
    rrnet_ip65_eth_set_broadcast_destination();
    prepare_header(1);
    for (i = 0; i < 6; ++i) rrnet_ip65_eth_out[ARP_TARGET_MAC + i] = 0;
    memcpy(rrnet_ip65_eth_out + ARP_TARGET_IP, ipv4, 4);
    (void)rrnet_ip65_eth_send();
}

void rrnet_ip65_arp_set_config(const uint8_t *ipv4, const uint8_t *netmask,
                                const uint8_t *gateway)
{
    memcpy(rrnet_ip65_ipv4, ipv4, 4);
    memcpy(rrnet_ip65_netmask, netmask, 4);
    memcpy(rrnet_ip65_gateway, gateway, 4);
}
void rrnet_ip65_arp_init(void)
{
    memset(cache, 0, sizeof(cache));
    cache_next = waiting = 0;
}
uint8_t rrnet_ip65_arp_lookup(const uint8_t *ipv4, uint8_t *mac,
                              uint16_t ticks)
{
    const uint8_t *target;

    if (is_broadcast(ipv4)) {
        memset(mac, 255, 6);
        return 1;
    }
    target = routed_ipv4(ipv4);
    if (find(target, mac)) return 1;
    if (waiting && equal4(pending_ipv4, target) &&
        (int16_t)(ticks - retry_after) < 0) return 0;
    memcpy(pending_ipv4, target, 4);
    waiting = 1;
    retry_after = (uint16_t)(ticks + ARP_RETRY_TICKS);
    request(target);
    return 0;
}
void rrnet_ip65_arp_process(void)
{
    uint8_t i;
    const uint8_t *in = rrnet_ip65_eth_in;

    if (rrnet_ip65_eth_in_length < ARP_FRAME_LENGTH ||
        in[12] != 8 || in[13] != RRNET_IP65_ETH_ARP ||
        in[ARP_HARDWARE] != 0 || in[ARP_HARDWARE + 1] != 1 ||
        in[ARP_PROTOCOL] != 8 || in[ARP_PROTOCOL + 1] != 0 ||
        in[ARP_HARDWARE_LENGTH] != 6 || in[ARP_PROTOCOL_LENGTH] != 4) return;
    add(in + ARP_SENDER_IP, in + ARP_SENDER_MAC);
    if (in[ARP_OPERATION] != 0 || in[ARP_OPERATION + 1] != 1 ||
        !equal4(in + ARP_TARGET_IP, rrnet_ip65_ipv4)) return;

    memcpy(rrnet_ip65_eth_out, in + ARP_SENDER_MAC, 6);
    prepare_header(2);
    memcpy(rrnet_ip65_eth_out + ARP_TARGET_MAC, in + ARP_SENDER_MAC, 6);
    memcpy(rrnet_ip65_eth_out + ARP_TARGET_IP, in + ARP_SENDER_IP, 4);
    for (i = 0; i < 6; ++i) rrnet_ip65_eth_out[ARP_SENDER_MAC + i] = rrnet_ip65_mac[i];
    (void)rrnet_ip65_eth_send();
}

#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
