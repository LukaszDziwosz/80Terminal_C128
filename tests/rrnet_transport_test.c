#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "network.h"
#include "ip65_ip.h"
#include "ip65_dns.h"
#include "ip65_tcp.h"
#include "ip65_dhcp.h"
#include "ip65_arp.h"
#include "ip65_ethernet.h"

#ifdef __OSCAR64C__
#pragma heapsize(0)
#pragma stacksize(2048)
#endif

uint8_t rrnet_ip65_mac[6] = {2,0x55,0x54,0x80,0,1};
static uint8_t wire_in[1518], wire_out[1518];
static uint16_t input_size, output_size, ticks, client_port;
static uint8_t send_error, speed, check_speed;
static unsigned packets;
static uint8_t repeated_frames;
/* volatile: Oscar64 -O2 otherwise caches this across the indirect b->poll(). */
static volatile unsigned slow_calls;
static uint32_t client_seq, server_seq;
static const uint8_t own_ip[4] = {192,168,68,62};
static const uint8_t gateway[4] = {192,168,68,1};
static const uint8_t peer[4] = {203,0,113,42};
static const uint8_t mac[6] = {0x10,0x20,0x30,0x40,0x50,0x60};
uint8_t platform_slow(void) { uint8_t old = speed; speed = 0; ++slow_calls; return old; }
void platform_restore_speed(uint8_t s) { speed = s; }
uint16_t platform_ticks(void) { return ticks; }
uint8_t rrnet_ip65_init(void) { assert(!speed); return 0; }
void rrnet_ip65_shutdown(void) { }
uint16_t rrnet_ip65_receive(uint8_t *p, uint16_t capacity)
{
    uint16_t n = input_size;
    if (check_speed) assert(!speed);
    assert(n <= capacity); memcpy(p, wire_in, n);
    if (!repeated_frames || !--repeated_frames) input_size = 0;
    return n;
}
uint8_t rrnet_ip65_send(const uint8_t *p, uint16_t n)
{
    if (check_speed) assert(!speed);
    assert(n <= sizeof(wire_out)); memcpy(wire_out, p, n); output_size = n; ++packets;
    return send_error;
}
/* Independent wire checksum implementation, including carry folding. */
static uint16_t w16(const uint8_t *p) { return (uint16_t)p[0] * 256 + p[1]; }
static uint32_t w32(const uint8_t *p) { return (uint32_t)w16(p) * 65536UL + w16(p + 2); }
static void p16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
static void p32(uint8_t *p, uint32_t n) { p16(p, (uint16_t)(n >> 16)); p16(p + 2, (uint16_t)n); }
static uint16_t checksum(const uint8_t *p, uint16_t n, uint32_t sum)
{
    while (n > 1) { sum += w16(p); p += 2; n -= 2; }
    if (n) sum += (uint16_t)*p << 8;
    while (sum >> 16) sum = (sum & 65535UL) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint16_t transport_checksum(const uint8_t *frame)
{
    uint16_t n = w16(frame + 16) - 20;
    return checksum(frame + 34, n, (uint32_t)w16(frame + 26) + w16(frame + 28) +
                    w16(frame + 30) + w16(frame + 32) + frame[23] + n);
}
static void ip_packet(uint8_t protocol, uint16_t size, const uint8_t *source)
{
    memset(wire_in, 0, sizeof(wire_in));
    memcpy(wire_in, rrnet_ip65_mac, 6); memcpy(wire_in + 6, mac, 6);
    wire_in[12] = 8; wire_in[14] = 0x45; wire_in[22] = 64; wire_in[23] = protocol;
    memcpy(wire_in + 26, source, 4); memcpy(wire_in + 30, own_ip, 4);
    p16(wire_in + 16, 20 + size); input_size = 34 + size;
    p16(wire_in + 24, checksum(wire_in + 14, 20, 0));
}
static void tcp_packet(uint32_t seq, uint32_t ack, uint8_t flags,
                       const uint8_t *data, uint16_t size, uint16_t window)
{
    uint16_t header = flags & 2 ? 24 : 20;
    ip_packet(6, header + size, peer);
    p16(wire_in + 34, 23); p16(wire_in + 36, client_port);
    p32(wire_in + 38, seq); p32(wire_in + 42, ack);
    wire_in[46] = (uint8_t)(header << 2); wire_in[47] = flags; p16(wire_in + 48, window);
    if (flags & 2) { wire_in[54] = 2; wire_in[55] = 4; p16(wire_in + 56, 128); }
    if (size) memcpy(wire_in + 34 + header, data, size);
    p16(wire_in + 50, transport_checksum(wire_in));
}
static void feed_tcp(void)
{
    assert(rrnet_ip65_eth_poll()); assert(rr_ip_parse());
    rr_tcp_input(ticks); rr_tcp_poll(ticks);
}
static void arp_reply(void)
{
    uint8_t *in = rrnet_ip65_eth_in;
    memset(in, 0, 42);
    memcpy(in, rrnet_ip65_mac, 6); memcpy(in + 6, mac, 6);
    in[12] = 8; in[13] = 6; in[15] = 1; in[16] = 8;
    in[18] = 6; in[19] = 4; in[21] = 2;
    memcpy(in + 22, mac, 6); memcpy(in + 28, gateway, 4);
    memcpy(in + 32, rrnet_ip65_mac, 6); memcpy(in + 38, own_ip, 4);
    rrnet_ip65_eth_in_length = 42; rrnet_ip65_arp_process();
}
static void configure(void)
{
    rrnet_ip65_arp_set_config(own_ip, (uint8_t[]){255,255,252,0}, gateway);
    memset(rrnet_dhcp_dns, 1, 4); rrnet_ip65_arp_init();
    rr_dns_cancel(); rr_tcp_init(); ticks = 65000; speed = 1;
    packets = 0; input_size = output_size = 0; send_error = check_speed = 0;
    repeated_frames = 0; slow_calls = 0;
}
static void begin_tcp(void)
{
    configure(); assert(rr_tcp_connect(peer, 23, ticks) == NET_PENDING);
    rr_tcp_poll(ticks);
    assert(wire_out[13] == 6 && !memcmp(wire_out + 38, gateway, 4));
    arp_reply(); rr_tcp_poll(++ticks);
    assert(wire_out[23] == 6 && wire_out[47] == 2 && output_size == 58);
    assert(checksum(wire_out + 14, 20, 0) == 0 && transport_checksum(wire_out) == 0);
    assert(w16(wire_out + 56) == RR_TCP_MSS);
    client_port = w16(wire_out + 34); client_seq = w32(wire_out + 38) + 1;
    server_seq = 0xfffffff1UL;
}
static void establish(uint16_t window)
{
    tcp_packet(server_seq - 1, client_seq, 0x12, NULL, 0, window); feed_tcp();
    assert(rr_tcp_state() == NET_CONNECTED);
    assert(wire_out[47] == 0x10 && w32(wire_out + 42) == server_seq);
}
static void test_handshake_and_failures(void)
{
    uint32_t isn;
    unsigned count;
    begin_tcp(); isn = w32(wire_out + 38); count = packets;
    ticks += 60; rr_tcp_poll(ticks);
    assert(packets == count + 1 && w32(wire_out + 38) == isn && wire_out[47] == 2);
    tcp_packet(server_seq - 1, client_seq + 1, 0x12, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_state() == NET_CONNECTING);
    establish(4096);
    /* Lost final handshake ACK: duplicate SYN/ACK gets another ACK. */
    count = packets; tcp_packet(server_seq - 1, client_seq, 0x12, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_state() == NET_CONNECTED && packets == count + 1);
    begin_tcp(); tcp_packet(0, client_seq, 0x14, NULL, 0, 0); feed_tcp();
    assert(rr_tcp_state() == NET_FAILED && rr_tcp_error == RR_TCP_REFUSED);
    begin_tcp(); ticks += 1800; rr_tcp_poll(ticks);
    assert(rr_tcp_state() == NET_FAILED && rr_tcp_error == RR_TCP_TIMEOUT);
    begin_tcp(); rr_tcp_close(ticks);
    assert(rr_tcp_state() == NET_CLOSED && wire_out[47] == 4);
}
static void test_receive_and_eof(void)
{
    uint8_t readback[32];
    const uint8_t greeting[] = {'B','B','S',0,'!'};
    uint32_t start;
    begin_tcp(); establish(4096); start = server_seq;
    tcp_packet(start, client_seq, 0x18, greeting, 5, 4096); feed_tcp(); server_seq += 5;
    assert(w32(wire_out + 42) == server_seq);
    assert(rr_tcp_read(readback, 2) == 2 && !memcmp(readback, greeting, 2));
    tcp_packet(start, client_seq, 0x18, greeting, 5, 4096); feed_tcp();
    assert(rr_tcp_read(readback, 32) == 3 && !memcmp(readback, greeting + 2, 3));
    assert(rr_tcp_read(readback, 32) == 0);
    tcp_packet(server_seq + 5, client_seq, 0x18, greeting, 5, 4096); feed_tcp();
    assert(w32(wire_out + 42) == server_seq && rr_tcp_read(readback, 32) == 0);
    /* Overlapping retransmission only delivers bytes after RCV.NXT. */
    tcp_packet(server_seq - 2, client_seq, 0x18, greeting, 5, 4096); feed_tcp(); server_seq += 3;
    assert(rr_tcp_read(readback, 32) == 3 && !memcmp(readback, greeting + 2, 3));
    tcp_packet(server_seq, client_seq, 0x19, greeting, 5, 4096); feed_tcp(); server_seq += 6;
    assert(rr_tcp_state() == NET_CONNECTED && w32(wire_out + 42) == server_seq);
    assert(rr_tcp_read(readback, 2) == 2 && rr_tcp_state() == NET_CONNECTED);
    assert(rr_tcp_read(readback, 32) == 3 && rr_tcp_state() == NET_EOF);
    rr_tcp_close(ticks); rr_tcp_poll(ticks);
    assert(rr_tcp_state() == NET_CLOSING && wire_out[47] == 0x11);
    tcp_packet(server_seq, client_seq + 1, 0x10, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_state() == NET_CLOSED);
}
static void test_window_and_bad_input(void)
{
    uint8_t data[512], out[512];
    uint8_t i;
    unsigned count;
    memset(data, 0x55, sizeof(data)); begin_tcp(); establish(4096);
    for (i = 0; i < 4; ++i) {
        tcp_packet(server_seq, client_seq, 0x18, data, sizeof(data), 4096); feed_tcp();
        server_seq += sizeof(data);
    }
    assert(w16(wire_out + 48) == 0);
    tcp_packet(server_seq, client_seq, 0x18, data, 1, 4096); feed_tcp();
    assert(w32(wire_out + 42) == server_seq);
    count = packets;
    assert(rr_tcp_read(out, 512) == 512); rr_tcp_poll(ticks);
    assert(packets == count); /* Do not reopen in sub-MSS increments. */
    assert(rr_tcp_read(out, 128) == 128); rr_tcp_poll(ticks);
    assert(w16(wire_out + 48) == 640);
    for (i = 0; i < 2; ++i) assert(rr_tcp_read(out, 512) == 512);
    assert(rr_tcp_read(out, 512) == 384);
    assert(rr_tcp_stats.peak == 2048 && rr_tcp_stats.zero_windows > 0);
    tcp_packet(server_seq, client_seq, 0x18, data, 1, 4096); feed_tcp(); ++server_seq;
    assert(rr_tcp_read(out, 512) == 1 && out[0] == 0x55);
    count = packets;
    tcp_packet(server_seq, client_seq, 0x18, data, 1, 4096); wire_in[54] ^= 1; feed_tcp();
    assert(rr_tcp_read(out, 512) == 0);
    tcp_packet(server_seq, client_seq, 0x18, data, 1, 4096);
    wire_in[46] = 0xf0; p16(wire_in + 50, 0); p16(wire_in + 50, transport_checksum(wire_in)); feed_tcp();
    assert(rr_tcp_read(out, 512) == 0);
    tcp_packet(server_seq + 1, client_seq, 0x14, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_state() == NET_CONNECTED);
    (void)count;
    tcp_packet(server_seq, client_seq, 0x14, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_state() == NET_FAILED && rr_tcp_error == RR_TCP_RESET);
}
static void test_send_retransmit_and_close(void)
{
    uint8_t data[200];
    uint16_t i;
    uint32_t seq;
    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
    begin_tcp(); establish(4096);
    assert(rr_tcp_write(data, 200) == 200 && rr_tcp_write(data, 1) == 0);
    rr_tcp_poll(ticks); seq = w32(wire_out + 38);
    assert(output_size == 54 + 128 && !memcmp(wire_out + 54, data, 128));
    ticks += 60; rr_tcp_poll(ticks);
    assert(w32(wire_out + 38) == seq && !memcmp(wire_out + 54, data, 128));
    tcp_packet(server_seq, client_seq + 300, 0x10, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_write(data, 1) == 0); /* Future ACK cannot free data. */
    tcp_packet(server_seq, client_seq + 64, 0x10, NULL, 0, 4096); feed_tcp();
    ticks += 120; rr_tcp_poll(ticks);
    assert(w32(wire_out + 38) == seq + 64 && output_size == 54 + 64);
    assert(!memcmp(wire_out + 54, data + 64, 64));
    tcp_packet(server_seq, client_seq + 128, 0x10, NULL, 0, 4096); feed_tcp();
    assert(output_size == 54 + 72 && !memcmp(wire_out + 54, data + 128, 72));
    rr_tcp_close(ticks); assert(rr_tcp_state() == NET_CLOSING);
    tcp_packet(server_seq, client_seq + 200, 0x10, NULL, 0, 4096); feed_tcp();
    assert(wire_out[47] == 0x11 && w32(wire_out + 38) == client_seq + 200);
    tcp_packet(server_seq, client_seq + 201, 0x11, NULL, 0, 4096); feed_tcp();
    assert(rr_tcp_state() == NET_CLOSED && w32(wire_out + 42) == server_seq + 1);
    tcp_packet(server_seq, client_seq + 201, 0x11, NULL, 0, 4096); feed_tcp();
    assert(wire_out[47] == 0x10 && rr_tcp_state() == NET_CLOSED);
    assert(rr_tcp_connect(peer, 23, ticks) == NET_PENDING); rr_tcp_poll(ticks);
    assert(w16(wire_out + 34) != client_port);
}
static void test_render_batch_ack_cost(void)
{
    uint8_t data[RR_TCP_MSS], out[128];
    uint8_t i;
    unsigned count;
    memset(data, 'x', sizeof(data)); begin_tcp(); establish(4096);
    tcp_packet(server_seq, client_seq, 0x18, data, sizeof(data), 4096); feed_tcp();
    count = packets;
    for (i = 0; i < 4; ++i) {
        assert(rr_tcp_read(out, sizeof(out)) == 128);
        rr_tcp_poll(ticks);
        assert(packets == count);
    }
    assert(rr_tcp_read(out, sizeof(out)) == 24); rr_tcp_poll(ticks);
    assert(packets == count + 1 && w16(wire_out + 48) == RR_TCP_RX_CAPACITY);
    puts("536-byte render replay: 1 window-update ACK for 5 application reads");
}
static void test_zero_window_and_close_timeout(void)
{
    uint8_t data[] = {'a','b','c'};
    begin_tcp(); establish(0); assert(rr_tcp_write(data, 3) == 3);
    ticks += 60; rr_tcp_poll(ticks);
    assert(w32(wire_out + 38) == client_seq - 1 && output_size == 55);
    tcp_packet(server_seq, client_seq, 0x10, NULL, 0, 3); feed_tcp();
    assert(w32(wire_out + 38) == client_seq && output_size == 57);
    assert(!memcmp(wire_out + 54, data, 3));
    tcp_packet(server_seq, client_seq + 3, 0x10, NULL, 0, 4096); feed_tcp();
    rr_tcp_close(ticks); rr_tcp_poll(ticks); ticks += 900; rr_tcp_poll(ticks);
    assert(rr_tcp_state() == NET_CLOSED);
}
static void dns_response(uint8_t cname, uint8_t rcode)
{
    uint16_t query_size = output_size - 42;
    uint16_t pos = query_size, udp_port = w16(wire_out + 34);
    uint8_t *dns;
    ip_packet(17, 8 + query_size + (cname ? 34 : 16), (uint8_t[]){1,1,1,1});
    dns = wire_in + 42; memcpy(dns, wire_out + 42, query_size);
    dns[2] = 0x81; dns[3] = 0x80 | rcode; p16(dns + 6, cname ? 2 : 1);
    if (cname) {
        /* The CNAME is 'alias.' + the original question, using compression.
         * The A owner in the next RR points at the CNAME's RDATA. */
        dns[pos++] = 0xc0; dns[pos++] = 12;
        p16(dns + pos, 5); pos += 2; p16(dns + pos, 1); pos += 2;
        p32(dns + pos, 60); pos += 4; p16(dns + pos, 8); pos += 2;
        dns[pos++] = 5; memcpy(dns + pos, "alias", 5); pos += 5;
        dns[pos++] = 0xc0; dns[pos++] = 12;
        dns[pos++] = 0xc0; dns[pos++] = (uint8_t)(query_size + 12);
    } else { dns[pos++] = 0xc0; dns[pos++] = 12; }
    p16(dns + pos, 1); pos += 2; p16(dns + pos, 1); pos += 2;
    p32(dns + pos, 60); pos += 4; p16(dns + pos, 4); pos += 2;
    memcpy(dns + pos, peer, 4); pos += 4;
    input_size = pos + 42; p16(wire_in + 16, input_size - 14);
    p16(wire_in + 24, 0); p16(wire_in + 24, checksum(wire_in + 14, 20, 0));
    p16(wire_in + 34, 53); p16(wire_in + 36, udp_port); p16(wire_in + 38, pos + 8);
    p16(wire_in + 40, transport_checksum(wire_in));
}
static void feed_dns(void)
{
    assert(rrnet_ip65_eth_poll()); assert(rr_ip_parse()); assert(rr_udp_parse()); rr_dns_input(ticks);
}
static void start_dns(void)
{
    configure(); arp_reply();
    assert(rr_dns_start("Bbs.Example.", ticks) == NET_PENDING); rr_dns_poll(ticks);
    assert(w16(wire_out + 36) == 53 && !memcmp(wire_out + 30, (uint8_t[]){1,1,1,1}, 4));
    assert(transport_checksum(wire_out) == 0);
    assert(!memcmp(wire_out + 54, "\003bbs\007example\000\000\001\000\001", 17));
}
static void test_dns(void)
{
    uint16_t saved_port;
    unsigned count;
    configure(); assert(rr_dns_start("192.168.68.1", ticks) == NET_OK);
    assert(rr_dns_state == RR_DNS_DONE && !memcmp(rr_dns_address, gateway, 4));
    assert(rr_dns_start("bad..name", ticks) == NET_BAD_ARGUMENT);
    assert(rr_dns_start("-bad.name", ticks) == NET_BAD_ARGUMENT);
    start_dns(); dns_response(0, 0); feed_dns();
    assert(rr_dns_state == RR_DNS_DONE && !memcmp(rr_dns_address, peer, 4));
    start_dns(); dns_response(1, 0); feed_dns();
    assert(rr_dns_state == RR_DNS_DONE && !memcmp(rr_dns_address, peer, 4));
    start_dns(); dns_response(0, 3); feed_dns(); assert(rr_dns_state == RR_DNS_ERROR);
    start_dns(); dns_response(0, 0); wire_in[42] ^= 1;
    p16(wire_in + 40, 0); p16(wire_in + 40, transport_checksum(wire_in)); feed_dns();
    assert(rr_dns_state == RR_DNS_WAIT);
    dns_response(0, 0); saved_port = w16(wire_in + 36); p16(wire_in + 36, saved_port + 1);
    p16(wire_in + 40, 0); p16(wire_in + 40, transport_checksum(wire_in)); feed_dns();
    assert(rr_dns_state == RR_DNS_WAIT);
    /* Self-referential compression pointer must terminate without reading
     * beyond the frame or accepting an answer. */
    dns_response(0, 0); wire_in[54] = 0xc0; wire_in[55] = 12;
    p16(wire_in + 40, 0); p16(wire_in + 40, transport_checksum(wire_in)); feed_dns();
    assert(rr_dns_state == RR_DNS_WAIT);
    count = packets; ticks += 120; rr_dns_poll(ticks); assert(packets == count + 1);
    ticks += 1200; rr_dns_poll(ticks); assert(rr_dns_state == RR_DNS_TIMEOUT);
}
static void test_ip_validation(void)
{
    configure(); ip_packet(17, 8, peer); p16(wire_in + 38, 8);
    memcpy(rrnet_ip65_eth_in, wire_in, input_size); rrnet_ip65_eth_in_length = input_size;
    assert(rr_ip_parse() && rr_udp_parse());
    rrnet_ip65_eth_in_length = 33; assert(!rr_ip_parse());
    rrnet_ip65_eth_in_length = input_size; rrnet_ip65_eth_in[14] = 0x4f;
    assert(!rr_ip_parse());
    memcpy(rrnet_ip65_eth_in, wire_in, input_size); rrnet_ip65_eth_in[20] = 0x20;
    assert(!rr_ip_parse());
    memcpy(rrnet_ip65_eth_in, wire_in, input_size); rrnet_ip65_eth_in[24] ^= 1;
    assert(!rr_ip_parse());
}
static void test_backend_tcp_and_renewal(void)
{
    const struct net_backend *b = rrnet_backend();
    configure(); check_speed = 1; assert(b->init() == NET_PENDING && speed == 1);
    /* Install a lease; DORA itself is covered by rrnet_dhcp_test.c. */
    rrnet_ip65_arp_set_config(own_ip, (uint8_t[]){255,255,252,0}, gateway);
    memcpy(rrnet_dhcp_server, gateway, 4); memset(rrnet_dhcp_dns, 1, 4);
    rrnet_dhcp_state = RR_DHCP_BOUND; rrnet_dhcp_lease = 0xffffffffUL;
    b->poll(); assert(b->state() == NET_CLOSED && speed == 1);
    arp_reply(); assert(b->connect("203.0.113.42", 23) == NET_PENDING); b->poll();
    client_port = w16(wire_out + 34); client_seq = w32(wire_out + 38) + 1; server_seq = 100;
    tcp_packet(server_seq - 1, client_seq, 0x12, NULL, 0, 4096); b->poll();
    assert(b->state() == NET_CONNECTED && speed == 1);
    rrnet_dhcp_state = RR_DHCP_RENEWING; b->poll();
    assert(b->state() == NET_CONNECTED);
    rrnet_dhcp_state = RR_DHCP_BOUND; b->poll();
    assert(b->state() == NET_CONNECTED);
    /* Eight queued copies: one poll must consume only four, ACK promptly,
     * restore speed, and deliver the payload exactly once. */
    tcp_packet(server_seq, client_seq, 0x18, (const uint8_t *)"abc", 3, 4096);
    repeated_frames = 8;
    {
        unsigned calls = slow_calls;
        uint8_t data[8];
        b->poll();
        assert(repeated_frames == 4 && speed == 1);
        assert(slow_calls == calls + 8); /* Four RX and four ACK transfers. */
        assert(b->read(data, sizeof(data)) == 3 && !memcmp(data, "abc", 3));
        b->poll();
        assert(repeated_frames == 0 && b->read(data, sizeof(data)) == 0);
        assert(rr_tcp_stats.duplicates == 7);
    }
    server_seq += 3;
    tcp_packet(server_seq, client_seq, 0x19, (const uint8_t *)"bye", 3, 4096); b->poll();
    assert(b->state() == NET_CONNECTED);
    {
        uint8_t data[8]; assert(b->read(data, sizeof(data)) == 3 && !memcmp(data, "bye", 3));
    }
    b->poll(); assert(b->state() == NET_EOF); b->close(); b->poll();
    tcp_packet(server_seq + 4, client_seq + 1, 0x10, NULL, 0, 4096); b->poll();
    assert(b->state() == NET_CLOSED && speed == 1);
    check_speed = 0;
}
int main(void)
{
    test_handshake_and_failures(); test_receive_and_eof(); test_window_and_bad_input();
    test_send_retransmit_and_close(); test_zero_window_and_close_timeout();
    test_render_batch_ack_cost();
    test_dns(); test_ip_validation(); test_backend_tcp_and_renewal();
    puts("RR-Net IPv4, DNS, TCP stream and backend tests passed");
    return 0;
}
