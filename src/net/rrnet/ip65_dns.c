/* One asynchronous DNS A lookup, with bounded compressed-name/CNAME parsing. */
#include <string.h>
#include "network.h"
#include "ip65_dns.h"
#include "ip65_dhcp.h"
#include "ip65_ip.h"
#include "ip65_ethernet.h"
#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif
uint8_t rr_dns_state, rr_dns_address[4];
static char query_name[254], wanted[254], decoded[254];
static uint16_t ident = 0x1280;
static uint16_t port, deadline, started;
static uint8_t attempts, aliases;

static uint8_t lower(uint8_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static uint8_t literal(const char *s)
{
    uint8_t i, digits;
    uint16_t n;
    for (i = 0; i < 4; ++i) {
        n = 0; digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + *s++ - '0';
            if (++digits > 3 || n > 255) return 0;
        }
        if (!digits) return 0;
        rr_dns_address[i] = (uint8_t)n;
        if (i == 3) return !*s;
        if (*s++ != '.') return 0;
    }
    return 0;
}
void rr_dns_cancel(void) { rr_dns_state = RR_DNS_IDLE; }
int rr_dns_start(const char *hostname, uint16_t ticks)
{
    uint16_t n = 0;
    uint8_t label = 0, c;
    rr_dns_state = RR_DNS_IDLE;
    if (!hostname || !*hostname) return NET_BAD_ARGUMENT;
    if (literal(hostname)) {
        if (!rr_dns_address[0] || rr_dns_address[0] >= 224 || rr_dns_address[0] == 127)
            return NET_BAD_ARGUMENT;
        rr_dns_state = RR_DNS_DONE; return NET_OK;
    }
    while (*hostname) {
        c = lower((uint8_t)*hostname++);
        if (n >= 253) return NET_BAD_ARGUMENT;
        if (c == '.') {
            if (!label || query_name[n - 1] == '-') return NET_BAD_ARGUMENT;
            if (!*hostname) break;
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
                (!label && c == '-') || ++label > 63) return NET_BAD_ARGUMENT;
        }
        query_name[n++] = (char)c;
    }
    if (!n || query_name[n - 1] == '-') return NET_BAD_ARGUMENT;
    query_name[n] = 0;
    ident = (uint16_t)(ident + 257 + ticks);
    port = 49152U + (ident & 0x3fff);
    started = deadline = ticks; attempts = aliases = 0;
    rr_dns_state = RR_DNS_WAIT;
    return NET_PENDING;
}
void rr_dns_poll(uint16_t ticks)
{
    uint8_t *out = rrnet_ip65_eth_out + 42;
    uint16_t pos, label_pos, i, size;
    uint8_t result;
    if (rr_dns_state != RR_DNS_WAIT) return;
    if ((uint16_t)(ticks - started) >= 1200U) { rr_dns_state = RR_DNS_TIMEOUT; return; }
    if ((int16_t)(ticks - deadline) < 0) return;
    if (attempts == 4) { rr_dns_state = RR_DNS_TIMEOUT; return; }
    size = (uint16_t)strlen(query_name) + 18;
    result = rr_ip_prepare(rrnet_dhcp_dns, 17, size + 8, ticks);
    if (result == 2) return;
    if (result) { rr_dns_state = RR_DNS_ERROR; return; }
    memset(out, 0, 12); rr_put16(out, ident); out[2] = 1; out[5] = 1;
    pos = 13; label_pos = 12; out[label_pos] = 0;
    for (i = 0; query_name[i]; ++i) {
        if (query_name[i] == '.') { label_pos = pos++; out[label_pos] = 0; }
        else { out[pos++] = (uint8_t)query_name[i]; ++out[label_pos]; }
    }
    out[pos++] = 0; rr_put16(out + pos, 1); rr_put16(out + pos + 2, 1);
    if (rr_udp_send(port, 53, size)) { rr_dns_state = RR_DNS_ERROR; return; }
    deadline = ticks + (uint16_t)(120U << (attempts < 2 ? attempts : 2));
    ++attempts;
}
/* Decode a name without leaving the UDP payload or following pointer loops.
 * Return the position following the original compressed representation. */
static uint16_t name_at(uint16_t pos, char *out)
{
    const uint8_t *in = rr_udp_payload;
    uint16_t next = 0, used = 0, steps = 0;
    uint8_t len, i;
    while (pos < rr_udp_length && ++steps <= 128) {
        len = in[pos++];
        if (!len) { out[used] = 0; return next ? next : pos; }
        if ((len & 0xc0) == 0xc0) {
            if (pos >= rr_udp_length) return 0;
            if (!next) next = pos + 1;
            pos = ((uint16_t)(len & 63) << 8) | in[pos];
        } else {
            if ((len & 0xc0) || pos + len > rr_udp_length || used + len + (used != 0) > 253) return 0;
            if (used) out[used++] = '.';
            for (i = 0; i < len; ++i) out[used++] = (char)lower(in[pos++]);
        }
    }
    return 0;
}
void rr_dns_input(uint16_t ticks)
{
    const uint8_t *in = rr_udp_payload;
    uint16_t pos, answer_start, count, i, type, length, data, next;
    uint8_t hop, changed = 0;
    if (rr_dns_state != RR_DNS_WAIT || !rr_ip_local() || rr_udp_source != 53 ||
        rr_udp_destination != port || memcmp(rr_ip_header + 12, rrnet_dhcp_dns, 4) ||
        rr_udp_length < 12 || rr_get16(in) != ident || !(in[2] & 0x80) || (in[2] & 0x78) ||
        rr_get16(in + 4) != 1) return;
    pos = name_at(12, decoded);
    if (!pos || pos + 4 > rr_udp_length || strcmp(decoded, query_name) ||
        rr_get16(in + pos) != 1 || rr_get16(in + pos + 2) != 1) return;
    if ((in[2] & 2) || (in[3] & 15)) { rr_dns_state = RR_DNS_ERROR; return; }
    answer_start = pos + 4; count = rr_get16(in + 6);
    if (count > 64) return;
    strcpy(wanted, query_name);
    for (hop = 0; hop < 8; ++hop) {
        pos = answer_start;
        for (i = 0; i < count; ++i) {
            pos = name_at(pos, decoded);
            if (!pos || pos + 10 > rr_udp_length) return;
            type = rr_get16(in + pos); length = rr_get16(in + pos + 8);
            data = pos + 10;
            if (length > rr_udp_length - data) return;
            next = data + length;
            if (!strcmp(decoded, wanted) && rr_get16(in + pos + 2) == 1) {
                if (type == 1 && length == 4 && in[data] && in[data] < 224 && in[data] != 127) {
                    memcpy(rr_dns_address, in + data, 4); rr_dns_state = RR_DNS_DONE; return;
                }
                if (type == 5) {
                    if (name_at(data, decoded) != next || !decoded[0] || !strcmp(decoded, wanted)) return;
                    strcpy(wanted, decoded); changed = 1; break;
                }
            }
            pos = next;
        }
        if (i == count) break;
    }
    if (hop == 8) { rr_dns_state = RR_DNS_ERROR; return; }
    if (changed && ++aliases <= 8) {
        strcpy(query_name, wanted); ++ident;
        attempts = 0; deadline = ticks;
    } else rr_dns_state = RR_DNS_ERROR;
}
#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
