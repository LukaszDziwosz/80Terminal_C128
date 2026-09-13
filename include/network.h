#ifndef T80_NETWORK_H
#define T80_NETWORK_H
#include <stdint.h>

/* One selected TCP stream. No Telnet, text encoding or disk I/O here. */
enum net_state {
    NET_CLOSED, NET_INITIALIZING, NET_CONNECTING, NET_CONNECTED,
    NET_CLOSING, NET_EOF, NET_FAILED
};
enum net_result {
    NET_OK = 0, NET_PENDING = 1,
    NET_NOT_IMPLEMENTED = -1, NET_NO_DEVICE = -2, NET_TIMEOUT = -3,
    NET_IO_ERROR = -4, NET_BAD_ARGUMENT = -5
};

struct net_backend {
    const char *name;
    const char *status;
    int (*init)(void);
    int (*connect)(const char *hostname, uint16_t port);
    void (*poll)(void);
    int (*read)(uint8_t *buffer, uint16_t capacity);
    int (*write)(const uint8_t *buffer, uint16_t length);
    /* Use int: pinned Oscar64 miscompiles indirect 8-bit return values. */
    int (*state)(void);
    void (*close)(void);
};
/* read/write: positive byte count, 0 = would block, negative = error.
 * Partial writes are normal; caller keeps the unsent tail. The backend
 * copies accepted TX bytes before returning, and owns its internal RX.
 * EOF is reported by state() only after buffered RX has been drained.
 * init/connect/poll must be bounded; deadlines belong to the backend.
 * close() may start asynchronous cleanup: poll while NET_CLOSING before
 * unloading the adapter. Poll even when the renderer/IEC writer has no work.
 * Never call from IRQ.
 */
const struct net_backend *rrnet_backend(void);
const struct net_backend *ultimate_backend(void);
const struct net_backend *wic64_backend(void);

/* Honest scaffold operations shared by the three unfinished adapters. */
int net_unimplemented_init(void);
int net_unimplemented_connect(const char *hostname, uint16_t port);
int net_unimplemented_read(uint8_t *buffer, uint16_t capacity);
int net_unimplemented_write(const uint8_t *buffer, uint16_t length);
int net_unimplemented_state(void);
void net_unimplemented_idle(void);
#endif
