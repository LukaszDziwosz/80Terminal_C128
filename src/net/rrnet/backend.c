#include "network.h"
#include "platform.h"
#include "ip65_rrnet.h"
#include "ip65_arp.h"

#pragma code(rrcode)
#pragma data(rrdata)

static enum net_state connection;
static char message[74];

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
    rrnet_ip65_arp_init();
    connection = NET_FAILED;
    set_message("RR-Net detected. DHCP, IPv4, DNS and TCP are still being ported.");
    return NET_NOT_IMPLEMENTED;
}
static int state(void) { return (int)connection; }
static void close_connection(void)
{
    rrnet_ip65_shutdown();
    connection = NET_CLOSED;
}
static struct net_backend backend = {
    "RR-Net", message,
    initialize, net_unimplemented_connect,
    net_unimplemented_idle, net_unimplemented_read,
    net_unimplemented_write, state, close_connection
};
__noinline const struct net_backend *rrnet_backend(void) { return &backend; }
#pragma code(code)
#pragma data(data)
