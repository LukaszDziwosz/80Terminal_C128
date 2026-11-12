#include <conio.h>
#include <string.h>
#include "session.h"
#include "telnet.h"
#include "platform.h"
#include "keyboard.h"
#include "ansi_terminal.h"

#pragma bss(lowbss)
static uint8_t outgoing[1024], incoming[128];
static char end_status[80];
#pragma bss(bss)
static uint16_t out_length, out_offset;
static uint8_t out_overflow;
static enum telnet_profile profile;

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
static uint8_t wait_key(const struct net_backend *backend)
{
    while (!platform_key_pending()) backend->poll();
    return getch();
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
static void ansi_send_terminal(const uint8_t *data, uint8_t length)
{
    while (length--) telnet_send_byte(*data++);
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
/* "Keys read: n   TCP bytes sent (incl. Telnet): n" without sprintf. */
static char *decimal(char *out, uint16_t value)
{
    char digits[5];
    uint8_t n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    return out;
}
static void counters(uint16_t keys, uint16_t sent)
{
    char text[60], *out = text;
    const char *s;
    for (s = "Keys read: "; *s; ) *out++ = *s++;
    out = decimal(out, keys);
    for (s = "   TCP bytes sent (incl. Telnet): "; *s; ) *out++ = *s++;
    out = decimal(out, sent);
    *out = 0;
    line(7, text);
}
static void terminal(const struct net_backend *backend)
{
    int count, sent;
    uint16_t i;
    uint8_t key, value, petscii_escape = 0;
    uint8_t encoded[5], key_length, k;
    uint16_t key_count = 0, tx_count = 0;
    uint16_t last_receive = 0;
    uint8_t cursor_pending = 0;
    uint8_t disconnected = 0;
    uint8_t ascii_mode = telnet_profile_is_ansi(profile);
    out_length = out_offset = out_overflow = 0;
    telnet_init(profile, enqueue);
    if (ascii_mode && !ansi_terminal_init(ansi_send_terminal, platform_device(), telnet_profile_is_vt100(profile))) {
        clrscr();
        line(5, "Could not load CP437 font from the boot disk.");
        getch();
        return;
    }
    if (!ascii_mode) {
        clrscr();
        line(0, "Connected - F7 disconnects. CTRL-C goes to the host.");
        gotoxy(0, 2);
    }
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
        if (ascii_mode && count) ansi_terminal_begin_update();
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
        if (ascii_mode && count) {
            last_receive = platform_ticks();
            cursor_pending = 1;
        } else if (ascii_mode && cursor_pending &&
                   (uint16_t)(platform_ticks() - last_receive) >= 8) {
            ansi_terminal_end_update();
            cursor_pending = 0;
        }
        key = platform_key_raw();
        /* RUN/STOP and CTRL-C both produce $03 through GETIN. Reserve
         * only F7 locally so remote interrupt characters survive. */
        if (key == PETSCII_F7) { disconnected = 1; break; }
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
    strncpy(end_status, backend->status, sizeof(end_status) - 1);
    end_status[sizeof(end_status) - 1] = 0;
    shutdown(backend);
    line(22, "");
    line(5, out_overflow ? "Transmit queue overflow; session stopped." :
         disconnected ? "Disconnected." : end_status);
    if (backend->state() == NET_FAILED) line(9, backend->status);
    counters(key_count, tx_count);
    line(20, "Press any key to return to the main screen.");
    line(21, "Then press F3 for a new connection, or F1 if the interface failed.");
    wait_key(backend);
}
void session_run(const struct net_backend *backend, const struct connect_request *request)
{
    profile = request->profile;
    /* Some transports (WiC64) perform TCP open as one firmware request.
     * Draw this before starting it so a DNS or remote-connect wait is visible. */
    clrscr();
    line(2, backend->name);
    line(5, "Opening connection...");
    line(7, "Resolving host and waiting for the remote server.");
    line(18, "WiC64 opens are synchronous; this can take several seconds.");
    if (backend->connect(request->hostname, request->port) < 0) {
        line(10, backend->status);
        line(20, "Press any key to return to the main screen.");
        wait_key(backend); shutdown(backend); return;
    }
    clrscr();
    line(5, backend->status);
    line(18, "RUN/STOP or F7 cancels; pending open is closed when it completes.");
    if (wait_ready(backend) && backend->state() == NET_CONNECTED) terminal(backend);
    else line(5, backend->status);
    shutdown(backend);
}
