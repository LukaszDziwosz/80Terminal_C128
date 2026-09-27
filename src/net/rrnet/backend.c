#include "network.h"
#include "platform.h"
#include "ip65_rrnet.h"
#include "ip65_arp.h"
#include "ip65_dhcp.h"
#include "ip65_ip.h"
#include "ip65_dns.h"
#include "ip65_tcp.h"
#include "ip65_ethernet.h"
#include <stdio.h>
#include <string.h>

#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif

static enum net_state connection;
static char message[74];
static uint8_t previous_dhcp_state;
static uint16_t previous_tick, stalled_polls;
static uint8_t hardware_ready, stream_active, resolving, configured_ip[4];
static uint16_t remote_port;
static uint16_t stats_tick;

static void set_message(const char *text)
{
    uint8_t i = 0;
    while (text[i] && i < sizeof(message) - 1) { message[i] = text[i]; ++i; }
    message[i] = 0;
}
static int initialize(void)
{
    uint8_t speed = platform_slow();
    uint8_t result = rrnet_ip65_init();
    platform_restore_speed(speed);
    hardware_ready = stream_active = resolving = 0;
    rr_tcp_init(); rr_dns_cancel(); memset(configured_ip, 0, 4);
    if (result) {
        connection = NET_FAILED;
        set_message(result == RRNET_IP65_NO_CHIP ?
                    "RR-Net CS8900A was not detected." :
                    "RR-Net CS8900A reset timed out.");
        return NET_NO_DEVICE;
    }
    hardware_ready = 1;
    rrnet_dhcp_start(platform_ticks());
    previous_tick = platform_ticks(); stalled_polls = 0;
    previous_dhcp_state = RR_DHCP_OFF;
    connection = NET_INITIALIZING;
    set_message("RR-Net detected. DHCP: discovering server...");
    return NET_PENDING;
}
static void network_status(void)
{
    sprintf(message, "DHCP IP %u.%u.%u.%u DNS 1.1.1.1; ready.",
            (unsigned)rrnet_ip65_ipv4[0], (unsigned)rrnet_ip65_ipv4[1],
            (unsigned)rrnet_ip65_ipv4[2], (unsigned)rrnet_ip65_ipv4[3]);
}
static void poll_inner(uint16_t ticks)
{
    int new_state;
    uint8_t budget = 4;
    rrnet_dhcp_tick(ticks);
    /* Drain a bounded burst before returning to the renderer/keyboard.
     * ACK each TCP frame promptly; only hardware transfers run at 1 MHz. */
    while (budget-- && rrnet_ip65_eth_poll()) {
        ++rrnet_dhcp_received;
        if (rrnet_ip65_ipv4[0]) rrnet_ip65_arp_process();
        if (rr_ip_parse()) {
            if (rr_ip_protocol == 17 && rr_udp_parse()) {
                if (rr_udp_destination == 68) rrnet_dhcp_input(ticks);
                else rr_dns_input(ticks);
            } else if (rr_ip_protocol == 6) {
                rr_tcp_input(ticks);
                rr_tcp_poll(ticks);
            }
        }
    }
    rrnet_dhcp_tick(ticks);
    if (configured_ip[0] && memcmp(configured_ip, rrnet_ip65_ipv4, 4)) {
        rr_tcp_abort(); rr_dns_cancel();
        if (stream_active) {
            stream_active = resolving = 0; connection = NET_FAILED;
            set_message("DHCP address changed; connection ended. Retry the connection.");
        }
    }
    memcpy(configured_ip, rrnet_ip65_ipv4, 4);
    if (previous_dhcp_state != rrnet_dhcp_state) {
      previous_dhcp_state = rrnet_dhcp_state;
      switch (rrnet_dhcp_state) {
    case RR_DHCP_SELECTING:
        if (connection != NET_FAILED) {
            connection = NET_INITIALIZING; set_message("DHCP: discovering server...");
        }
        break;
    case RR_DHCP_REQUESTING:
        if (connection != NET_FAILED) {
            connection = NET_INITIALIZING; set_message("DHCP: requesting offered address...");
        }
        break;
    case RR_DHCP_BOUND:
        if (!stream_active && connection != NET_FAILED) { connection = NET_CLOSED; network_status(); }
        break;
    case RR_DHCP_RENEWING:
        if (!stream_active) set_message("DHCP: renewing lease..."); break;
    case RR_DHCP_REBINDING:
        if (!stream_active) set_message("DHCP: rebinding lease..."); break;
    case RR_DHCP_TIMEOUT:
        connection = NET_FAILED;
        sprintf(message, "DHCP timed out: sent %u, RX %u, DHCP %u. F1 retries.",
                (unsigned)rrnet_dhcp_sent, (unsigned)rrnet_dhcp_received,
                (unsigned)rrnet_dhcp_replies); break;
    case RR_DHCP_IO_ERROR:
        connection = NET_FAILED;
        set_message("RR-Net could not send a DHCP packet. F1 retries."); break;
    default: break;
      }
    }
    if (!rrnet_ip65_ipv4[0]) return;
    if (resolving) {
        rr_dns_poll(ticks);
        if (rr_dns_state == RR_DNS_DONE) {
            resolving = 0;
            if (rr_tcp_connect(rr_dns_address, remote_port, ticks) < 0) {
                stream_active = 0; connection = NET_FAILED;
                set_message("Invalid remote IPv4 address or port."); return;
            }
            set_message("Opening TCP connection...");
        } else if (rr_dns_state == RR_DNS_ERROR || rr_dns_state == RR_DNS_TIMEOUT) {
            resolving = stream_active = 0; connection = NET_FAILED;
            set_message(rr_dns_state == RR_DNS_TIMEOUT ? "DNS lookup timed out." :
                        "DNS lookup failed: no usable IPv4 answer.");
            return;
        } else return;
    }
    rr_tcp_poll(ticks);
    if (stream_active) {
        new_state = rr_tcp_state();
        if (new_state != (int)connection) {
            connection = (enum net_state)new_state;
            if (connection == NET_CONNECTED) set_message("Connected via RR-Net.");
            else if (connection == NET_EOF) set_message("Remote closed the connection.");
            else if (connection == NET_CLOSED) { stream_active = 0; network_status(); }
            else if (connection == NET_FAILED)
                set_message(rr_tcp_error == RR_TCP_REFUSED ? "TCP connection refused." :
                            rr_tcp_error == RR_TCP_RESET ? "Connection reset by remote host." :
                            rr_tcp_error == RR_TCP_TIMEOUT ? "TCP connection timed out." :
                            "RR-Net packet transmission failed.");
        }
        if (connection == NET_CONNECTED && (uint16_t)(ticks - stats_tick) >= 60U) {
            stats_tick = ticks;
            sprintf(message, "RR peak %u gap %u dup %u retry %u zero %u wait %u",
                    rr_tcp_stats.peak, rr_tcp_stats.gaps, rr_tcp_stats.duplicates,
                    rr_tcp_stats.retries, rr_tcp_stats.zero_windows, rr_tcp_stats.max_poll_ticks);
        }
    }
}
static void poll(void)
{
    uint16_t ticks;
    if (!hardware_ready) return;
    ticks = platform_ticks();
    if (stream_active && connection == NET_CONNECTED &&
        (uint16_t)(ticks - previous_tick) > rr_tcp_stats.max_poll_ticks)
        rr_tcp_stats.max_poll_ticks = (uint16_t)(ticks - previous_tick);
    if (ticks != previous_tick) { previous_tick = ticks; stalled_polls = 0; }
    else if (++stalled_polls == 8192) {
        connection = NET_FAILED; hardware_ready = 0;
        rr_tcp_abort(); rr_dns_cancel();
        set_message("Network stopped: C128 clock is not advancing. F1 retries."); return;
    }
    poll_inner(ticks);
}
static int connect_host(const char *hostname, uint16_t port)
{
    int result;
    if (!hardware_ready || !rrnet_ip65_ipv4[0]) return NET_NO_DEVICE;
    if (!port || stream_active) return NET_BAD_ARGUMENT;
    result = rr_dns_start(hostname, platform_ticks());
    if (result < 0) { set_message("Invalid hostname or IPv4 address."); return result; }
    remote_port = port; stream_active = resolving = 1; connection = NET_CONNECTING;
    stats_tick = platform_ticks();
    set_message(result == NET_OK ? "Opening TCP connection..." : "Resolving hostname via 1.1.1.1...");
    return NET_PENDING;
}
static int read_stream(uint8_t *buffer, uint16_t capacity) { return rr_tcp_read(buffer, capacity); }
static int write_stream(const uint8_t *buffer, uint16_t length) { return rr_tcp_write(buffer, length); }
static int state(void) { return (int)connection; }
static void close_connection(void)
{
    rr_dns_cancel(); resolving = 0;
    rr_tcp_close(platform_ticks());
    connection = (enum net_state)rr_tcp_state();
    if (connection == NET_CLOSED) {
        stream_active = 0;
        if (!hardware_ready || !rrnet_ip65_ipv4[0]) connection = NET_FAILED;
    }
}
static struct net_backend backend = {
    "RR-Net", message,
    initialize, connect_host,
    poll, read_stream,
    write_stream, state, close_connection
};
#ifndef RRNET_IP65_TEST
__noinline
#endif
const struct net_backend *rrnet_backend(void) { return &backend; }
#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
