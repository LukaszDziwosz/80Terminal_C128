#ifndef TERMINAL80_XMODEM_H
#define TERMINAL80_XMODEM_H

#include <stdint.h>

#define XMODEM_BLOCK_SIZE 128U
#define XMODEM_1K_BLOCK_SIZE 1024U
#define XMODEM_MAX_ERRORS 10U
#define XMODEM_CRC_RETRIES 3U

#define XMODEM_SOH 0x01
#define XMODEM_STX 0x02
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_NAK 0x15
#define XMODEM_CAN 0x18
#define XMODEM_CRC_REQUEST 0x43

enum xmodem_event {
    XMODEM_EVENT_NONE = 0,
    XMODEM_EVENT_BLOCK_READY,
    XMODEM_EVENT_SEND_C,
    XMODEM_EVENT_SEND_ACK,
    XMODEM_EVENT_SEND_NAK,
    XMODEM_EVENT_COMPLETE,
    XMODEM_EVENT_CANCELLED,
    XMODEM_EVENT_ERROR
};

typedef void (*xmodem_store_callback)(void* context,
                                      uint16_t offset,
                                      uint8_t value);

struct xmodem_receiver {
    xmodem_store_callback store;
    void* store_context;
    uint16_t crc;
    uint16_t received_crc;
    uint16_t block_size;
    uint16_t data_index;
    uint16_t blocks_received;
    uint8_t state;
    uint8_t expected_block;
    uint8_t block_number;
    uint8_t checksum;
    uint8_t checksum_mode;
    uint8_t errors;
    uint8_t startup_timeouts;
    uint8_t mode_locked;
    uint8_t cancel_count;
    uint8_t eot_seen;
};

void xmodem_init(struct xmodem_receiver* receiver,
                 xmodem_store_callback store,
                 void* store_context);
void xmodem_select_checksum(struct xmodem_receiver* receiver,
                            uint8_t checksum_mode);
enum xmodem_event xmodem_receive(struct xmodem_receiver* receiver,
                                 uint8_t value);
enum xmodem_event xmodem_timeout(struct xmodem_receiver* receiver);
void xmodem_accept_block(struct xmodem_receiver* receiver);
uint16_t xmodem_current_block_size(
    const struct xmodem_receiver* receiver);
uint16_t xmodem_crc16(const uint8_t* data, uint16_t length);


/* Sender: the caller reads the file and sends packets; this decides when.
 * XMODEM_SEND_NEXT: read up to xmodem_send_capacity() bytes, then call
 * xmodem_send_load() with the count (0 at the end of the file) and send
 * the packet it describes; XMODEM_SEND_AGAIN: send the same packet again. */
#define XMODEM_SUB 0x1A  /* pads the last block */
enum xmodem_send_event {
    XMODEM_SEND_NONE = 0,
    XMODEM_SEND_NEXT,
    XMODEM_SEND_AGAIN,
    XMODEM_SEND_COMPLETE,
    XMODEM_SEND_CANCELLED,
    XMODEM_SEND_ERROR
};
struct xmodem_sender {
    uint32_t sent;          /* payload bytes acknowledged */
    uint16_t block_size;    /* of the current packet: 128 or 1024 */
    uint16_t length;        /* file bytes in it; the rest is padding */
    uint8_t state;
    uint8_t one_k;
    uint8_t crc_mode;
    uint8_t block_number;
    uint8_t errors;
    uint8_t cancel_count;
};
void xmodem_send_init(struct xmodem_sender *sender, uint8_t one_k);
enum xmodem_send_event xmodem_send_receive(struct xmodem_sender *sender, uint8_t value);
enum xmodem_send_event xmodem_send_timeout(struct xmodem_sender *sender);
uint8_t xmodem_send_started(const struct xmodem_sender *sender);
/* After the last block: the packet to (re)send is a single EOT. */
uint8_t xmodem_send_at_end(const struct xmodem_sender *sender);
/* Bytes to read for the next packet: 1024 in 1K CRC mode, else 128. */
uint16_t xmodem_send_capacity(const struct xmodem_sender *sender);
/* length file bytes are in data[0..]; pads data to the block size. Returns
 * 0 if this is the end of the file (send EOT: one byte, XMODEM_EOT). */
uint8_t xmodem_send_load(struct xmodem_sender *sender, uint8_t *data, uint16_t length);
/* The 3-byte header and the 1- or 2-byte check of the loaded packet. */
void xmodem_send_header(const struct xmodem_sender *sender, uint8_t *header);
uint8_t xmodem_send_check(const struct xmodem_sender *sender, const uint8_t *data, uint8_t *check);
#endif
