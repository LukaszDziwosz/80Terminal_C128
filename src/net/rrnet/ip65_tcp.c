/* Single active-open TCP stream for RR-Net. One segment in flight, bounded
 * buffers, cumulative ACKs, conservative MSS and exponential retransmission.
 * Wire format/state transitions follow RFC 9293; no listening sockets.
 */
#include <string.h>
#include "network.h"
#include "ip65_tcp.h"
#include "ip65_ip.h"
#include "ip65_ethernet.h"
#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif
#define FIN 1U
#define SYN 2U
#define RST 4U
#define PSH 8U
#define ACK 16U
enum tcp_phase { CLOSED, SYN_SENT, ESTABLISHED, CLOSE_WAIT, FIN_WAIT_1,
                 FIN_WAIT_2, CLOSING, LAST_ACK, TIME_WAIT, FAILED };
static uint8_t phase, ack_pending, local_close, retries, flight_flags, transmitted;
static uint8_t peer[4], tx[RR_TCP_MSS], rx[RR_TCP_RX_CAPACITY];
static uint16_t peer_port, local_port, peer_window, peer_mss;
static uint16_t queued, flight, rx_head, rx_count;
static uint16_t deadline, opened, closing_at, rto, probe_rto, probe_at, time_wait_at;
static uint32_t snd_una, snd_nxt, rcv_nxt, window_seq, window_ack;
static uint16_t port_serial = 49152;
static uint32_t isn_serial = 0x12800000UL;
uint8_t rr_tcp_error;
struct rr_tcp_counters rr_tcp_stats;
static uint16_t freed_since_ack;

