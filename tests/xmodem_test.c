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


/* ---- Sender ---- */
static uint8_t file_data[3000];
static uint8_t packet[XMODEM_1K_BLOCK_SIZE];
static uint8_t received[3200];
static uint16_t file_size, file_at;

/* Feeds one sender packet through a receiver; corrupt flips a data byte. */
static enum xmodem_event deliver(struct xmodem_receiver* receiver,
                                 struct xmodem_sender* sender, uint8_t corrupt)
{
    uint8_t header[3], check[2], n, i;
    enum xmodem_event event = XMODEM_EVENT_NONE;
    uint16_t j;
    if (xmodem_send_at_end(sender)) return xmodem_receive(receiver, XMODEM_EOT);
    xmodem_send_header(sender, header);
    n = xmodem_send_check(sender, packet, check);
    for (i = 0; i < 3; ++i) assert(xmodem_receive(receiver, header[i]) == XMODEM_EVENT_NONE);
    for (j = 0; j < sender->block_size; ++j)
        assert(xmodem_receive(receiver, (uint8_t)(packet[j] ^ (corrupt && j == 5))) == XMODEM_EVENT_NONE);
    for (i = 0; i < n; ++i) event = xmodem_receive(receiver, check[i]);
    return event;
}
static void load_next(struct xmodem_sender* sender)
{
    uint16_t n = xmodem_send_capacity(sender);
    if (n > file_size - file_at) n = file_size - file_at;
    memcpy(packet, file_data + file_at, n);
    file_at += n;
    xmodem_send_load(sender, packet, n);
}
static void store_received(void* context, uint16_t offset, uint8_t value)
{
    (void)context;
    ((uint8_t*)context)[offset] = value;
}
/* A complete transfer through our own receiver, with one corrupted block
 * and one lost ACK on the way. */
static void loopback(uint8_t one_k, uint8_t checksum)
{
    struct xmodem_receiver receiver;
    struct xmodem_sender sender;
    enum xmodem_event event;
    enum xmodem_send_event action;
    uint8_t block[XMODEM_1K_BLOCK_SIZE];
    uint16_t stored = 0, i, blocks = 0;
    uint8_t corrupted = 0, lost = 0;
    file_size = 2500; file_at = 0;
    for (i = 0; i < file_size; ++i) file_data[i] = (uint8_t)(i * 7 + (i >> 8));
    file_data[100] = 0xff;
    xmodem_init(&receiver, store_received, block);
    if (checksum) xmodem_select_checksum(&receiver, 1);
    xmodem_send_init(&sender, one_k);
    assert(xmodem_send_receive(&sender, 'x') == XMODEM_SEND_NONE);
    assert(xmodem_send_timeout(&sender) == XMODEM_SEND_NONE);
    action = xmodem_send_receive(&sender, checksum ? XMODEM_NAK : XMODEM_CRC_REQUEST);
    for (;;) {
        if (action == XMODEM_SEND_NEXT) load_next(&sender);
        else assert(action == XMODEM_SEND_AGAIN);
        event = deliver(&receiver, &sender, !corrupted && blocks == 1);
        if (!corrupted && blocks == 1) {
            corrupted = 1;
            assert(event == XMODEM_EVENT_SEND_NAK);
            action = xmodem_send_receive(&sender, XMODEM_NAK);
            continue;
        }
        if (event == XMODEM_EVENT_BLOCK_READY) {
            memcpy(received + stored, block, xmodem_current_block_size(&receiver));
            stored += xmodem_current_block_size(&receiver);
            xmodem_accept_block(&receiver);
            ++blocks;
            if (blocks == 2 && !lost) {
                /* The ACK is lost: the sender times out and repeats; the
                 * receiver ACKs the duplicate without storing it twice. */
                lost = 1;
                assert(xmodem_send_timeout(&sender) == XMODEM_SEND_AGAIN);
                assert(deliver(&receiver, &sender, 0) == XMODEM_EVENT_SEND_ACK);
            }
            action = xmodem_send_receive(&sender, XMODEM_ACK);
        } else if (event == XMODEM_EVENT_SEND_NAK) {
            /* The receiver NAKs the first EOT. */
            assert(xmodem_send_at_end(&sender));
            action = xmodem_send_receive(&sender, XMODEM_NAK);
            assert(action == XMODEM_SEND_AGAIN && sender.errors == 0);
        } else {
            assert(event == XMODEM_EVENT_COMPLETE);
            assert(xmodem_send_receive(&sender, XMODEM_ACK) == XMODEM_SEND_COMPLETE);
            break;
        }
    }
    assert(sender.sent == file_size);
    assert(!memcmp(received, file_data, file_size));
    /* Padding: SUB up to the block boundary. */
    for (i = file_size; i < stored; ++i) assert(received[i] == XMODEM_SUB);
    /* 1K CRC: 2 x 1024 + a 452-byte tail in 1024 (more than 128) = 3072. */
    assert(stored == (one_k && !checksum ? 3072 : 2560));
}
static void test_sender(void)
{
    struct xmodem_sender sender;
    loopback(0, 0);
    loopback(1, 0);
    loopback(1, 1);  /* checksum receivers never get 1K blocks */
    /* Cancel needs two CANs; ten errors in a row give up. */
    xmodem_send_init(&sender, 0);
    assert(xmodem_send_receive(&sender, XMODEM_CRC_REQUEST) == XMODEM_SEND_NEXT);
    xmodem_send_load(&sender, packet, 10);
    assert(sender.block_size == XMODEM_BLOCK_SIZE && packet[10] == XMODEM_SUB);
    assert(xmodem_send_receive(&sender, XMODEM_CAN) == XMODEM_SEND_NONE);
    assert(xmodem_send_receive(&sender, XMODEM_NAK) == XMODEM_SEND_AGAIN);
    assert(xmodem_send_receive(&sender, XMODEM_CAN) == XMODEM_SEND_NONE);
    assert(xmodem_send_receive(&sender, XMODEM_CAN) == XMODEM_SEND_CANCELLED);
    assert(xmodem_send_receive(&sender, XMODEM_ACK) == XMODEM_SEND_NONE);
    xmodem_send_init(&sender, 1);
    xmodem_send_receive(&sender, XMODEM_CRC_REQUEST);
    xmodem_send_load(&sender, packet, 200);
    assert(sender.block_size == XMODEM_1K_BLOCK_SIZE);
    {
        unsigned k;
        for (k = 1; k < XMODEM_MAX_ERRORS; ++k) assert(xmodem_send_timeout(&sender) == XMODEM_SEND_AGAIN);
        assert(xmodem_send_timeout(&sender) == XMODEM_SEND_ERROR);
    }
    /* An empty file is just EOT. */
    xmodem_send_init(&sender, 0);
    assert(xmodem_send_receive(&sender, XMODEM_CRC_REQUEST) == XMODEM_SEND_NEXT);
    assert(!xmodem_send_load(&sender, packet, 0) && xmodem_send_at_end(&sender));
    assert(xmodem_send_receive(&sender, XMODEM_ACK) == XMODEM_SEND_COMPLETE);
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
    test_sender();
    puts("XMODEM-CRC core and sender tests: PASS");
    return 0;
}
