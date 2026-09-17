/* RR-Net DHCP client. Packet formats/state transitions: RFC 2131 and 2132.
 * Uses the existing IP65 Ethernet buffers; no general UDP socket layer yet.
 */
#include <string.h>
#include "ip65_dhcp.h"
#include "ip65_ethernet.h"
#include "ip65_arp.h"
#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif

#define BOOTP 42U
#define OPTIONS 240U
#define PACKET_SIZE 342U
#define TICKS_SECOND 60U
#define ACQUIRE_TICKS 3600U
#define INFINITE_LEASE 0xffffffffUL
#define HAVE_TYPE 1U
#define HAVE_SERVER 2U
#define HAVE_MASK 4U
#define HAVE_ROUTER 8U
#define HAVE_LEASE 16U
#define HAVE_T1 32U
#define HAVE_T2 64U

uint8_t rrnet_dhcp_state;
uint8_t rrnet_dhcp_server[4], rrnet_dhcp_dns[4];
uint32_t rrnet_dhcp_lease;
uint16_t rrnet_dhcp_sent, rrnet_dhcp_received, rrnet_dhcp_replies;
static uint8_t xid[4], offered_ip[4], offered_mask[4], offered_gateway[4];
static uint8_t offered_flags, sent_count;
static uint8_t serial = 0x5a;
static uint16_t deadline, acquire_start, last_ticks, fraction;
static uint32_t lease_age, renew_at, rebind_at;

struct options {
    uint8_t flags, type, overload;
    uint8_t server[4], mask[4], router[4];
    uint32_t lease, t1, t2;
};
/* Static scratch keeps the 6502 runtime stack small. */
static struct options reply;

static uint16_t get16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}
static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}
static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)get16(p) << 16) | get16(p + 2);
}
static uint16_t add_word(uint16_t sum, uint16_t word)
{
    uint16_t result = (uint16_t)(sum + word);
    return (uint16_t)(result + (result < sum));
}
static uint16_t sum_bytes(uint16_t sum, const uint8_t *p, uint16_t size)
{
    while (size >= 2) {
        sum = add_word(sum, get16(p)); p += 2; size -= 2;
    }
    if (size) sum = add_word(sum, (uint16_t)p[0] << 8);
    return sum;
}
static uint8_t equal4(const uint8_t *a, const uint8_t *b)
{
    return !memcmp(a, b, 4);
}
static uint8_t unicast(const uint8_t *ip)
{
    return ip[0] && ip[0] < 224 && ip[0] != 127;
}
static uint8_t valid_mask(const uint8_t *mask)
{
    uint32_t inverse = ~get32(mask);
    return get32(mask) != 0 && !(inverse & (inverse + 1));
}
static void clear_config(void)
{
    memset(rrnet_ip65_ipv4, 0, 4);
    memset(rrnet_ip65_netmask, 0, 4);
    memset(rrnet_ip65_gateway, 0, 4);
    memset(rrnet_dhcp_dns, 0, 4);
    rrnet_dhcp_lease = 0;
    rrnet_ip65_arp_init();
}
static void transaction(uint16_t ticks)
{
    ++serial;
    xid[0] = rrnet_ip65_mac[2] ^ serial;
    xid[1] = rrnet_ip65_mac[3];
    xid[2] = rrnet_ip65_mac[4] ^ (uint8_t)(ticks >> 8);
    xid[3] = rrnet_ip65_mac[5] ^ (uint8_t)ticks;
}

