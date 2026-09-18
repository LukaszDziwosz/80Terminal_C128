#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "network.h"
#include "ip65_dhcp.h"
#include "ip65_arp.h"
#include "ip65_ethernet.h"

uint8_t rrnet_ip65_mac[6] = {2, 0x55, 0x54, 0x80, 0, 1};
static uint8_t incoming[1518], sent[1518], transaction_id[4];
static uint16_t incoming_length, sent_length, now;
static unsigned sends;
static uint8_t fail_send, chip_error, speed, check_speed;
static const uint8_t address[4] = {192,168,68,61};
static const uint8_t mask[4] = {255,255,252,0};
static const uint8_t gateway[4] = {192,168,68,1};
static const uint8_t router_mac[6] = {0x10,0x20,0x30,0x40,0x50,0x60};

uint8_t platform_slow(void) { uint8_t old = speed; speed = 0; return old; }
void platform_restore_speed(uint8_t value) { speed = value; }
uint16_t platform_ticks(void) { return now; }
uint8_t rrnet_ip65_init(void) { assert(!speed); return chip_error; }
void rrnet_ip65_shutdown(void) { }
uint16_t rrnet_ip65_receive(uint8_t *buffer, uint16_t capacity)
{
    uint16_t n = incoming_length;
    if (check_speed) assert(!speed);
    assert(n <= capacity);
    memcpy(buffer, incoming, n); incoming_length = 0;
    return n;
}
uint8_t rrnet_ip65_send(const uint8_t *buffer, uint16_t length)
{
    if (check_speed) assert(!speed);
    assert(length <= sizeof(sent));
    memcpy(sent, buffer, length); sent_length = length; ++sends;
    if (sent[12] == 8 && sent[13] == 0) memcpy(transaction_id, sent + 46, 4);
    return fail_send;
}
static uint16_t word(const uint8_t *p) { return (uint16_t)p[0] * 256 + p[1]; }
static void word_put(uint8_t *p, uint16_t n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
/* Independent 32-bit checksum implementation for fixture verification. */
static uint16_t checksum(const uint8_t *p, uint16_t length, uint32_t sum)
{
    while (length > 1) { sum += word(p); p += 2; length -= 2; }
    if (length) sum += (uint16_t)*p << 8;
    while (sum >> 16) sum = (sum & 65535UL) + (sum >> 16);
    return (uint16_t)~sum;
}
static void repair_checksums(void)
{
    uint8_t *ip = incoming + 14;
    uint8_t *udp = ip + (ip[0] & 15) * 4;
    uint16_t n;
    word_put(ip + 10, 0);
    word_put(ip + 10, checksum(ip, (ip[0] & 15) * 4, 0));
    word_put(udp + 6, 0);
    n = word(udp + 4);
    word_put(udp + 6, checksum(udp, n, (uint32_t)word(ip + 12) + word(ip + 14) +
        word(ip + 16) + word(ip + 18) + 17 + n));
    if (!word(udp + 6)) word_put(udp + 6, 65535);
}
static uint16_t option(uint16_t pos, uint8_t type, uint8_t len, const uint8_t *data)
{
    incoming[pos++] = type; incoming[pos++] = len;
    memcpy(incoming + pos, data, len);
    return pos + len;
}
/* Offer/ACK fixtures intentionally contain PAD, unknown, and multi-router
 * options, and a nonzero UDP checksum, as real DHCP servers commonly do. */
static void make_reply(uint8_t type, uint32_t lease)
{
    uint16_t pos = 282;
    uint8_t duration[4];
    uint8_t routers[8] = {192,168,68,1,192,168,68,2};
    uint8_t dns[4] = {192,168,0,1};
    memset(incoming, 0, sizeof(incoming));
    memset(incoming, 255, 6); memcpy(incoming + 6, router_mac, 6);
    incoming[12] = 8; incoming[14] = 0x45; incoming[22] = 64; incoming[23] = 17;
    memcpy(incoming + 26, gateway, 4); memset(incoming + 30, 255, 4);
    word_put(incoming + 34, 67); word_put(incoming + 36, 68);
    incoming[42] = 2; incoming[43] = 1; incoming[44] = 6;
    memcpy(incoming + 46, transaction_id, 4);
    if (type != 6) memcpy(incoming + 58, address, 4);
    memcpy(incoming + 70, rrnet_ip65_mac, 6);
    incoming[278] = 99; incoming[279] = 130; incoming[280] = 83; incoming[281] = 99;
    incoming[pos++] = 0;
    pos = option(pos, 53, 1, &type);
    pos = option(pos, 54, 4, gateway);
    pos = option(pos, 1, 4, mask);
    pos = option(pos, 3, 8, routers);
    pos = option(pos, 6, 4, dns);
    pos = option(pos, 200, 4, dns);
    duration[0] = (uint8_t)(lease >> 24); duration[1] = (uint8_t)(lease >> 16);
    duration[2] = (uint8_t)(lease >> 8); duration[3] = (uint8_t)lease;
    pos = option(pos, 51, 4, duration);
    incoming[pos++] = 255;
    incoming_length = pos;
    word_put(incoming + 16, pos - 14); word_put(incoming + 38, pos - 34);
    repair_checksums();
}
static uint8_t *find_option(uint8_t *frame, uint8_t code)
{
    uint16_t pos = 282;
    while (frame[pos] != 255) {
        if (frame[pos] == code) return frame + pos + 2;
        if (!frame[pos]) ++pos;
        else pos += 2 + frame[pos + 1];
        assert(pos < 600);
    }
    return NULL;
}
static void reset(uint16_t ticks)
{
    now = ticks; incoming_length = sent_length = 0; sends = 0;
    fail_send = chip_error = check_speed = 0; speed = 1;
    rrnet_dhcp_start(now);
    rrnet_dhcp_poll(now);
    assert(rrnet_dhcp_state == RR_DHCP_SELECTING);
    assert(sends == 1 && sent_length == 342);
    assert(checksum(sent + 14, 20, 0) == 0);
    assert(word(sent + 34) == 68 && word(sent + 36) == 67);
    assert(word(sent + 38) == 308 && sent[52] == 0x80);
    assert(*find_option(sent, 53) == 1);
    assert(find_option(sent, 50) == NULL && find_option(sent, 54) == NULL);
    assert(!memcmp(find_option(sent, 61) + 1, rrnet_ip65_mac, 6));
    assert(!memcmp(sent + 70, rrnet_ip65_mac, 6));
}
static void offer(void)
{
    make_reply(2, 120);
    rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_REQUESTING);
    assert(!rrnet_ip65_ipv4[0]);
    assert(*find_option(sent, 53) == 3);
    assert(!memcmp(find_option(sent, 50), address, 4));
    assert(!memcmp(find_option(sent, 54), gateway, 4));
}
static void bind_lease(uint32_t lease)
{
    offer();
    make_reply(5, lease); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND);
    assert(!memcmp(rrnet_ip65_ipv4, address, 4));
    assert(!memcmp(rrnet_ip65_netmask, mask, 4));
    assert(!memcmp(rrnet_ip65_gateway, gateway, 4));
    assert(!memcmp(rrnet_dhcp_server, gateway, 4));
    assert(!memcmp(rrnet_dhcp_dns, (uint8_t[]){1,1,1,1}, 4));
    assert(rrnet_dhcp_lease == lease);
}
static void advance(uint16_t ticks)
{
    now += ticks; rrnet_dhcp_poll(now);
}
static void test_acquisition(void)
{
    unsigned before;
    uint8_t old_xid[4];
    reset(65000); /* Request deadlines and lease clock cross a 16-bit wrap. */
    memcpy(old_xid, transaction_id, 4);
    make_reply(5, 120); rrnet_dhcp_poll(++now); /* No unsolicited ACK. */
    assert(rrnet_dhcp_state == RR_DHCP_SELECTING);
    before = sends;
    advance(300); assert(sends == before + 1);
    assert(!memcmp(old_xid, transaction_id, 4));
    offer();
    before = sends;
    advance(600); assert(sends == before + 1);
    assert(*find_option(sent, 53) == 3);
    make_reply(5, 120); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND);
    assert(!memcmp(rrnet_dhcp_dns, (uint8_t[]){1,1,1,1}, 4));
}
static void test_reject_bad_packets(void)
{
    uint8_t variant;
    reset(100); offer();
    for (variant = 0; variant < 15; ++variant) {
        make_reply(5, 120);
        switch (variant) {
        case 0: incoming[46] ^= 1; break; /* Wrong transaction. */
        case 1: incoming[70] ^= 1; break; /* Wrong client MAC. */
        case 2: incoming[278] = 0; break; /* Bad cookie. */
        case 3: find_option(incoming, 54)[3] = 2; break; /* Other server. */
        case 4: incoming[58] = 10; break; /* Other offered address. */
        case 5: incoming[20] = 0x20; break; /* More fragments. */
        case 6: incoming[37] = 69; break; /* Wrong port. */
        case 7: find_option(incoming, 51)[3] = 0; break; /* Zero lease. */
        case 8: find_option(incoming, 1)[1] = 0; break; /* Noncontiguous mask. */
        case 9: find_option(incoming, 51)[-1] = 255; break; /* Truncated TLV. */
        case 10: incoming[incoming_length - 1] = 0; break; /* No END. */
        case 11: incoming[43] = 2; break; /* Non-Ethernet. */
        default: break;
        }
        repair_checksums();
        if (variant == 12) incoming[24] ^= 1;
        if (variant == 13) incoming[41] ^= 1;
        if (variant == 14) incoming_length -= 3;
        rrnet_dhcp_poll(++now);
        assert(rrnet_dhcp_state == RR_DHCP_REQUESTING);
        assert(!rrnet_ip65_ipv4[0] && !rrnet_dhcp_dns[0]);
    }
    make_reply(5, 120); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND);
}
static void test_option_overload_and_long_options(void)
{
    uint8_t opts[100];
    uint16_t n;
    reset(0); offer();
    make_reply(5, 120);
    n = incoming_length - 282;
    memcpy(opts, incoming + 282, n);
    /* Put all ACK options in file; main options only specify overload. */
    memset(incoming + 282, 0, n);
    incoming[282] = 52; incoming[283] = 1; incoming[284] = 1; incoming[285] = 255;
    memcpy(incoming + 150, opts, n);
    repair_checksums(); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND);
    reset(0); offer(); make_reply(5, 120);
    n = incoming_length - 282;
    memmove(incoming + 582, incoming + 282, n);
    memset(incoming + 282, 0, 300); /* Many PADs: options index must be 16-bit. */
    incoming_length += 300;
    word_put(incoming + 16, incoming_length - 14);
    word_put(incoming + 38, incoming_length - 34);
    repair_checksums(); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND);
}
static void test_nak_timeout_and_io(void)
{
    uint8_t old_xid[4];
    unsigned i;
    reset(64000); offer(); memcpy(old_xid, transaction_id, 4);
    make_reply(6, 120); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_SELECTING && !rrnet_ip65_ipv4[0]);
    advance(60);
    assert(*find_option(sent, 53) == 1 && memcmp(old_xid, transaction_id, 4));
    /* Irrelevant traffic must not extend the overall acquisition deadline. */
    for (i = 0; i < 60; ++i) { make_reply(5, 120); advance(60); }
    assert(rrnet_dhcp_state == RR_DHCP_TIMEOUT && !rrnet_dhcp_lease);
    reset(0); fail_send = 1; advance(300);
    assert(rrnet_dhcp_state == RR_DHCP_IO_ERROR);
}
static void arp_reply(void)
{
    memset(incoming, 0, 42);
    memcpy(incoming, rrnet_ip65_mac, 6); memcpy(incoming + 6, router_mac, 6);
    incoming[12] = 8; incoming[13] = 6;
    incoming[15] = 1; incoming[16] = 8; incoming[18] = 6; incoming[19] = 4;
    incoming[21] = 2;
    memcpy(incoming + 22, router_mac, 6); memcpy(incoming + 28, gateway, 4);
    memcpy(incoming + 32, rrnet_ip65_mac, 6); memcpy(incoming + 38, address, 4);
    incoming_length = 42;
}
static void test_renew_rebind_expiry(void)
{
    reset(65000); bind_lease(120);
    advance(59 * 60); assert(rrnet_dhcp_state == RR_DHCP_BOUND);
    advance(60); assert(rrnet_dhcp_state == RR_DHCP_RENEWING);
    assert(sent[13] == 6); /* ARP before unicast DHCPREQUEST. */
    arp_reply(); rrnet_dhcp_poll(++now);
    assert(sent[13] == 0 && *find_option(sent, 53) == 3);
    assert(!memcmp(sent, router_mac, 6));
    assert(!memcmp(sent + 26, address, 4) && !memcmp(sent + 30, gateway, 4));
    assert(!memcmp(sent + 54, address, 4) && !sent[52]);
    assert(!find_option(sent, 50) && !find_option(sent, 54));
    make_reply(5, 240); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND && rrnet_dhcp_lease == 240);
    assert(!memcmp(rrnet_dhcp_dns, (uint8_t[]){1,1,1,1}, 4));
    advance(210 * 60);
    assert(rrnet_dhcp_state == RR_DHCP_REBINDING);
    assert(sent[0] == 255 && sent[30] == 255 && sent[52] == 0x80);
    make_reply(5, 120); find_option(incoming, 54)[3] = 2;
    repair_checksums(); rrnet_dhcp_poll(++now);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND && rrnet_dhcp_server[3] == 2);
    advance(120 * 60);
    assert(rrnet_dhcp_state == RR_DHCP_SELECTING);
    assert(!rrnet_ip65_ipv4[0] && !rrnet_dhcp_dns[0]);
    assert(*find_option(sent, 53) == 1);
    reset(0); bind_lease(0xffffffffUL);
    advance(60000);
    assert(rrnet_dhcp_state == RR_DHCP_BOUND);
}
static void test_backend(void)
{
    const struct net_backend *backend = rrnet_backend();
    reset(0); check_speed = 1;
    assert(backend->init() == NET_PENDING && speed == 1);
    assert(backend->state() == NET_INITIALIZING);
    backend->poll(); assert(speed == 1);
    make_reply(2, 120); backend->poll();
    assert(strstr(backend->status, "requesting") != NULL);
    make_reply(5, 120); backend->poll();
    assert(backend->state() == NET_CLOSED && speed == 1);
    assert(strstr(backend->status, "192.168.68.61") != NULL);
    assert(strstr(backend->status, "1.1.1.1") != NULL);
    assert(backend->connect("example", 23) == NET_PENDING);
    backend->close();
    assert(backend->init() == NET_PENDING);
    now += 3600; backend->poll();
    assert(backend->state() == NET_FAILED && strstr(backend->status, "timed out"));
    chip_error = RRNET_IP65_NO_CHIP;
    assert(backend->init() == NET_NO_DEVICE && speed == 1);
    assert(strstr(backend->status, "not detected") != NULL);
    check_speed = 0;
}
int main(void)
{
    test_acquisition(); test_reject_bad_packets();
    test_option_overload_and_long_options(); test_nak_timeout_and_io();
    test_renew_rebind_expiry(); test_backend();
    puts("RR-Net DHCP acquisition, validation, renewal and backend tests passed");
    return 0;
}
