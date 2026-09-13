#include <string.h>
#include "network.h"
#include "uci.h"
#ifndef ULTIMATE_TEST
#include "platform.h"
#pragma code(ultcode)
#pragma data(ultdata)
#endif

#ifdef __OSCAR64C__
#define NET_ENTRY __noinline
#else
#define NET_ENTRY
#endif

static enum net_state connection;
static uint8_t operation, socket_id, have_socket, closing;
static uint8_t tx[UCI_PAYLOAD], rx[UCI_PAYLOAD], command[UCI_PAYLOAD + 3];
static uint16_t tx_length, tx_offset, rx_length, rx_offset, tx_started;
static char message[UCI_STATUS_SIZE];
enum { RESET, IDENTIFY, ADDRESS, NONE, OPEN, READ_SOCKET, WRITE_SOCKET, CLOSE_SOCKET, CLOSE_RESET };

static void set_message(const char *text)
{
    uint8_t i = 0;
    while (text[i] && i < sizeof(message) - 1) { message[i] = text[i]; ++i; }
    message[i] = 0;
}
static void failure(const char *text)
{
    set_message(text);
    connection = NET_FAILED;
    operation = NONE;
}
static uint8_t start(uint8_t op, uint16_t size, uint16_t timeout)
{
    if (!uci_start(command, size, timeout)) {
        failure("UCI is busy or unavailable. Check Ultimate connection.");
        return 0;
    }
    operation = op;
    return 1;
}
static int initialize_inner(void)
{
    connection = NET_INITIALIZING;
    have_socket = closing = 0;
    tx_length = rx_length = tx_offset = rx_offset = 0;
    if (!uci_present()) uci_enable();
    if (!uci_present()) {
        failure("Ultimate UCI not detected after software unlock.");
        return NET_NO_DEVICE;
    }
    set_message("Checking Ultimate network interface...");
    uci_reset(); operation = RESET;
    return NET_PENDING;
}
static int connect_inner(const char *hostname, uint16_t port)
{
    uint8_t n = 0;
    if (connection != NET_CLOSED) return NET_IO_ERROR;
    if (!hostname || !hostname[0] || !port) return NET_BAD_ARGUMENT;
    while (hostname[n]) {
        if (n == 64 || hostname[n] <= ' ' || (uint8_t)hostname[n] > 126)
            return NET_BAD_ARGUMENT;
        command[4 + n] = (uint8_t)hostname[n];
        ++n;
    }
    command[0] = 3; command[1] = 7;
    command[2] = (uint8_t)port; command[3] = (uint8_t)(port >> 8);
    command[4 + n] = 0;
    if (!start(OPEN, n + 5, 1800)) return NET_IO_ERROR;
    connection = NET_CONNECTING;
    set_message("Resolving host / connecting (up to 30 seconds)...");
    return NET_PENDING;
}
static void start_close(void)
{
    tx_length = rx_length = 0;
    if (!have_socket) { connection = NET_CLOSED; operation = NONE; return; }
    command[0] = 3; command[1] = 9; command[2] = socket_id;
    start(CLOSE_SOCKET, 3, 300);
}
static void completed(void)
{
    uint8_t code = uci_status_code();
    uint16_t count;
    uint8_t finished = operation;
    operation = NONE;
    if (finished == RESET || finished == CLOSE_RESET) {
        if (finished == CLOSE_RESET || closing) { start_close(); return; }
        command[0] = 3; command[1] = 1;
        start(IDENTIFY, 2, 300);
        return;
    }
    if (finished == READ_SOCKET && code == 2) {
        /* Firmware reports recv() EAGAIN as 02,NO DATA: 11, with FFFF count.
         * Other errno values must not leave an invalid socket polling forever. */
        if (strstr(uci_status, ": 11") == 0) {
            failure(uci_status);
            return;
        }
    } else if (finished == READ_SOCKET && code == 1) {
        have_socket = 0; /* firmware closes on recv() == 0 */
        connection = closing ? NET_CLOSED : NET_EOF;
        set_message("Connection closed by remote host.");
        return;
    } else if (code != 0) {
        failure(code == 255 ? "Malformed UCI status response." : uci_status);
        return;
    } else {
        switch (finished) {
        case IDENTIFY:
            if (!uci_reply_length) { failure("Empty network identification."); return; }
            command[0] = 3; command[1] = 5; command[2] = 0;
            start(ADDRESS, 3, 300);
            return;
        case ADDRESS:
            if (uci_reply_length != 12) { failure("Malformed network address response."); return; }
            if (!(uci_reply[0] | uci_reply[1] | uci_reply[2] | uci_reply[3])) {
                failure("Ultimate has no IP address. Check its network settings."); return;
            }
            connection = NET_CLOSED;
            set_message("Ultimate ready for direct TCP connections.");
            break;
        case OPEN:
            if (uci_reply_length != 1) { failure("Malformed TCP open response."); return; }
            socket_id = uci_reply[0]; have_socket = 1;
            connection = closing ? NET_CLOSING : NET_CONNECTED;
            set_message("Connected.");
            break;
        case READ_SOCKET:
            if (uci_reply_length < 2) { failure("Short TCP read response."); return; }
            count = uci_reply[0] | ((uint16_t)uci_reply[1] << 8);
            if (count > UCI_PAYLOAD || uci_reply_length != count + 2) {
                failure("Invalid TCP read length."); return;
            }
            memcpy(rx, uci_reply + 2, count);
            rx_offset = 0; rx_length = count;
            break;
        case WRITE_SOCKET:
            if (uci_reply_length != 2) { failure("Short TCP write response."); return; }
            count = uci_reply[0] | ((uint16_t)uci_reply[1] << 8);
            if (count > tx_length - tx_offset) { failure("Invalid TCP write count."); return; }
            tx_offset += count;
            if (tx_offset == tx_length) tx_offset = tx_length = 0;
            break;
        case CLOSE_SOCKET:
            have_socket = 0;
            connection = NET_CLOSED;
            return;
        default: failure("Unexpected UCI operation."); return;
        }
    }
    if (closing) start_close();
}
static void poll_inner(void)
{
    enum uci_result result;
    uint16_t n;
    if (operation != NONE) {
        result = uci_poll();
        if (result == UCI_TIMEOUT) { failure("Ultimate command timed out."); return; }
        if (result == UCI_ERROR) { failure("Ultimate protocol error or oversized reply."); return; }
        if (result == UCI_DONE) completed();
        return;
    }
    if (connection != NET_CONNECTED) return;
    command[0] = 3;
    command[2] = socket_id;
    if (tx_length) {
        if ((uint16_t)(uci_ticks() - tx_started) >= 600) {
            failure("TCP send stalled for 10 seconds."); return;
        }
        command[1] = 0x11;
        n = tx_length - tx_offset;
        memcpy(command + 3, tx + tx_offset, n);
        start(WRITE_SOCKET, n + 3, 300);
    } else if (rx_offset == rx_length) {
        command[1] = 0x10;
        command[3] = 0; command[4] = 2; /* 512 bytes, within one legacy reply */
        start(READ_SOCKET, 5, 300);
    }
}
static NET_ENTRY int receive(uint8_t *buffer, uint16_t capacity)
{
    uint16_t n = rx_length - rx_offset;
    if (connection == NET_FAILED) return NET_IO_ERROR;
    if (n > capacity) n = capacity;
    if (n) { memcpy(buffer, rx + rx_offset, n); rx_offset += n; }
    return (int)n;
}
static NET_ENTRY int transmit(const uint8_t *buffer, uint16_t length)
{
    if (connection != NET_CONNECTED) return NET_IO_ERROR;
    if (tx_length) return 0;
    if (length > UCI_PAYLOAD) length = UCI_PAYLOAD;
    if (length) memcpy(tx, buffer, length);
    tx_length = length; tx_offset = 0; tx_started = uci_ticks();
    return (int)length;
}
static NET_ENTRY int state(void) { return (int)connection; }
static void close_inner(void)
{
    if (connection == NET_CLOSING) return;
    closing = 1;
    if (connection == NET_FAILED) {
        if (!uci_present()) { connection = NET_CLOSED; return; }
        uci_reset(); operation = CLOSE_RESET;
    }
    connection = NET_CLOSING;
    if (operation == NONE) start_close();
}
/* Limit cartridge accesses to 1 MHz until the real C128 timing is measured.
 * Restore 2 MHz before returning to the renderer or polling the keyboard. */
