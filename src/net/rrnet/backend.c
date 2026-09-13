#include "network.h"
#pragma code(rrcode)
#pragma data(rrdata)
static const struct net_backend backend = {
    "RR-Net", "RR-Net TCP/IP adapter is not implemented yet.",
    net_unimplemented_init, net_unimplemented_connect,
    net_unimplemented_idle, net_unimplemented_read,
    net_unimplemented_write, net_unimplemented_state, net_unimplemented_idle
};
__noinline const struct net_backend *rrnet_backend(void) { return &backend; }
#pragma code(code)
#pragma data(data)