static uint8_t before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static void fail(uint8_t error) { rr_tcp_error = error; phase = FAILED; }
void rr_tcp_abort(void) { phase = CLOSED; queued = flight = rx_count = 0; ack_pending = local_close = 0; }
void rr_tcp_init(void) {
    rr_tcp_abort(); rr_tcp_error = RR_TCP_NO_ERROR;
    memset(&rr_tcp_stats, 0, sizeof(rr_tcp_stats)); freed_since_ack = 0;
}
int rr_tcp_state(void)
{
    if (phase == FAILED) return NET_FAILED;
    if (local_close && phase != CLOSED && phase != TIME_WAIT) return NET_CLOSING;
    if (phase == SYN_SENT) return NET_CONNECTING;
    if (phase == CLOSE_WAIT) return rx_count ? NET_CONNECTED : NET_EOF;
    if (phase == ESTABLISHED) return NET_CONNECTED;
    if (phase == CLOSED || phase == TIME_WAIT) return NET_CLOSED;
    return NET_CLOSING;
}
int rr_tcp_connect(const uint8_t *ip, uint16_t port, uint16_t ticks)
{
    if (phase != CLOSED && phase != TIME_WAIT && phase != FAILED) return NET_BAD_ARGUMENT;
    if (!port || !ip[0] || ip[0] >= 224 || ip[0] == 127) return NET_BAD_ARGUMENT;
    rr_tcp_init(); memcpy(peer, ip, 4); peer_port = port;
    if (++port_serial < 49152) port_serial = 49152;
    local_port = port_serial;
    isn_serial += 64001UL + ((uint32_t)ticks << 8);
    snd_una = snd_nxt = isn_serial; rcv_nxt = 0;
    window_seq = window_ack = 0;
    peer_mss = RR_TCP_MSS; peer_window = 65535;
    flight_flags = SYN; flight = 1; transmitted = retries = 0;
    rx_head = 0; phase = SYN_SENT; opened = deadline = ticks;
    rto = probe_rto = 60; probe_at = ticks + 60;
    return NET_PENDING;
}
/* Ethernet helpers switch to 1 MHz only for the hardware transfer. */
static uint8_t send_segment(uint8_t flags, uint32_t seq, const uint8_t *data,
                             uint16_t size, uint16_t ticks)
{
    uint8_t *out = rrnet_ip65_eth_out + 34;
    uint16_t header = flags & SYN ? 24 : 20;
    uint8_t result = rr_ip_prepare(peer, 6, header + size, ticks);
    if (result) return result;
    memset(out, 0, header);
    rr_put16(out, local_port); rr_put16(out + 2, peer_port);
    rr_put32(out + 4, seq); rr_put32(out + 8, flags & ACK ? rcv_nxt : 0);
    out[12] = (uint8_t)(header << 2); out[13] = flags;
    rr_put16(out + 14, local_close ? RR_TCP_RX_CAPACITY : RR_TCP_RX_CAPACITY - rx_count);
    if (flags & SYN) { out[20] = 2; out[21] = 4; rr_put16(out + 22, RR_TCP_MSS); }
    if (size) memcpy(out + header, data, size);
    rr_put16(out + 16, (uint16_t)~rr_transport_sum(rrnet_ip65_eth_out + 14, out, header + size));
    result = rrnet_ip65_eth_send();
    if (!result && (flags & ACK)) {
        freed_since_ack = 0;
        if (!size && flags == ACK) ++rr_tcp_stats.acks;
        if (!local_close && rx_count == RR_TCP_RX_CAPACITY) ++rr_tcp_stats.zero_windows;
    }
    return result;
}
static uint8_t syn_options(const uint8_t *p, uint16_t length)
{
    uint8_t code, n;
    while (length) {
        code = *p++; --length;
        if (!code) return 1;
        if (code == 1) continue;
        if (!length) return 0;
        n = *p++; --length;
        if (n < 2 || n - 2 > length) return 0;
        if (code == 2) {
            if (n != 4 || !rr_get16(p)) return 0;
            peer_mss = rr_get16(p);
            if (peer_mss > RR_TCP_MSS) peer_mss = RR_TCP_MSS;
        }
        p += n - 2; length -= n - 2;
    }
    return 1;
}
static void time_wait(uint16_t ticks)
{
    phase = TIME_WAIT; time_wait_at = ticks; queued = flight = 0;
}
static void receive_bytes(const uint8_t *data, uint16_t size)
{
    uint16_t tail = (rx_head + rx_count) & (RR_TCP_RX_CAPACITY - 1);
    uint16_t first = RR_TCP_RX_CAPACITY - tail;
    if (first > size) first = size;
    memcpy(rx + tail, data, first);
    memcpy(rx, data + first, size - first);
    rx_count += size;
    if (rx_count > rr_tcp_stats.peak) rr_tcp_stats.peak = rx_count;
}
void rr_tcp_input(uint16_t ticks)
{
    const uint8_t *in = rr_ip_payload;
    uint32_t seq, ack, end, fin_at;
    uint16_t header, size, trim, space, progress;
    uint8_t flags;
    if (phase == CLOSED || phase == FAILED || rr_ip_protocol != 6 || !rr_ip_local() ||
        memcmp(rr_ip_header + 12, peer, 4) || rr_ip_length < 20 ||
        rr_get16(in) != peer_port || rr_get16(in + 2) != local_port ||
        rr_transport_sum(rr_ip_header, in, rr_ip_length) != 0xffff) return;
    header = (uint16_t)(in[12] >> 4) * 4;
    if (header < 20 || header > rr_ip_length) return;
    flags = in[13]; seq = rr_get32(in + 4); ack = rr_get32(in + 8);
    size = rr_ip_length - header; fin_at = seq + size;
    if (phase == SYN_SENT) {
        if (!(flags & ACK) || !transmitted || ack != snd_nxt) return;
        if (flags & RST) { fail(RR_TCP_REFUSED); return; }
        if (!(flags & SYN) || (flags & FIN) || !syn_options(in + 20, header - 20)) return;
        snd_una = ack; flight = 0; transmitted = retries = 0;
        rcv_nxt = seq + 1; peer_window = rr_get16(in + 14);
        window_seq = seq; window_ack = ack;
        phase = ESTABLISHED; ack_pending = 1; rto = 60;
        ++seq; fin_at = seq + size;
        flags &= (uint8_t)~SYN;
    }
    space = local_close ? RR_TCP_RX_CAPACITY : RR_TCP_RX_CAPACITY - rx_count;
    if (size && !(flags & (RST | SYN))) {
        if (before(rcv_nxt, seq)) ++rr_tcp_stats.gaps;
        else if (before(seq, rcv_nxt)) ++rr_tcp_stats.duplicates;
    }
    end = seq + size + ((flags & FIN) != 0) + ((flags & SYN) != 0);
    /* Reject segments outside the receive window; zero-window probes elicit
     * an ACK, and in-window RSTs must match RCV.NXT exactly. */
    if (seq != rcv_nxt && (!space || !before(seq, rcv_nxt + space) || !before(rcv_nxt, end))) {
        if (!(flags & RST)) ack_pending = 1;
        return;
    }
    if (flags & RST) {
        if (seq == rcv_nxt) fail(RR_TCP_RESET);
        else ack_pending = 1;
        return;
    }
    if (flags & SYN) { ack_pending = 1; return; }
    if (!(flags & ACK)) return;
    if (before(snd_nxt, ack)) { ack_pending = 1; return; }
    if (!before(ack, snd_una)) {
        if (before(window_seq, seq) || (window_seq == seq && !before(ack, window_ack))) {
            peer_window = rr_get16(in + 14); window_seq = seq; window_ack = ack;
        }
        if (before(snd_una, ack)) {
            progress = (uint16_t)(ack - snd_una);
            if (progress > flight || !transmitted) return;
            snd_una = ack; flight -= progress;
            if (!(flight_flags & (SYN | FIN))) {
                queued -= progress; memmove(tx, tx + progress, queued);
            }
            if (!flight) {
                transmitted = retries = 0; rto = 60;
                if (phase == FIN_WAIT_1) phase = FIN_WAIT_2;
                else if (phase == CLOSING) time_wait(ticks);
                else if (phase == LAST_ACK) { phase = CLOSED; return; }
            }
            deadline = ticks + rto;
        }
    }
    if (phase == TIME_WAIT) {
        if (flags & FIN) { ack_pending = 1; time_wait_at = ticks; }
        return;
    }
    if (seq != rcv_nxt && !before(seq, rcv_nxt)) { ack_pending = 1; return; }
    trim = before(seq, rcv_nxt) ? (uint16_t)(rcv_nxt - seq) : 0;
    if (trim > size) trim = size;
    size -= trim;
    if (size > space) size = space;
    if (phase != CLOSE_WAIT && phase != LAST_ACK && phase != CLOSING) {
        if (size) {
            if (!local_close) receive_bytes(in + header + trim, size);
            rcv_nxt += size; ack_pending = 1;
        }
        if ((flags & FIN) && fin_at == rcv_nxt && space > size) {
            ++rcv_nxt; ack_pending = 1;
            if (phase == ESTABLISHED) phase = CLOSE_WAIT;
            else if (phase == FIN_WAIT_1) phase = CLOSING;
            else if (phase == FIN_WAIT_2) time_wait(ticks);
        }
    }
    if ((flags & FIN) || trim) ack_pending = 1;
}
void rr_tcp_poll(uint16_t ticks)
{
    uint8_t result;
    uint16_t size;
    if (phase == CLOSED || phase == FAILED) return;
    if (phase == TIME_WAIT && (uint16_t)(ticks - time_wait_at) >= 7200U) {
        phase = CLOSED; return;
    }
    if (local_close && phase != TIME_WAIT && (uint16_t)(ticks - closing_at) >= 900U) {
        (void)send_segment(RST | ACK, snd_nxt, tx, 0, ticks);
        rr_tcp_abort(); return;
    }
    if (phase == SYN_SENT && (uint16_t)(ticks - opened) >= 1800U) { fail(RR_TCP_TIMEOUT); return; }
    if (ack_pending && phase != SYN_SENT) {
        result = send_segment(ACK, snd_nxt, tx, 0, ticks);
        if (!result) ack_pending = 0;
        else if (result == 1) { fail(RR_TCP_IO_ERROR); return; }
    }
    if (!flight && (phase == ESTABLISHED || phase == CLOSE_WAIT)) {
        if (queued && peer_window) {
            flight = queued < peer_window ? queued : peer_window;
            if (flight > peer_mss) flight = peer_mss;
            flight_flags = ACK | PSH; transmitted = retries = 0; deadline = ticks;
        } else if (!queued && local_close) {
            flight = 1; flight_flags = FIN | ACK; transmitted = retries = 0; deadline = ticks;
            phase = phase == CLOSE_WAIT ? LAST_ACK : FIN_WAIT_1;
        } else if (queued && !peer_window && (int16_t)(ticks - probe_at) >= 0) {
            /* Probe one already-acknowledged sequence position. Never count
             * this byte as new data or free application data for its ACK. */
            (void)send_segment(ACK, snd_una - 1, tx, 1, ticks);
            probe_at = ticks + probe_rto;
            if (probe_rto < 1800) probe_rto *= 2;
        }
    }
    if (!flight || (int16_t)(ticks - deadline) < 0) return;
    if (transmitted && retries >= 6) { fail(RR_TCP_TIMEOUT); return; }
    size = flight_flags & (SYN | FIN) ? 0 : flight;
    result = send_segment(flight_flags, snd_una, tx, size, ticks);
    if (result == 2) return;
    if (result) { fail(RR_TCP_IO_ERROR); return; }
    if (!transmitted) { snd_nxt = snd_una + flight; transmitted = 1; }
    else { ++retries; ++rr_tcp_stats.retries; if (rto < 1800) rto *= 2; }
    deadline = ticks + rto;
    ack_pending = 0;
}
int rr_tcp_write(const uint8_t *buffer, uint16_t length)
{
    if (phase != ESTABLISHED || local_close) return phase == CLOSE_WAIT ? 0 : NET_IO_ERROR;
    if (queued || !length) return 0;
    if (length > RR_TCP_MSS) length = RR_TCP_MSS;
    memcpy(tx, buffer, length); queued = length;
    return (int)length;
}
int rr_tcp_read(uint8_t *buffer, uint16_t capacity)
{
    uint16_t n = rx_count < capacity ? rx_count : capacity;
    uint16_t first = RR_TCP_RX_CAPACITY - rx_head;
    if (phase == FAILED) return NET_IO_ERROR;
    if (first > n) first = n;
    memcpy(buffer, rx + rx_head, first); memcpy(buffer + first, rx, n - first);
    rx_head = (rx_head + n) & (RR_TCP_RX_CAPACITY - 1); rx_count -= n;
    /* Avoid a standalone window-update packet for every 128-byte render
     * batch. Reopen after a segment's worth, or when the queue is drained.
     * Incoming data still receives an immediate ACK with the current space. */
    freed_since_ack += n;
    if (n && (freed_since_ack >= RR_TCP_MSS || !rx_count)) ack_pending = 1;
    return (int)n;
}
void rr_tcp_close(uint16_t ticks)
{
    if (phase == SYN_SENT) {
        if (transmitted) (void)send_segment(RST, snd_nxt, tx, 0, ticks);
        rr_tcp_abort(); return;
    }
    if (phase == FAILED) { rr_tcp_abort(); return; }
    if (phase == CLOSED || phase == TIME_WAIT || local_close) return;
    local_close = 1; closing_at = ticks; rx_count = 0;
}
#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
