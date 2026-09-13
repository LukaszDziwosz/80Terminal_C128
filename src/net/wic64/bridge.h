#ifndef T80_WIC64_BRIDGE_H
#define T80_WIC64_BRIDGE_H
#include <stdint.h>

enum {
    WIC64_BRIDGE_OK = 0,
    WIC64_BRIDGE_TIMEOUT = 0x80,
    WIC64_BRIDGE_TRUNCATED = 0x81,
    WIC64_BRIDGE_LEGACY = 0x82,
    WIC64_MAILBOX_HOST = 0x8200,
    WIC64_MAILBOX_TX = 0x8280,
    WIC64_MAILBOX_RX = 0x8480,
    WIC64_MAILBOX_ERROR = 0x8f00,
    WIC64_MAILBOX_RX_LENGTH = 0x8f80,
    WIC64_MAILBOX_AVAILABLE = 0x8f82,
    WIC64_MAILBOX_TX_LENGTH = 0x8f84
};

uint8_t wic64_detect_bridge(void);
uint8_t wic64_open_bridge(void);
uint8_t wic64_available_bridge(void);
uint8_t wic64_read_bridge(void);
uint8_t wic64_write_bridge(void);
uint8_t wic64_close_bridge(void);
uint8_t wic64_status_message_bridge(void);
#endif
