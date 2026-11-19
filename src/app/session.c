#include <conio.h>
#include <string.h>
#include "session.h"
#include "telnet.h"
#include "platform.h"
#include "keyboard.h"
#include "ansi_terminal.h"
#include "vdc_screen.h"

#ifdef T80_APP
/* Part of the uiterm overlay (overlay.h): only needed during a session. */
#pragma code(termcode)
#pragma data(termdata)
#endif
#pragma bss(lowbss)
static uint8_t incoming[128];
static char end_status[80];
#pragma bss(bss)
/* The transmit queue is the resident session_buffer (session.h): empty
 * while a transfer uses it as its block buffer. */
#define outgoing session_buffer
static uint16_t out_length, out_offset;
static uint8_t out_overflow;
static enum telnet_profile profile;
static uint8_t display_flags;
/* Kept across a transfer. */
static uint16_t key_count, tx_count, last_receive;
static uint8_t petscii_escape, cursor_pending, saved_row, saved_col;
#define PETSCII_HELP 0x84

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
/* Sends the queued bytes; returns 0 if the connection failed. */
static uint8_t send_queued(const struct net_backend *backend)
{
    int sent;
    if (out_length > out_offset) {
        sent = backend->write(outgoing + out_offset, out_length - out_offset);
        if (sent < 0) return 0;
        tx_count += (uint16_t)sent;
        out_offset += (uint16_t)sent;
        if (out_offset == out_length) out_offset = out_length = 0;
    }
    return 1;
}
/* HELP: hand the screen and connection to the transfer overlay. */
static void suspend(const struct net_backend *backend, uint8_t ascii_mode)
{
    while (out_length && send_queued(backend)) backend->poll();
    out_length = out_offset = 0;
    if (ascii_mode) ansi_terminal_suspend();
    else {
        /* The KERNAL editor's cursor line and column (TBLX, PNTR). */
        saved_row = *(volatile uint8_t *)0xeb;
        saved_col = *(volatile uint8_t *)0xec;
        screen_save_page(0x0000, 0x0800);
    }
}
static uint8_t resume(uint8_t ascii_mode)
{
    telnet_set_sender(enqueue);
    if (ascii_mode) return ansi_terminal_resume(platform_device());
    iocharmap(IOCHM_PETSCII_2);
    screen_restore_page(0x0000, 0x0800);
    gotoxy(saved_col, saved_row);
    return 1;
}
static void terminal(const struct net_backend *backend, struct session_result *result, uint8_t resuming)
{
    int count;
    uint16_t i;
    uint8_t key, value;
    uint8_t encoded[5], key_length, k;
    uint8_t disconnected = 0;
    uint8_t ascii_mode = telnet_profile_is_ansi(profile);
    if (resuming) {
        if (!resume(ascii_mode)) {
            result->outcome = SESSION_ENDED;
            copy_text(result->message, "Could not load the terminal font from the boot disk.");
            result->detail[0] = 0;
            return;
        }
    } else {
        out_length = out_offset = out_overflow = 0;
        key_count = tx_count = last_receive = 0;
        petscii_escape = cursor_pending = 0;
        telnet_init(profile, enqueue);
        if (ascii_mode && !ansi_terminal_init(ansi_send_terminal, platform_device(), display_flags)) {
            result->outcome = SESSION_ENDED;
            copy_text(result->message, "Could not load the terminal font from the boot disk.");
            result->detail[0] = 0;
            result->keys = result->sent = 0;
            return;
        }
        if (!ascii_mode) {
            clrscr();
            line(0, "Connected - F7 disconnects, HELP transfers files. CTRL-C goes to the host.");
            gotoxy(0, 2);
        }
    }
    for (;;) {
        /* Send our Telnet identity before the first receive poll. This is
         * required by BBSes which gate their banner on terminal negotiation. */
        if (!send_queued(backend)) break;
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
            ansi_terminal_flush();
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
        if (key == PETSCII_HELP) {
            suspend(backend, ascii_mode);
            result->outcome = SESSION_TRANSFER;
            return;
        }
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
                             struct session_result *result, uint8_t resuming)
{
    if (resuming) {
        terminal(backend, result, 1);
        if (result->outcome != SESSION_TRANSFER) shutdown(backend);
        return;
    }
    profile = request->profile;
    display_flags = (telnet_profile_is_vt100(profile) ? ANSI_TERMINAL_VT100 : 0) |
                    (telnet_profile_is_mono(profile) ? ANSI_TERMINAL_MONOCHROME : 0) |
                    (profile == TELNET_PROFILE_AMIGA_80 ? ANSI_TERMINAL_AMIGA : 0);
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
        if (backend->state() == NET_CONNECTED) {
            terminal(backend, result, 0);
            if (result->outcome == SESSION_TRANSFER) return;
        } else {
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