/* Returns 0 sent, 1 hardware error, 2 waiting for ARP. */
static uint8_t send_message(uint16_t ticks)
{
    uint8_t *out = rrnet_ip65_eth_out;
    uint8_t *boot = out + BOOTP;
    uint8_t destination[6];
    uint16_t pos;
    uint8_t renewing = rrnet_dhcp_state == RR_DHCP_RENEWING;
    uint8_t leased = renewing || rrnet_dhcp_state == RR_DHCP_REBINDING;

    if (renewing && !rrnet_ip65_arp_lookup(rrnet_dhcp_server, destination, ticks)) return 2;
    memset(out, 0, PACKET_SIZE);
    if (renewing) memcpy(out, destination, 6);
    else rrnet_ip65_eth_set_broadcast_destination();
    rrnet_ip65_eth_set_source();
    rrnet_ip65_eth_set_protocol(RRNET_IP65_ETH_IP);
    out[14] = 0x45;
    put16(out + 16, PACKET_SIZE - 14);
    out[22] = 64; out[23] = 17;
    if (leased) memcpy(out + 26, rrnet_ip65_ipv4, 4);
    if (renewing) memcpy(out + 30, rrnet_dhcp_server, 4);
    else memset(out + 30, 255, 4);
    put16(out + 24, (uint16_t)~sum_bytes(0, out + 14, 20));
    put16(out + 34, 68); put16(out + 36, 67);
    put16(out + 38, PACKET_SIZE - 34);
    /* UDP checksum zero is permitted for IPv4. Incoming nonzero checksums
     * are verified, including the pseudo-header. */
    boot[0] = 1; boot[1] = 1; boot[2] = 6;
    memcpy(boot + 4, xid, 4);
    if (!leased) put16(boot + 8, (uint16_t)(ticks - acquire_start) / TICKS_SECOND);
    if (!renewing) boot[10] = 0x80; /* Request broadcast replies. */
    if (leased) memcpy(boot + 12, rrnet_ip65_ipv4, 4);
    memcpy(boot + 28, rrnet_ip65_mac, 6);
    boot[236] = 99; boot[237] = 130; boot[238] = 83; boot[239] = 99;
    pos = OPTIONS;
    boot[pos++] = 53; boot[pos++] = 1;
    boot[pos++] = rrnet_dhcp_state == RR_DHCP_SELECTING ? 1 : 3;
    boot[pos++] = 61; boot[pos++] = 7; boot[pos++] = 1;
    memcpy(boot + pos, rrnet_ip65_mac, 6); pos += 6;
    if (rrnet_dhcp_state == RR_DHCP_REQUESTING) {
        boot[pos++] = 50; boot[pos++] = 4;
        memcpy(boot + pos, offered_ip, 4); pos += 4;
        boot[pos++] = 54; boot[pos++] = 4;
        memcpy(boot + pos, rrnet_dhcp_server, 4); pos += 4;
    }
    boot[pos++] = 57; boot[pos++] = 2;
    put16(boot + pos, 576); pos += 2;
    boot[pos++] = 55; boot[pos++] = 6;
    boot[pos++] = 1; boot[pos++] = 3; boot[pos++] = 6;
    boot[pos++] = 51; boot[pos++] = 58; boot[pos++] = 59;
    boot[pos] = 255;
    rrnet_ip65_eth_out_length = PACKET_SIZE;
    return rrnet_ip65_eth_send();
}

/* Parse bounded TLVs, including options in overloaded BOOTP file/sname.
 * Do not modify live configuration until an entire ACK has been validated. */
static uint8_t parse_options(const uint8_t *p, uint16_t size, uint8_t main_options)
{
    uint8_t code, len, flag;
    while (size) {
        code = *p++; --size;
        if (code == 255) return 1;
        if (!code) continue;
        if (!size) return 0;
        len = *p++; --size;
        if (len > size) return 0;
        flag = 0;
        switch (code) {
        case 53:
            if (len != 1) return 0;
            flag = HAVE_TYPE; reply.type = p[0]; break;
        case 54:
            if (len != 4) return 0;
            flag = HAVE_SERVER; memcpy(reply.server, p, 4); break;
        case 1:
            if (len != 4 || !valid_mask(p)) return 0;
            flag = HAVE_MASK; memcpy(reply.mask, p, 4); break;
        case 3:
            if (!len || (len & 3)) return 0;
            flag = HAVE_ROUTER; memcpy(reply.router, p, 4); break;
        case 6:
            if (!len || (len & 3)) return 0;
            break; /* User override: 1.1.1.1, applied only after ACK. */
        case 51:
            if (len != 4) return 0;
            flag = HAVE_LEASE; reply.lease = get32(p); break;
        case 58:
            if (len != 4) return 0;
            flag = HAVE_T1; reply.t1 = get32(p); break;
        case 59:
            if (len != 4) return 0;
            flag = HAVE_T2; reply.t2 = get32(p); break;
        case 52:
            if (!main_options || reply.overload || len != 1 || !p[0] || p[0] > 3) return 0;
            reply.overload = p[0]; break;
        default: break;
        }
        if (reply.flags & flag) return 0;
        reply.flags |= flag;
        p += len; size -= len;
    }
    return 0; /* Missing END option. */
}

