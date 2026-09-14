#include <conio.h>
#include <string.h>
#include "session.h"
#include "telnet.h"
#include "platform.h"
#include "keyboard.h"
#include "ansi_terminal.h"
#include <stdio.h>

static char hostname[65];
static char port_text[6] = "23";
static uint8_t outgoing[1024], incoming[128];
static uint16_t out_length, out_offset;
static uint8_t out_overflow;
static uint8_t ascii_mode = 1;

static void line(uint8_t row, const char *s)
{
    uint8_t col = 0;
    gotoxy(3, row);
    while (s[col] && col < 73) putch(s[col++]);
    while (col++ < 73) putch(' ');
}
static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
}
static uint8_t edit(uint8_t row, char *text, uint8_t capacity, uint8_t digits)
{
    uint8_t length = (uint8_t)strlen(text), key;
    for (;;) {
        line(row, text);
        gotoxy((uint8_t)(3 + length), row); putch('_');
        key = getch();
        if (cancelled(key)) return 0;
        if (key == 10 || key == 13) return length != 0;
        if (key == PETSCII_HOME) { length = 0; text[0] = 0; }
        else if (key == PETSCII_DEL) { if (length) text[--length] = 0; }
        else if (length < capacity && key >= 33 && key <= 126 &&
                 (!digits || (key >= '0' && key <= '9'))) {
            text[length++] = (char)key; text[length] = 0;
        }
    }
}
static void enqueue(const uint8_t *data, uint8_t length)
{
    if (out_offset) {
        memmove(outgoing, outgoing + out_offset, out_length - out_offset);
        out_length -= out_offset; out_offset = 0;
    }
    if (out_length + length > sizeof(outgoing)) { out_overflow = 1; return; }
    memcpy(outgoing + out_length, data, length);
    out_length += length;
}
static uint8_t wait_ready(const struct net_backend *backend)
{
    enum net_state state;
    for (;;) {
        backend->poll();
        state = backend->state();
        if (state != NET_INITIALIZING && state != NET_CONNECTING) break;
        if (cancelled(getchx())) return 0;
    }
    return state == NET_CLOSED || state == NET_CONNECTED;
}
static void shutdown(const struct net_backend *backend)
{
    backend->close();
    if (backend->state() == NET_CLOSING) {
        line(22, "Closing connection...");
        while (backend->state() == NET_CLOSING) backend->poll();
    }
}
static void terminal(const struct net_backend *backend)
{
    int count, sent;
    uint16_t i;
    uint8_t key, value, petscii_escape = 0;
    uint8_t encoded[5], key_length, k;
    uint16_t key_count = 0, tx_count = 0;
    char counters[64];
    out_length = out_offset = out_overflow = 0;
    telnet_init(ascii_mode ? TELNET_PROFILE_VT100_80 : TELNET_PROFILE_PETSCII_80, enqueue);
    if (ascii_mode && !ansi_terminal_init(enqueue, platform_device())) {
        clrscr();
        line(5, "Could not load CP437 font from the boot disk.");
        getch();
        return;
    }
    if (!ascii_mode) {
        clrscr();
        line(0, "Connected - RUN/STOP disconnects. Remote echo; no local echo.");
        gotoxy(0, 2);
    }
    telnet_startup();
    for (;;) {
        /* Send our Telnet identity before the first receive poll. This is
         * required by BBSes which gate their banner on terminal negotiation. */
        if (out_length > out_offset) {
            sent = backend->write(outgoing + out_offset, out_length - out_offset);
            if (sent < 0) break;
            tx_count += (uint16_t)sent;
            out_offset += (uint16_t)sent;
            if (out_offset == out_length) out_offset = out_length = 0;
        }
        backend->poll();
        if (backend->state() == NET_FAILED || backend->state() == NET_EOF) break;
        /* One bounded input batch leaves time for keyboard and driver work. */
        count = backend->read(incoming, sizeof(incoming));
        if (count < 0) break;
        for (i = 0; i < (uint16_t)count; ++i) {
            if (!telnet_receive(incoming[i], &value)) continue;
            if (ascii_mode) {
                ansi_terminal_receive(value);
            } else {
                /* C128 ESC X swaps displays: terminal remains 80-column only. */
                if (petscii_escape) { petscii_escape = 0; continue; }
                if (value == 27) { petscii_escape = 1; continue; }
                putrch(value);
            }
        }
        key = platform_key_raw();
        if (key == PETSCII_STOP) break;
        if (key) {
            ++key_count;
            key_length = ascii_mode ? ansi_terminal_key(key, encoded) :
                                      keyboard_encode(key, 1, encoded);
            if (key_length == 1 && encoded[0] == 13) telnet_send_enter();
            else for (k = 0; k < key_length; ++k) telnet_send_byte(encoded[k]);
        }
        if (out_overflow) break;
    }
    if (ascii_mode) ansi_terminal_shutdown();
    iocharmap(IOCHM_PETSCII_2);
    clrscr();
    line(5, out_overflow ? "Transmit queue overflow; session stopped." : backend->status);
    sprintf(counters, "Keys read: %u   TCP bytes sent (incl. Telnet): %u", key_count, tx_count);
    line(7, counters);
}
void session_open(const struct net_backend *backend)
{
    uint32_t port;
    uint8_t i, key;
    /* A new transport session starts with an empty host. Do not retain an
     * earlier BBS address in application RAM after returning to the launcher. */
    hostname[0] = 0;
    port_text[0] = '2'; port_text[1] = '3'; port_text[2] = 0;
    clrscr();
    line(2, backend->name);
    line(5, backend->status);
    line(18, "RUN/STOP or F7 cancels.");
    if (!wait_ready(backend)) {
        line(5, backend->status);
        shutdown(backend);
        return;
    }
    for (;;) {
        clrscr();
        line(2, backend->name);
        line(4, "Hostname or IPv4 address:");
        line(8, "Port (1-65535):");
        line(18, "RETURN accepts each field. F7 cancels.");
        if (!edit(5, hostname, 64, 0) || !edit(9, port_text, 5, 1)) {
            shutdown(backend); return;
        }
        port = 0;
        for (i = 0; port_text[i]; ++i) port = port * 10 + port_text[i] - '0';
        if (port > 0 && port <= 65535) break;
        line(14, "Invalid port. Press a key to try again."); getch();
    }
    line(12, "1  ANSI/VT100 80 columns         2  PETSCII 80/Telnet");
    line(14, "ANSI sends terminal identification, cursor and status replies.");
    do { key = getch(); if (cancelled(key)) { shutdown(backend); return; } }
    while (key != '1' && key != '2');
    ascii_mode = key == '1';
    /* Some transports (WiC64) perform TCP open as one firmware request.
     * Draw this before starting it so a DNS or remote-connect wait is visible. */
    clrscr();
    line(2, backend->name);
    line(5, "Opening connection...");
    line(7, "Resolving host and waiting for the remote server.");
    line(18, "WiC64 opens are synchronous; this can take several seconds.");
    if (backend->connect(hostname, (uint16_t)port) < 0) {
        line(10, backend->status);
        line(20, "Press any key to return to the launcher.");
        getch(); shutdown(backend); return;
    }
    clrscr();
    line(5, backend->status);
    line(18, "RUN/STOP or F7 cancels; pending open is closed when it completes.");
    if (wait_ready(backend) && backend->state() == NET_CONNECTED) terminal(backend);
    else line(5, backend->status);
    shutdown(backend);
}
