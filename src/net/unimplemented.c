#include "network.h"

int net_unimplemented_init(void) { return NET_NOT_IMPLEMENTED; }
int net_unimplemented_connect(const char *hostname, uint16_t port)
{
    (void)hostname; (void)port;
    return NET_NOT_IMPLEMENTED;
}
int net_unimplemented_read(uint8_t *buffer, uint16_t capacity)
{
    (void)buffer; (void)capacity;
    return NET_NOT_IMPLEMENTED;
}
int net_unimplemented_write(const uint8_t *buffer, uint16_t length)
{
    (void)buffer; (void)length;
    return NET_NOT_IMPLEMENTED;
}
int net_unimplemented_state(void) { return NET_FAILED; }
void net_unimplemented_idle(void) {}
