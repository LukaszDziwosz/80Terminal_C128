/* Ported from References/80Terminal_cc65/src/xmodem.c. */
#include <stdint.h>

#include "xmodem.h"

#define XMODEM_STATE_WAITING       0
#define XMODEM_STATE_BLOCK_NUMBER  1
#define XMODEM_STATE_COMPLEMENT    2
#define XMODEM_STATE_DATA          3
#define XMODEM_STATE_CRC_HIGH      4
#define XMODEM_STATE_CRC_LOW       5
#define XMODEM_STATE_BLOCK_READY   6
#define XMODEM_STATE_CHECKSUM      7

static uint16_t crc_add(uint16_t crc, uint8_t value)
{
    uint8_t bit;

    crc ^= (uint16_t)value << 8;
    for (bit = 0; bit != 8; ++bit) {
        if ((crc & 0x8000U) != 0) {
            crc = (uint16_t)((crc << 1) ^ 0x1021U);
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

static void reset_packet(struct xmodem_receiver* receiver)
{
    receiver->state = XMODEM_STATE_WAITING;
    receiver->data_index = 0;
    receiver->crc = 0;
    receiver->received_crc = 0;
    receiver->checksum = 0;
}

static enum xmodem_event packet_error(struct xmodem_receiver* receiver)
{
    reset_packet(receiver);
    ++receiver->errors;
    if (receiver->errors >= XMODEM_MAX_ERRORS) {
        return XMODEM_EVENT_ERROR;
    }
    return XMODEM_EVENT_SEND_NAK;
}

uint16_t xmodem_crc16(const uint8_t* data, uint16_t length)
{
    uint16_t crc;

    crc = 0;
    while (length != 0) {
        crc = crc_add(crc, *data++);
        --length;
    }
    return crc;
}

void xmodem_init(struct xmodem_receiver* receiver,
                 xmodem_store_callback store,
                 void* store_context)
{
    receiver->store = store;
    receiver->store_context = store_context;
    receiver->block_size = XMODEM_BLOCK_SIZE;
    receiver->blocks_received = 0;
    receiver->expected_block = 1;
    receiver->checksum_mode = 0;
    receiver->errors = 0;
    receiver->startup_timeouts = 0;
    receiver->mode_locked = 0;
    receiver->cancel_count = 0;
    receiver->eot_seen = 0;
    reset_packet(receiver);
}

void xmodem_select_checksum(struct xmodem_receiver* receiver,
                            uint8_t checksum_mode)
{
    receiver->checksum_mode = checksum_mode != 0;
    receiver->startup_timeouts = 0;
    receiver->mode_locked = 1;
}

enum xmodem_event xmodem_receive(struct xmodem_receiver* receiver,
                                 uint8_t value)
{
    if (receiver->state == XMODEM_STATE_BLOCK_READY) {
        return XMODEM_EVENT_NONE;
    }

    if (receiver->state == XMODEM_STATE_WAITING) {
        if (value == XMODEM_CAN) {
            ++receiver->cancel_count;
            if (receiver->cancel_count == 2) {
                reset_packet(receiver);
                return XMODEM_EVENT_CANCELLED;
            }
            return XMODEM_EVENT_NONE;
        }
        receiver->cancel_count = 0;
    }

    switch (receiver->state) {
    case XMODEM_STATE_WAITING:
        if (value == XMODEM_SOH || value == XMODEM_STX) {
            receiver->block_size = value == XMODEM_STX ?
                XMODEM_1K_BLOCK_SIZE : XMODEM_BLOCK_SIZE;
            receiver->state = XMODEM_STATE_BLOCK_NUMBER;
            receiver->eot_seen = 0;
            return XMODEM_EVENT_NONE;
        }
        if (value == XMODEM_EOT) {
            if (!receiver->eot_seen) {
                receiver->eot_seen = 1;
                return XMODEM_EVENT_SEND_NAK;
            }
            return XMODEM_EVENT_COMPLETE;
        }
        return XMODEM_EVENT_NONE;

    case XMODEM_STATE_BLOCK_NUMBER:
        receiver->block_number = value;
        receiver->state = XMODEM_STATE_COMPLEMENT;
        return XMODEM_EVENT_NONE;

    case XMODEM_STATE_COMPLEMENT:
        if ((uint8_t)(receiver->block_number + value) != 0xFFU) {
            return packet_error(receiver);
        }
        receiver->data_index = 0;
        receiver->crc = 0;
        receiver->state = XMODEM_STATE_DATA;
        return XMODEM_EVENT_NONE;

    case XMODEM_STATE_DATA:
        if (receiver->store != 0) {
            receiver->store(receiver->store_context,
                            receiver->data_index,
                            value);
        }
        ++receiver->data_index;
        receiver->crc = crc_add(receiver->crc, value);
        receiver->checksum += value;
        if (receiver->data_index == receiver->block_size) {
            receiver->state = receiver->checksum_mode ?
                XMODEM_STATE_CHECKSUM : XMODEM_STATE_CRC_HIGH;
        }
        return XMODEM_EVENT_NONE;

    case XMODEM_STATE_CHECKSUM:
        if (value != receiver->checksum) {
            return packet_error(receiver);
        }
        break;

    case XMODEM_STATE_CRC_HIGH:
        receiver->received_crc = (uint16_t)value << 8;
        receiver->state = XMODEM_STATE_CRC_LOW;
        return XMODEM_EVENT_NONE;

    case XMODEM_STATE_CRC_LOW:
        receiver->received_crc |= value;
        if (receiver->received_crc != receiver->crc) {
            return packet_error(receiver);
        }
        break;

    default:
        return XMODEM_EVENT_NONE;
    }

    if (receiver->state == XMODEM_STATE_CHECKSUM ||
        receiver->state == XMODEM_STATE_CRC_LOW) {
        if (receiver->block_number ==
            (uint8_t)(receiver->expected_block - 1U)) {
            reset_packet(receiver);
            return XMODEM_EVENT_SEND_ACK;
        }
        if (receiver->block_number != receiver->expected_block) {
            return packet_error(receiver);
        }
        receiver->state = XMODEM_STATE_BLOCK_READY;
        return XMODEM_EVENT_BLOCK_READY;
    }
    return XMODEM_EVENT_NONE;
}

void xmodem_accept_block(struct xmodem_receiver* receiver)
{
    if (receiver->state != XMODEM_STATE_BLOCK_READY) {
        return;
    }
    ++receiver->expected_block;
    ++receiver->blocks_received;
    receiver->errors = 0;
    reset_packet(receiver);
}

enum xmodem_event xmodem_timeout(struct xmodem_receiver* receiver)
{
    reset_packet(receiver);
    ++receiver->errors;
    if (receiver->errors >= XMODEM_MAX_ERRORS) {
        return XMODEM_EVENT_ERROR;
    }
    if (receiver->blocks_received == 0) {
        ++receiver->startup_timeouts;
        if (receiver->mode_locked) {
            return receiver->checksum_mode ?
                XMODEM_EVENT_SEND_NAK : XMODEM_EVENT_SEND_C;
        }
        if (receiver->startup_timeouts >= XMODEM_CRC_RETRIES) {
            receiver->checksum_mode = 1;
            return XMODEM_EVENT_SEND_NAK;
        }
        return XMODEM_EVENT_SEND_C;
    }
    return XMODEM_EVENT_SEND_NAK;
}

uint16_t xmodem_current_block_size(
    const struct xmodem_receiver* receiver)
{
    return receiver->block_size;
}
