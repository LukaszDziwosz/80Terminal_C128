/* The uimain overlay: launcher, main screen and connect form. */
#ifdef PROGRAM_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#endif
#include <string.h>
#include "ui.h"
#include "platform.h"
#include "version.h"

#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma code(uicode)
#pragma data(uidata)
#pragma bss(uibss)
#endif
#ifdef __OSCAR64C__
#define UI_ENTRY __noinline
#else
#define UI_ENTRY
#endif

/* uibss lives outside the overlay file, so these keep their values while
 * another overlay uses the slot. ui_reset() sets them before first use. */
static uint8_t ready, initialized;
static char port_text[6];
static char status[80];

static void line(uint8_t row, const char *text)
{
    uint8_t col = 0;
    gotoxy(3, row);
    while (*text && col < 73) { putch(*text++); ++col; }
    while (col++ < 73) putch(' ');
}
static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
}
static uint8_t wait_key(const struct net_backend *backend)
{
    while (!platform_key_pending()) backend->poll();
    return (uint8_t)getch();
}
static void remember_status(const struct net_backend *backend)
{
    strncpy(status, backend->status, sizeof(status) - 1);
    status[sizeof(status) - 1] = 0;
}
static void show_status_change(const struct net_backend *backend, uint8_t row)
{
    if (strncmp(status, backend->status, sizeof(status) - 1)) {
        line(row, backend->status);
        remember_status(backend);
    }
}

UI_ENTRY uint8_t ui_launcher(void)
{
    uint8_t key;
    clrscr();
    line(2, "80Terminal " APP_VERSION " / Launcher");
    line(4, "Commodore 128 - 80 columns");
    line(7, "F1   RR-Net");
    line(9, "F3   1541 Ultimate II+");
    line(11, "F5   WiC64");
    line(17, "F8   Exit to BASIC");
    line(21, "All adapters: direct TCP. RR-Net uses DHCP; DNS 1.1.1.1.");
    for (;;) {
        key = (uint8_t)getch();
        if (key == PETSCII_F1) return 1;
        if (key == PETSCII_F3) return 2;
        if (key == PETSCII_F5) return 3;
        if (key == PETSCII_F8) return 0;
    }
}

UI_ENTRY void ui_reset(void)
{
    ready = initialized = 0;
    status[0] = 0;
}

static void draw_main(const struct net_backend *backend)
{
    clrscr();
    line(2, "80Terminal " APP_VERSION " / MAIN");
    line(5, "Network interface:");
    line(6, backend->name);
    line(9, ready ? "Ready for a connection." : "Interface unavailable. F1 retries initialization.");
    line(11, backend->status);
    line(16, "F3  Terminal / connect");
    line(23, "F1 Retry interface     F3 Terminal     F8 Quit");
}
static uint8_t initialize(const struct net_backend *backend)
{
    int result;
    line(9, "Initializing interface...");
    result = backend->init();
    line(11, backend->status);
    remember_status(backend);
    if (result < 0) return 0;
    /* NET_PENDING means the IO2 reset and network queries still need polling.
     * Each backend enforces its own command deadlines. */
    while (backend->state() == NET_INITIALIZING) {
        backend->poll();
        show_status_change(backend, 11);
    }
    return backend->state() == NET_CLOSED;
}
static uint8_t edit(const struct net_backend *backend, uint8_t row,
                    char *text, uint8_t capacity, uint8_t digits)
{
    uint8_t length = (uint8_t)strlen(text), key;
    for (;;) {
        line(row, text);
        gotoxy((uint8_t)(3 + length), row); putch('_');
        key = wait_key(backend);
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
/* The adapter may still be finishing a previous close or a DHCP renewal. */
static uint8_t wait_ready(const struct net_backend *backend)
{
    int state;
    for (;;) {
        backend->poll();
        state = backend->state();
        if (state != NET_INITIALIZING && state != NET_CONNECTING) break;
        if (platform_key_pending() && cancelled((uint8_t)getch())) return 0;
    }
    return state == NET_CLOSED;
}
/* Hostname, port and terminal type. Returns 1 with request filled in. */
static uint8_t connect_form(const struct net_backend *backend, struct connect_request *request)
{
    uint32_t port;
    uint8_t i, key;
    /* A new transport session starts with an empty host. Do not retain an
     * earlier BBS address in application RAM after returning to the launcher. */
    request->hostname[0] = 0;
    port_text[0] = '2'; port_text[1] = '3'; port_text[2] = 0;
    clrscr();
    line(2, backend->name);
    line(5, backend->status);
    line(18, "RUN/STOP or F7 cancels.");
    if (!wait_ready(backend)) return 0;
    for (;;) {
        clrscr();
        line(2, backend->name);
        line(4, "Hostname or IPv4 address:");
        line(8, "Port (1-65535):");
        line(18, "RETURN accepts each field. F7 cancels.");
        if (!edit(backend, 5, request->hostname, 64, 0) ||
            !edit(backend, 9, port_text, 5, 1)) return 0;
        port = 0;
        for (i = 0; port_text[i]; ++i) port = port * 10 + (uint8_t)(port_text[i] - '0');
        if (port > 0 && port <= 65535) break;
        line(14, "Invalid port. Press a key to try again."); wait_key(backend);
    }
    request->port = (uint16_t)port;
    line(12, "1  ANSI 80x25       2  PETSCII 80x25       3  VT100 80x24");
    line(14, "ANSI: CP437/color. VT100: DEC graphics and terminal controls.");
    line(16, "During a session: F7 disconnects; CTRL-C goes to the host.");
    do { key = wait_key(backend); if (cancelled(key)) return 0; }
    while (key != '1' && key != '2' && key != '3');
    request->profile = key == '1' ? TELNET_PROFILE_ANSI_80 :
                       key == '2' ? TELNET_PROFILE_PETSCII_80 : TELNET_PROFILE_VT100_80;
    return 1;
}
static void close_backend(const struct net_backend *backend)
{
    backend->close();
    while (backend->state() == NET_CLOSING) backend->poll();
}

UI_ENTRY uint8_t ui_main(const struct net_backend *backend, struct connect_request *request)
{
    uint8_t key;
    if (!initialized) {
        initialized = 1;
        draw_main(backend);
        ready = initialize(backend);
    }
    draw_main(backend);
    remember_status(backend);
    for (;;) {
        /* Keep DHCP lease timers and ARP alive while waiting at the menu. */
        backend->poll();
        if (ready != (backend->state() == NET_CLOSED)) {
            ready = backend->state() == NET_CLOSED;
            draw_main(backend);
        }
        show_status_change(backend, 11);
        if (!platform_key_pending()) continue;
        key = (uint8_t)getch();
        if (key == PETSCII_F8) return UI_QUIT;
        if (key == PETSCII_F1) {
            close_backend(backend);
            ready = initialize(backend);
        } else if (key == PETSCII_F3 && ready) {
            if (connect_form(backend, request)) return UI_CONNECT;
            /* Cancelled: leave the adapter closed, as before. */
            close_backend(backend);
        } else continue;
        draw_main(backend);
    }
}

#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