static void receive_message(uint16_t ticks)
{
    const uint8_t *frame = rrnet_ip65_eth_in;
    const uint8_t *ip = frame + 14;
    const uint8_t *udp, *boot;
    uint16_t ihl, total, len, checksum;
    uint8_t initial = rrnet_dhcp_state == RR_DHCP_REQUESTING;

    if (rrnet_ip65_eth_in_length < 14 + 20 + 8 + OPTIONS ||
        frame[12] != 8 || frame[13] != 0 || (ip[0] >> 4) != 4 || ip[9] != 17) return;
    ihl = (uint16_t)(ip[0] & 15) * 4;
    total = get16(ip + 2);
    if (ihl < 20 || total < ihl + 8 + OPTIONS ||
        total > rrnet_ip65_eth_in_length - 14 ||
        (get16(ip + 6) & 0x3fff) || sum_bytes(0, ip, ihl) != 0xffff) return;
    udp = ip + ihl;
    len = get16(udp + 4);
    if (get16(udp) != 67 || get16(udp + 2) != 68 ||
        len < 8 + OPTIONS || len > total - ihl) return;
    ++rrnet_dhcp_replies;
    if (get16(udp + 6)) {
        checksum = sum_bytes(0, ip + 12, 8);
        checksum = add_word(checksum, 17);
        checksum = add_word(checksum, len);
        if (sum_bytes(checksum, udp, len) != 0xffff) return;
    }
    boot = udp + 8;
    if (boot[0] != 2 || boot[1] != 1 || boot[2] != 6 ||
        memcmp(boot + 4, xid, 4) || memcmp(boot + 28, rrnet_ip65_mac, 6) ||
        boot[236] != 99 || boot[237] != 130 || boot[238] != 83 || boot[239] != 99) return;
    memset(&reply, 0, sizeof(reply));
    if (!parse_options(boot + OPTIONS, len - 8 - OPTIONS, 1)) return;
    if ((reply.overload & 1) && !parse_options(boot + 108, 128, 0)) return;
    if ((reply.overload & 2) && !parse_options(boot + 44, 64, 0)) return;
    if ((reply.flags & (HAVE_TYPE | HAVE_SERVER)) != (HAVE_TYPE | HAVE_SERVER) ||
        !unicast(reply.server)) return;

    if (rrnet_dhcp_state == RR_DHCP_SELECTING) {
        if (reply.type != 2 || !unicast(boot + 16)) return;
        memcpy(offered_ip, boot + 16, 4);
        memcpy(rrnet_dhcp_server, reply.server, 4);
        memcpy(offered_mask, reply.mask, 4);
        memcpy(offered_gateway, reply.router, 4);
        offered_flags = reply.flags;
        rrnet_dhcp_state = RR_DHCP_REQUESTING;
        sent_count = 0; deadline = ticks;
        return;
    }
    if (!initial && rrnet_dhcp_state != RR_DHCP_RENEWING &&
        rrnet_dhcp_state != RR_DHCP_REBINDING) return;
    if (rrnet_dhcp_state != RR_DHCP_REBINDING &&
        !equal4(reply.server, rrnet_dhcp_server)) return;
    if (reply.type == 6) {
        clear_config();
        rrnet_dhcp_state = RR_DHCP_SELECTING;
        if (!initial) acquire_start = ticks;
        transaction(ticks);
        sent_count = 0; deadline = ticks + TICKS_SECOND;
        return;
    }
    if (reply.type != 5 || !(reply.flags & HAVE_LEASE) || !reply.lease ||
        !equal4(boot + 16, initial ? offered_ip : rrnet_ip65_ipv4)) return;
    if (!(reply.flags & HAVE_MASK)) {
        if (initial && !(offered_flags & HAVE_MASK)) return;
        memcpy(reply.mask, initial ? offered_mask : rrnet_ip65_netmask, 4);
    }
    if (!(reply.flags & HAVE_ROUTER))
        memcpy(reply.router, initial ? offered_gateway : rrnet_ip65_gateway, 4);
    rrnet_ip65_arp_set_config(boot + 16, reply.mask, reply.router);
    rrnet_ip65_arp_init();
    memcpy(rrnet_dhcp_server, reply.server, 4);
    memset(rrnet_dhcp_dns, 1, 4);
    rrnet_dhcp_lease = reply.lease;
    renew_at = reply.flags & HAVE_T1 ? reply.t1 : reply.lease / 2;
    rebind_at = reply.flags & HAVE_T2 ? reply.t2 : reply.lease - reply.lease / 8;
    if (!renew_at || renew_at >= rebind_at || rebind_at >= reply.lease) {
        rebind_at = reply.lease - reply.lease / 8;
        if (rebind_at >= reply.lease) rebind_at = reply.lease - 1;
        renew_at = rebind_at / 2;
    }
    lease_age = 0; fraction = 0; last_ticks = ticks;
    rrnet_dhcp_state = RR_DHCP_BOUND;
}