static NET_ENTRY int initialize(void)
{
#ifndef ULTIMATE_TEST
    uint8_t speed = platform_slow();
#endif
    int result = initialize_inner();
#ifndef ULTIMATE_TEST
    platform_restore_speed(speed);
#endif
    return result;
}
static NET_ENTRY int connect_to(const char *hostname, uint16_t port)
{
#ifndef ULTIMATE_TEST
    uint8_t speed = platform_slow();
#endif
    int result = connect_inner(hostname, port);
#ifndef ULTIMATE_TEST
    platform_restore_speed(speed);
#endif
    return result;
}
static NET_ENTRY void poll(void)
{
#ifndef ULTIMATE_TEST
    uint8_t speed = platform_slow();
#endif
    poll_inner();
#ifndef ULTIMATE_TEST
    platform_restore_speed(speed);
#endif
}
static NET_ENTRY void close_connection(void)
{
#ifndef ULTIMATE_TEST
    uint8_t speed = platform_slow();
#endif
    close_inner();
#ifndef ULTIMATE_TEST
    platform_restore_speed(speed);
#endif
}
static struct net_backend backend = {
    "1541 Ultimate II+", message, initialize, connect_to,
    poll, receive, transmit, state, close_connection
};
#ifndef ULTIMATE_TEST
__noinline
#endif
const struct net_backend *ultimate_backend(void)
{
    backend.init = initialize;
    backend.connect = connect_to;
    backend.poll = poll;
    backend.read = receive;
    backend.write = transmit;
    backend.state = state;
    backend.close = close_connection;
    return &backend;
}
#ifndef ULTIMATE_TEST
#pragma code(code)
#pragma data(data)
#endif
