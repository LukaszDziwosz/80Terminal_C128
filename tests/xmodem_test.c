#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xmodem.h"

static enum xmodem_event feed_block(struct xmodem_receiver* receiver,
                                    uint8_t number,
                                    const uint8_t* data,
                                    uint16_t size,
                                    uint8_t corrupt_crc)
{
    enum xmodem_event event;
    uint16_t crc;
    uint16_t i;

    assert(xmodem_receive(receiver, size == XMODEM_1K_BLOCK_SIZE ?
                          XMODEM_STX : XMODEM_SOH) ==
           XMODEM_EVENT_NONE);
    assert(xmodem_receive(receiver, number) == XMODEM_EVENT_NONE);
    assert(xmodem_receive(receiver, (uint8_t)~number) ==
           XMODEM_EVENT_NONE);
    for (i = 0; i != size; ++i) {
        assert(xmodem_receive(receiver, data[i]) == XMODEM_EVENT_NONE);
    }
    crc = xmodem_crc16(data, size);
    assert(xmodem_receive(receiver, (uint8_t)(crc >> 8)) ==
           XMODEM_EVENT_NONE);
    if (corrupt_crc) {
        crc ^= 1;
    }
    event = xmodem_receive(receiver, (uint8_t)crc);
    return event;
}

static void store_byte(void* context, uint16_t offset, uint8_t value)
{
    ((uint8_t*)context)[offset] = value;
}

static void test_crc(void)
{
    static const uint8_t vector[] = "123456789";

    assert(xmodem_crc16(vector, 9) == 0x31C3);
}

static void test_blocks(void)
{
    struct xmodem_receiver receiver;
    uint8_t input[XMODEM_BLOCK_SIZE];
    uint8_t block[XMODEM_BLOCK_SIZE];
    uint16_t i;

    for (i = 0; i != XMODEM_BLOCK_SIZE; ++i) {
        input[i] = (uint8_t)i;
    }
    input[20] = 0xFF;

    xmodem_init(&receiver, store_byte, block);
    assert(feed_block(&receiver, 1, input, XMODEM_BLOCK_SIZE, 0) ==
           XMODEM_EVENT_BLOCK_READY);
    assert(memcmp(input, block, sizeof(block)) == 0);
    xmodem_accept_block(&receiver);
    assert(receiver.blocks_received == 1);
    assert(receiver.expected_block == 2);

    assert(feed_block(&receiver, 1, input, XMODEM_BLOCK_SIZE, 0) ==
           XMODEM_EVENT_SEND_ACK);
    assert(receiver.blocks_received == 1);

    assert(feed_block(&receiver, 2, input, XMODEM_BLOCK_SIZE, 1) ==
           XMODEM_EVENT_SEND_NAK);
    assert(feed_block(&receiver, 3, input, XMODEM_BLOCK_SIZE, 0) ==
           XMODEM_EVENT_SEND_NAK);
    assert(feed_block(&receiver, 2, input, XMODEM_BLOCK_SIZE, 0) ==
           XMODEM_EVENT_BLOCK_READY);
    xmodem_accept_block(&receiver);
    assert(receiver.blocks_received == 2);
}

static void test_bad_complement(void)
{
    struct xmodem_receiver receiver;
    uint8_t block[XMODEM_BLOCK_SIZE];

    xmodem_init(&receiver, store_byte, block);
    assert(xmodem_receive(&receiver, XMODEM_SOH) == XMODEM_EVENT_NONE);
    assert(xmodem_receive(&receiver, 1) == XMODEM_EVENT_NONE);
    assert(xmodem_receive(&receiver, 1) == XMODEM_EVENT_SEND_NAK);
}

static void test_finish_and_cancel(void)
{
    struct xmodem_receiver receiver;
    uint8_t block[XMODEM_BLOCK_SIZE];

    xmodem_init(&receiver, store_byte, block);
    assert(xmodem_receive(&receiver, XMODEM_EOT) ==
           XMODEM_EVENT_SEND_NAK);
    assert(xmodem_receive(&receiver, XMODEM_EOT) ==
           XMODEM_EVENT_COMPLETE);

    xmodem_init(&receiver, store_byte, block);
    assert(xmodem_receive(&receiver, XMODEM_CAN) ==
           XMODEM_EVENT_NONE);
    assert(xmodem_receive(&receiver, XMODEM_CAN) ==
           XMODEM_EVENT_CANCELLED);
}

static void test_timeouts(void)
{
    struct xmodem_receiver receiver;
    uint8_t block[XMODEM_BLOCK_SIZE];
    uint8_t count;

    xmodem_init(&receiver, store_byte, block);
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_C);
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_C);
    for (count = 2; count != XMODEM_MAX_ERRORS - 1; ++count) {
        assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_NAK);
    }
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_ERROR);
}

static void test_1k_block(void)
{
    struct xmodem_receiver receiver;
    static uint8_t input[XMODEM_1K_BLOCK_SIZE];
    static uint8_t block[XMODEM_1K_BLOCK_SIZE];
    uint16_t i;

    for (i = 0; i != XMODEM_1K_BLOCK_SIZE; ++i) {
        input[i] = (uint8_t)(i ^ (i >> 8));
    }
    xmodem_init(&receiver, store_byte, block);
    assert(feed_block(&receiver, 1, input, XMODEM_1K_BLOCK_SIZE, 0) ==
           XMODEM_EVENT_BLOCK_READY);
    assert(xmodem_current_block_size(&receiver) ==
           XMODEM_1K_BLOCK_SIZE);
    assert(memcmp(input, block, sizeof(block)) == 0);
    xmodem_accept_block(&receiver);
    assert(receiver.blocks_received == 1);
}

static void test_checksum_fallback(void)
{
    struct xmodem_receiver receiver;
    uint8_t input[XMODEM_BLOCK_SIZE];
    uint8_t block[XMODEM_BLOCK_SIZE];
    uint16_t i;
    uint8_t checksum = 0;

    memset(input, 0x5A, sizeof(input));
    xmodem_init(&receiver, store_byte, block);
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_C);
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_C);
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_NAK);
    assert(xmodem_receive(&receiver, XMODEM_SOH) == XMODEM_EVENT_NONE);
    assert(xmodem_receive(&receiver, 1) == XMODEM_EVENT_NONE);
    assert(xmodem_receive(&receiver, 0xFE) == XMODEM_EVENT_NONE);
    for (i = 0; i != XMODEM_BLOCK_SIZE; ++i) {
        checksum += input[i];
        assert(xmodem_receive(&receiver, input[i]) ==
               XMODEM_EVENT_NONE);
    }
    assert(xmodem_receive(&receiver, checksum) ==
           XMODEM_EVENT_BLOCK_READY);
}

static void test_selected_modes(void)
{
    struct xmodem_receiver receiver;
    uint8_t block[XMODEM_BLOCK_SIZE];
    uint8_t count;

    xmodem_init(&receiver, store_byte, block);
    xmodem_select_checksum(&receiver, 0);
    for (count = 0; count != XMODEM_CRC_RETRIES + 1; ++count) {
        assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_C);
    }

    xmodem_init(&receiver, store_byte, block);
    xmodem_select_checksum(&receiver, 1);
    assert(xmodem_timeout(&receiver) == XMODEM_EVENT_SEND_NAK);
}

int main(void)
{
    test_crc();
    test_blocks();
    test_bad_complement();
    test_finish_and_cancel();
    test_timeouts();
    test_1k_block();
    test_checksum_fallback();
    test_selected_modes();
    puts("XMODEM-CRC core tests: PASS");
    return 0;
}
