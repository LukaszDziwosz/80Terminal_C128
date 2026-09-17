#include "network.h"
#include "platform.h"
#include "ip65_rrnet.h"
#include "ip65_arp.h"
#include "ip65_dhcp.h"
#include <stdio.h>

#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif

static enum net_state connection;
static char message[74];
static uint8_t previous_dhcp_state;
static uint16_t previous_tick, stalled_polls;

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
    if (result) {
        connection = NET_FAILED;
        set_message(result == RRNET_IP65_NO_CHIP ?
                    "RR-Net CS8900A was not detected." :
                    "RR-Net CS8900A reset timed out.");
        return NET_NO_DEVICE;
    }
    rrnet_dhcp_start(platform_ticks());
    previous_tick = platform_ticks(); stalled_polls = 0;
    previous_dhcp_state = RR_DHCP_OFF;
    connection = NET_INITIALIZING;
    set_message("RR-Net detected. DHCP: discovering server...");
    return NET_PENDING;
}
static void poll(void)
{
    uint8_t speed;
    uint16_t ticks;
    if (connection == NET_FAILED) return;
    ticks = platform_ticks();
    if (ticks != previous_tick) { previous_tick = ticks; stalled_polls = 0; }
    else if (++stalled_polls == 8192) {
        connection = NET_FAILED;
        set_message("DHCP stopped: C128 clock is not advancing. F1 retries.");
        return;
    }
    speed = platform_slow();
    rrnet_dhcp_poll(ticks);
    platform_restore_speed(speed);
    if (previous_dhcp_state == rrnet_dhcp_state) return;
    previous_dhcp_state = rrnet_dhcp_state;
    switch (rrnet_dhcp_state) {
    case RR_DHCP_SELECTING:
        connection = NET_INITIALIZING;
        set_message("DHCP: discovering server..."); break;
    case RR_DHCP_REQUESTING:
        connection = NET_INITIALIZING;
        set_message("DHCP: requesting offered address..."); break;
    case RR_DHCP_BOUND:
        connection = NET_CLOSED;
        sprintf(message, "DHCP IP %u.%u.%u.%u DNS 1.1.1.1; TCP not implemented.",
                (unsigned)rrnet_ip65_ipv4[0], (unsigned)rrnet_ip65_ipv4[1],
                (unsigned)rrnet_ip65_ipv4[2], (unsigned)rrnet_ip65_ipv4[3]);
        break;
    case RR_DHCP_RENEWING:
        set_message("DHCP: renewing lease..."); break;
    case RR_DHCP_REBINDING:
        set_message("DHCP: rebinding lease..."); break;
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
static int connect_unavailable(const char *hostname, uint16_t port)
{
    (void)hostname; (void)port;
    set_message("RR-Net TCP and DNS resolution are not implemented yet.");
    return NET_NOT_IMPLEMENTED;
}
static int state(void) { return (int)connection; }
static void close_connection(void)
{
    rrnet_ip65_shutdown();
    if (connection != NET_INITIALIZING && connection != NET_FAILED)
        connection = NET_CLOSED;
}
static struct net_backend backend = {
    "RR-Net", message,
    initialize, connect_unavailable,
    poll, net_unimplemented_read,
    net_unimplemented_write, state, close_connection
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
