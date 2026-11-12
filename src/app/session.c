#include <conio.h>
#include <string.h>
#include "session.h"
#include "telnet.h"
#include "platform.h"
#include "keyboard.h"
#include "ansi_terminal.h"

#ifdef T80_APP
/* Part of the uiterm overlay (overlay.h): only needed during a session. */
#pragma code(termcode)
#pragma data(termdata)
#endif
#pragma bss(lowbss)
static uint8_t outgoing[1024], incoming[128];
static char end_status[80];
#pragma bss(bss)
static uint16_t out_length, out_offset;
static uint8_t out_overflow;
static enum telnet_profile profile;

/* Status text at column 5, inside the UI's panels (see draw.h). */
static void line(uint8_t row, const char *s)
{
    uint8_t col = 0;
    gotoxy(5, row);
    while (s[col] && col < 71) putch(s[col++]);
    while (col++ < 71) putch(' ');
}
static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
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
/* Returns 0 only if the user cancelled; otherwise the open has finished,
 * successfully or not (see backend->state()). */
static uint8_t wait_ready(const struct net_backend *backend)
{
    int state;
    for (;;) {
        backend->poll();
        state = backend->state();
        if (state != NET_INITIALIZING && state != NET_CONNECTING) return 1;
        if (cancelled(getchx())) return 0;
    }
}
static void shutdown(const struct net_backend *backend)
{
    backend->close();
    if (backend->state() == NET_CLOSING) {
        line(22, "Closing connection...");
        while (backend->state() == NET_CLOSING) backend->poll();
    }
}
static void copy_text(char *to, const char *from)
{
    strncpy(to, from, 79);
    to[79] = 0;
}
static void terminal(const struct net_backend *backend, struct session_result *result)
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
        result->outcome = SESSION_ENDED;
        copy_text(result->message, "Could not load CP437 font from the boot disk.");
        result->detail[0] = 0;
        result->keys = result->sent = 0;
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
    result->outcome = SESSION_ENDED;
    copy_text(result->message, out_overflow ? "Transmit queue overflow; session stopped." :
              disconnected ? "Disconnected." : end_status);
    copy_text(result->detail, backend->state() == NET_FAILED ? backend->status : "");
    result->keys = key_count;
    result->sent = tx_count;
}
__noinline void session_run(const struct net_backend *backend, const struct connect_request *request,
                             struct session_result *result)
{
    profile = request->profile;
    result->outcome = SESSION_NONE;
    /* The UI has drawn the "Connecting" screen. Some transports (WiC64)
     * open TCP in one firmware request, so it is visible during the wait. */
    if (backend->connect(request->hostname, request->port) < 0) {
        result->outcome = SESSION_CONNECT_FAILED;
        copy_text(result->message, backend->status);
        shutdown(backend);
        return;
    }
    line(SESSION_STATUS_ROW, backend->status);
    if (wait_ready(backend)) {
        if (backend->state() == NET_CONNECTED) terminal(backend, result);
        else {
            result->outcome = SESSION_CONNECT_FAILED;
            copy_text(result->message, backend->status);
        }
    }
    shutdown(backend);
}
#ifdef T80_APP
#pragma code(code)
#pragma data(data)
#endif
