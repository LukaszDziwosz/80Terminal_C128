#include <string.h>
#include "network.h"
#include "platform.h"
#include "bridge.h"

#pragma code(wiccode)

/* $8200-$8fff is high RAM reserved for WiC64 transfers. The resident program
 * ends its heap at $8000 and reserves only $8000-$81ff for its runtime stack. */
#define WORD_AT(address) (*(volatile uint16_t *)(address))
#define HOST ((volatile char *)WIC64_MAILBOX_HOST)
#define TX ((volatile uint8_t *)WIC64_MAILBOX_TX)
#define RX ((volatile uint8_t *)WIC64_MAILBOX_RX)
#define ERROR_TEXT ((volatile char *)WIC64_MAILBOX_ERROR)

#pragma data(wicasm)
static volatile uint8_t wic64_bridge_image[] = {
    #embed "../../../build/wic64bridge.bin"
};
#pragma data(wicdata)

/* $9000 is the bridge jump table. Return A through Oscar64's accumulator. */
uint8_t wic64_detect_bridge(void)
{
    return __asm volatile { jsr $9000; sta accu; lda #0; sta accu + 1; };
}
uint8_t wic64_open_bridge(void)
{
    return __asm volatile { jsr $9003; sta accu; lda #0; sta accu + 1; };
}
uint8_t wic64_available_bridge(void)
{
    return __asm volatile { jsr $9006; sta accu; lda #0; sta accu + 1; };
}
uint8_t wic64_read_bridge(void)
{
    return __asm volatile { jsr $9009; sta accu; lda #0; sta accu + 1; };
}
uint8_t wic64_write_bridge(void)
{
    return __asm volatile { jsr $900c; sta accu; lda #0; sta accu + 1; };
}
uint8_t wic64_close_bridge(void)
{
    return __asm volatile { jsr $900f; sta accu; lda #0; sta accu + 1; };
}
uint8_t wic64_status_message_bridge(void)
{
    return __asm volatile { jsr $9012; sta accu; lda #0; sta accu + 1; };
}

static enum net_state connection;
static uint16_t rx_length, rx_offset;
static uint16_t available_after;
static uint8_t available_failures;
static uint8_t device_ready;
static char message[74];

static void set_message(const char *text)
{
    uint8_t i = 0;
    while (text[i] && i < sizeof(message) - 1) { message[i] = text[i]; ++i; }
    message[i] = 0;
}
static void bridge_error(uint8_t status, const char *operation)
{
    if (status == WIC64_BRIDGE_TIMEOUT) {
        if (operation[0] == 'o') set_message("WiC64 TCP OPEN: user-port transfer timed out.");
        else if (operation[0] == 'a') set_message("WiC64 TCP AVAILABLE: user-port transfer timed out.");
        else if (operation[0] == 'r') set_message("WiC64 TCP READ: user-port transfer timed out.");
        else if (operation[0] == 'w') set_message("WiC64 TCP WRITE: user-port transfer timed out.");
        else if (operation[0] == 'c') set_message("WiC64 TCP CLOSE: user-port transfer timed out.");
        else set_message("WiC64 detect: user-port transfer timed out.");
    }
    else if (status == WIC64_BRIDGE_TRUNCATED) set_message("WiC64 reply exceeded the 2560-byte terminal buffer.");
    else if (status == WIC64_BRIDGE_LEGACY) set_message("WiC64 firmware 2.0.0 or newer is required.");
    else if (status) {
        uint8_t speed, result, i = 0;
        speed = platform_slow();
        result = wic64_status_message_bridge();
        platform_restore_speed(speed);
        if (!result && ERROR_TEXT[0]) {
            /* Preserve the status byte: WiC64's status text can be SUCCESS
             * after it has serviced this follow-up request. */
            while (operation[i] && i < 10) {
                char c = operation[i];
                message[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
                ++i;
            }
            message[i++] = ' '; message[i++] = 'S'; message[i++] = 'T';
            message[i++] = 'A'; message[i++] = 'T'; message[i++] = 'U';
            message[i++] = 'S'; message[i++] = ' '; message[i++] = (char)('0' + status);
            message[i++] = ':'; message[i++] = ' ';
            {
                uint8_t text = 0;
                while (ERROR_TEXT[text] && i < sizeof(message) - 1) {
                    message[i++] = ERROR_TEXT[text++];
                }
            }
            message[i] = 0;
        } else if (status == 2) set_message("WiC64 TCP client error: invalid host or port.");
        else if (status == 3) set_message("WiC64 connection error: WiFi or IP is unavailable.");
        else if (status == 4) set_message("WiC64 network error: TCP host refused or did not respond.");
        else set_message("WiC64 rejected the TCP request.");
        (void)operation;
    }
}
static int initialize(void)
{
    uint8_t speed = platform_slow();
    uint8_t result;
    device_ready = 0;
    /* This volatile read retains the embedded fixed-address bridge. */
    if (wic64_bridge_image[0] != 0x4c)
        result = WIC64_BRIDGE_TRUNCATED;
    else result = wic64_detect_bridge();
    platform_restore_speed(speed);
    rx_length = rx_offset = 0;
    available_failures = 0;
    if (result) {
        bridge_error(result, "detect");
        connection = NET_FAILED;
        return result == WIC64_BRIDGE_TIMEOUT ? NET_NO_DEVICE : NET_IO_ERROR;
    }
    connection = NET_CLOSED;
    device_ready = 1;
    set_message("WiC64 ready for direct TCP connections.");
    return NET_OK;
}
static int connect_to(const char *hostname, uint16_t port)
{
    uint8_t i = 0, digits, start;
    uint16_t value = port;
    uint8_t speed, result;
    if (connection != NET_CLOSED || !hostname || !hostname[0] || !port) return NET_BAD_ARGUMENT;
    HOST[0] = '~';
    if (HOST[0] != '~') {
        set_message("WiC64 mailbox RAM is not writable in this C128 mapping.");
        connection = NET_FAILED;
        return NET_IO_ERROR;
    }
    while (hostname[i]) {
        if (i == 64 || hostname[i] <= ' ' || (uint8_t)hostname[i] > 126) return NET_BAD_ARGUMENT;
        HOST[i] = hostname[i]; ++i;
    }
    HOST[i++] = ':';
    start = i;
    do { HOST[i++] = (char)('0' + value % 10); value /= 10; } while (value);
    digits = (uint8_t)(i - start);
    while (digits > 1) {
        char swap = HOST[start]; HOST[start] = HOST[(uint8_t)(start + digits - 1)];
        HOST[(uint8_t)(start + digits - 1)] = swap; ++start; digits -= 2;
    }
    HOST[i] = 0;
    speed = platform_slow();
    /* The ESP socket outlives the loaded overlay. Close any previous session
     * before opening, including one left behind by a failed transfer. */
    result = wic64_close_bridge();
    if (result) {
        platform_restore_speed(speed);
        bridge_error(result, "close"); connection = NET_FAILED; return NET_IO_ERROR;
    }
    rx_length = rx_offset = 0;
    result = wic64_open_bridge();
    platform_restore_speed(speed);
    if (result) {
        bridge_error(result, "open"); connection = NET_FAILED; return NET_IO_ERROR;
    }
    connection = NET_CONNECTED;
    /* TCP_OPEN may return before the ESP has promoted its socket to readable. */
    available_after = (uint16_t)(platform_ticks() + 30);
    available_failures = 0;
    set_message("Connected.");
    return NET_OK;
}
static void poll(void)
{
    uint8_t speed, result;
    uint16_t available = 0;
    if (connection != NET_CONNECTED || rx_offset != rx_length) return;
    if ((int16_t)(platform_ticks() - available_after) < 0) return;
    speed = platform_slow();
    result = wic64_available_bridge();
    if (!result) {
        available = WORD_AT(WIC64_MAILBOX_AVAILABLE);
        if (available) result = wic64_read_bridge();
    }
    platform_restore_speed(speed);
    if (result && !available) {
        /* Retry transient firmware statuses briefly after TCP_OPEN instead
         * of abandoning a socket while it settles. */
        available_after = (uint16_t)(platform_ticks() + 6);
        if (++available_failures <= 10) return;
        bridge_error(result, "available");
        connection = NET_FAILED;
        return;
    }
    if (result) { bridge_error(result, "read"); connection = NET_FAILED; return; }
    available_failures = 0;
    available_after = (uint16_t)(platform_ticks() + 1);
    if (available) {
        rx_length = WORD_AT(WIC64_MAILBOX_RX_LENGTH);
        rx_offset = 0;
    }
}
static int receive(uint8_t *buffer, uint16_t capacity)
{
    uint16_t count = rx_length - rx_offset, i;
    if (count > capacity) count = capacity;
    for (i = 0; i < count; ++i) buffer[i] = RX[rx_offset + i];
    rx_offset += count;
    return (int)count;
}
static int transmit(const uint8_t *buffer, uint16_t length)
{
    uint16_t i;
    uint8_t speed, result;
    if (connection != NET_CONNECTED) return NET_IO_ERROR;
    if (length > 256) length = 256;
    for (i = 0; i < length; ++i) TX[i] = buffer[i];
    WORD_AT(WIC64_MAILBOX_TX_LENGTH) = length;
    speed = platform_slow();
    result = wic64_write_bridge();
    platform_restore_speed(speed);
    if (result) { bridge_error(result, "write"); connection = NET_FAILED; return NET_IO_ERROR; }
    return (int)length;
}
static int state(void) { return (int)connection; }
static void close_connection(void)
{
    uint8_t speed;
    if (connection == NET_CLOSED) return;
    if (device_ready) {
        speed = platform_slow();
        (void)wic64_close_bridge();
        platform_restore_speed(speed);
    }
    connection = NET_CLOSED;
    rx_length = rx_offset = 0;
}
static struct net_backend backend = {
    "WiC64", message, initialize, connect_to, poll,
    receive, transmit, state, close_connection
};
__noinline const struct net_backend *wic64_backend(void) { return &backend; }
#pragma code(code)
#pragma data(data)