void rrnet_dhcp_start(uint16_t ticks)
{
    clear_config();
    memset(rrnet_dhcp_server, 0, 4);
    transaction(ticks);
    acquire_start = last_ticks = deadline = ticks;
    fraction = 0; lease_age = 0; sent_count = 0;
    rrnet_dhcp_sent = rrnet_dhcp_received = rrnet_dhcp_replies = 0;
    rrnet_dhcp_state = RR_DHCP_SELECTING;
}
void rrnet_dhcp_poll(uint16_t ticks)
{
    uint16_t delta;
    uint8_t result;
    uint8_t leased;
    if (rrnet_dhcp_state == RR_DHCP_OFF || rrnet_dhcp_state >= RR_DHCP_TIMEOUT) return;
    delta = (uint16_t)(ticks - last_ticks); last_ticks = ticks;
    lease_age += delta / TICKS_SECOND;
    fraction += delta % TICKS_SECOND;
    if (fraction >= TICKS_SECOND) { ++lease_age; fraction -= TICKS_SECOND; }
    leased = rrnet_dhcp_state >= RR_DHCP_BOUND;
    if (leased && rrnet_dhcp_lease != INFINITE_LEASE) {
        if (lease_age >= rrnet_dhcp_lease) {
            rrnet_dhcp_start(ticks);
        } else if (lease_age >= rebind_at && rrnet_dhcp_state != RR_DHCP_REBINDING) {
            rrnet_dhcp_state = RR_DHCP_REBINDING;
            sent_count = 0; deadline = ticks;
        } else if (lease_age >= renew_at && rrnet_dhcp_state == RR_DHCP_BOUND) {
            transaction(ticks);
            rrnet_dhcp_state = RR_DHCP_RENEWING;
            sent_count = 0; deadline = ticks;
        }
    }
    if (rrnet_ip65_eth_poll()) {
        ++rrnet_dhcp_received;
        if (rrnet_ip65_ipv4[0]) rrnet_ip65_arp_process();
        receive_message(ticks);
    }
    if (rrnet_dhcp_state == RR_DHCP_BOUND) return;
    leased = rrnet_dhcp_state == RR_DHCP_RENEWING || rrnet_dhcp_state == RR_DHCP_REBINDING;
    if (!leased && (uint16_t)(ticks - acquire_start) >= ACQUIRE_TICKS) {
        clear_config(); rrnet_dhcp_state = RR_DHCP_TIMEOUT; return;
    }
    if ((int16_t)(ticks - deadline) < 0) return;
    result = send_message(ticks);
    if (result == 2) return;
    if (!result) ++rrnet_dhcp_sent;
    if (result && !leased) {
        clear_config(); rrnet_dhcp_state = RR_DHCP_IO_ERROR; return;
    }
    /* 4, 8, 16 second retries with a small transaction-specific jitter. */
    deadline = ticks + (uint16_t)(240U << (sent_count < 2 ? sent_count : 2)) + (xid[3] & 31);
    if (sent_count < 2) ++sent_count;
}
#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
