/* XMODEM, XMODEM-CRC and XMODEM-1K sender. */
#include <string.h>
#include "xmodem.h"

#ifdef T80_APP
/* Part of the transfer overlay (overlay.h). */
#pragma code(xfercode)
#pragma data(xferdata)
#endif

#define SEND_WAIT_START 0
#define SEND_WAIT_ACK   1
#define SEND_WAIT_EOT   2
#define SEND_DONE       3

void xmodem_send_init(struct xmodem_sender *sender, uint8_t one_k)
{
    memset(sender, 0, sizeof(*sender));
    sender->one_k = one_k;
    sender->crc_mode = 1;
    sender->block_number = 1;
    sender->block_size = XMODEM_BLOCK_SIZE;
}
uint8_t xmodem_send_started(const struct xmodem_sender *sender)
{
    return sender->state != SEND_WAIT_START;
}
uint8_t xmodem_send_at_end(const struct xmodem_sender *sender)
{
    return sender->state == SEND_WAIT_EOT || (sender->state == SEND_DONE && !sender->length);
}
uint16_t xmodem_send_capacity(const struct xmodem_sender *sender)
{
    return sender->one_k && sender->crc_mode ? XMODEM_1K_BLOCK_SIZE : XMODEM_BLOCK_SIZE;
}
uint8_t xmodem_send_load(struct xmodem_sender *sender, uint8_t *data, uint16_t length)
{
    if (!length) {
        sender->state = SEND_WAIT_EOT;
        sender->length = 0;
        return 0;
    }
    /* A short tail goes in a 128-byte block, even in 1K mode. */
    sender->block_size = length > XMODEM_BLOCK_SIZE ? XMODEM_1K_BLOCK_SIZE : XMODEM_BLOCK_SIZE;
    sender->length = length;
    memset(data + length, XMODEM_SUB, sender->block_size - length);
    sender->state = SEND_WAIT_ACK;
    return 1;
}
void xmodem_send_header(const struct xmodem_sender *sender, uint8_t *header)
{
    header[0] = sender->block_size == XMODEM_1K_BLOCK_SIZE ? XMODEM_STX : XMODEM_SOH;
    header[1] = sender->block_number;
    header[2] = (uint8_t)~sender->block_number;
}
uint8_t xmodem_send_check(const struct xmodem_sender *sender, const uint8_t *data, uint8_t *check)
{
    uint16_t i, crc;
    uint8_t sum = 0;
    if (sender->crc_mode) {
        crc = xmodem_crc16(data, sender->block_size);
        check[0] = (uint8_t)(crc >> 8);
        check[1] = (uint8_t)crc;
        return 2;
    }
    for (i = 0; i < sender->block_size; ++i) sum = (uint8_t)(sum + data[i]);
    check[0] = sum;
    return 1;
}
static enum xmodem_send_event retry(struct xmodem_sender *sender, uint8_t count)
{
    if (count && ++sender->errors >= XMODEM_MAX_ERRORS) {
        sender->state = SEND_DONE;
        return XMODEM_SEND_ERROR;
    }
    return XMODEM_SEND_AGAIN;
}
enum xmodem_send_event xmodem_send_receive(struct xmodem_sender *sender, uint8_t value)
{
    if (sender->state == SEND_DONE) return XMODEM_SEND_NONE;
    if (value == XMODEM_CAN) {
        if (++sender->cancel_count >= 2) {
            sender->state = SEND_DONE;
            return XMODEM_SEND_CANCELLED;
        }
        return XMODEM_SEND_NONE;
    }
    sender->cancel_count = 0;
    if (sender->state == SEND_WAIT_START) {
        /* 'C' asks for CRC; NAK for the original checksum. */
        if (value != XMODEM_CRC_REQUEST && value != XMODEM_NAK) return XMODEM_SEND_NONE;
        sender->crc_mode = value == XMODEM_CRC_REQUEST;
        return XMODEM_SEND_NEXT;
    }
    if (value == XMODEM_ACK) {
        sender->errors = 0;
        if (sender->state == SEND_WAIT_EOT) {
            sender->state = SEND_DONE;
            return XMODEM_SEND_COMPLETE;
        }
        sender->sent += sender->length;
        ++sender->block_number;
        return XMODEM_SEND_NEXT;
    }
    if (value == XMODEM_NAK) {
        /* Receivers NAK the first EOT on purpose; that is not an error. */
        return retry(sender, sender->state != SEND_WAIT_EOT || sender->errors);
    }
    /* Receivers repeat 'C' until the first block lands. */
    if (value == XMODEM_CRC_REQUEST && sender->block_number == 1 && sender->state == SEND_WAIT_ACK)
        return retry(sender, 1);
    return XMODEM_SEND_NONE;
}
enum xmodem_send_event xmodem_send_timeout(struct xmodem_sender *sender)
{
    if (sender->state == SEND_DONE || sender->state == SEND_WAIT_START) return XMODEM_SEND_NONE;
    return retry(sender, 1);
}
#ifdef T80_APP
#pragma code(code)
#pragma data(data)
#endif
