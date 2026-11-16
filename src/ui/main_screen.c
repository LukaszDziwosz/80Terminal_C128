/* The uimain overlay: launcher, main screen, connect form, IRC server form
 * and summary. */
#ifdef PROGRAM_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#endif
#include <string.h>
#include "ui.h"
#include "platform.h"
#include "book.h"
#include "bank1.h"
#include "phonebook.h"
#include "ircconfig.h"
#include "disk.h"

#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma code(uicode)
#pragma data(uidata)
#pragma bss(uibss)
#endif
#ifdef __OSCAR64C__
/* UI speed does not matter; slot space does. */
#pragma optimize(push, size, outline)
#endif
#include "draw.h"
#include "terminal_choice.h"
/* Only the IRC form needs these checks, so they are part of this overlay. */
#include "../core/ircconfig.c"
#ifdef __OSCAR64C__
#define UI_ENTRY __noinline
#else
#define UI_ENTRY
#endif

/* Layout: labels, then values, inside the panels from draw.h. */
#define LABEL_COL 5
#define VALUE_COL 16
#define VALUE_WIDTH 60
#define HOST_ROW 6
#define PORT_ROW 8
#define PROFILE_ROW 10

/* uibss lives outside the overlay file, so these keep their values while
 * another overlay uses the slot. ui_reset() sets them before first use. */
static uint8_t ready, initialized;
static char port_text[6];
static char status[80];

static const char *const launcher_keys[] = {
    "F1", "RR-Net", "F3", "Ultimate II+", "F5", "WiC64", "F8", "BASIC", 0 };
static const char *const main_keys[] = {
    "F1", "Retry", "F2", "IRC", "F3", "Connect", "F5", "Phonebook", "1-9", "Dial", "F8", "Quit", 0 };
static const char *const edit_keys[] = {
    "RETURN", "Next", "DEL", "Erase", "HOME", "Clear", "F7", "Cancel", 0 };
static const char *const profile_keys[] = { "1-5", "Terminal", "F7", "Cancel", 0 };
static const char *const cancel_keys[] = { "F7", "Cancel", 0 };
static const char *const any_key[] = { "Any key", "Main screen", 0 };

static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
}
static uint8_t wait_key(const struct net_backend *backend)
{
    while (!platform_key_pending()) backend->poll();
    return (uint8_t)getch();
}
static void label(uint8_t row, const char *text, const char *value, uint8_t attr)
{
    ui_text(LABEL_COL, row, text, UI_MUTED);
    ui_text_field(VALUE_COL, row, VALUE_WIDTH, value, attr);
}
/* Adapter status can be up to 79 characters: break it at a space onto a
 * second row, which every status position leaves free. */
static void status_label(uint8_t row, const char *text)
{
    uint8_t n = 0, cut;
    while (text[n]) ++n;
    cut = n;
    if (n > VALUE_WIDTH) {
        cut = VALUE_WIDTH;
        while (cut && text[cut] != ' ') --cut;
        if (!cut) cut = VALUE_WIDTH;
    }
    ui_text(LABEL_COL, row, "Status", UI_MUTED);
    ui_text_field(VALUE_COL, row, cut < VALUE_WIDTH ? cut : VALUE_WIDTH, text, UI_INFO);
    if (cut < VALUE_WIDTH) ui_text_field((uint8_t)(VALUE_COL + cut), row, (uint8_t)(VALUE_WIDTH - cut), "", UI_INFO);
    while (text[cut] == ' ') ++cut;
    ui_text_field(VALUE_COL, (uint8_t)(row + 1), VALUE_WIDTH, text + cut, UI_INFO);
}
static void remember_status(const struct net_backend *backend)
{
    strncpy(status, backend->status, sizeof(status) - 1);
    status[sizeof(status) - 1] = 0;
}
static void show_status_change(const struct net_backend *backend, uint8_t row)
{
    if (strncmp(status, backend->status, sizeof(status) - 1)) {
        status_label(row, backend->status);
        remember_status(backend);
    }
}
static char *decimal(char *out, uint16_t value)
{
    char digits[5];
    uint8_t n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    *out = 0;
    return out;
}

UI_ENTRY uint8_t ui_launcher(void)
{
    uint8_t key;
    ui_clear();
    ui_title("Launcher");
    ui_text(LABEL_COL, 2, "Commodore 128 - 80 columns", UI_MUTED);
    ui_panel(4, 9, "Network adapter");
    ui_text(LABEL_COL, 6, "F1", UI_ACTION);  ui_text(10, 6, "RR-Net", UI_TEXT);
    ui_text(LABEL_COL, 8, "F3", UI_ACTION);  ui_text(10, 8, "1541 Ultimate II+", UI_TEXT);
    ui_text(LABEL_COL, 10, "F5", UI_ACTION); ui_text(10, 10, "WiC64", UI_TEXT);
    ui_text(LABEL_COL, 14, "All adapters: direct TCP. RR-Net uses DHCP; DNS 1.1.1.1.", UI_MUTED);
    ui_keys(launcher_keys);
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

static void draw_state(void)
{
    if (ready) label(6, "State", "Ready for a connection.", UI_GOOD);
    else label(6, "State", "Interface unavailable. F1 retries initialization.", UI_ERROR);
}
#define RECENT_ROW 13
static struct phonebook_entry recent;
static char target[72];
/* "host:port" of request into target. */
static void format_target(const char *host, uint16_t port)
{
    char *out = target;
    strncpy(target, host, 64); target[64] = 0;
    while (*out) ++out;
    *out++ = ':';
    decimal(out, port);
}
static void draw_recent(void)
{
    char digit[2];
    uint8_t i, row;
    ui_panel(RECENT_ROW - 1, RECENT_MAX + 2, "Recent connections");
    if (!book.recent_count)
        ui_text(LABEL_COL, RECENT_ROW, "None yet. F3 connects by address; F5 opens the phonebook.", UI_MUTED);
    digit[1] = 0;
    for (i = 0; i < book.recent_count; ++i) {
        row = (uint8_t)(RECENT_ROW + i);
        bank1_read(&recent, BANK1_RECENT + (uint16_t)i * BANK1_ENTRY_STRIDE, sizeof(recent));
        digit[0] = (char)('1' + i);
        ui_text(LABEL_COL, row, digit, UI_ACTION);
        ui_text_field(8, row, 26, recent.name, UI_HEADING);
        format_target(recent.hostname, recent.port);
        ui_text_field(34, row, 31, target, UI_INFO);
        ui_text(66, row, terminal_label(recent.profile), UI_MUTED);
    }
}
static void draw_main(const struct net_backend *backend)
{
    ui_clear();
    ui_title("Main");
    ui_panel(2, 9, "Network interface");
    label(4, "Adapter", backend->name, UI_INFO);
    draw_state();
    status_label(8, backend->status);
    draw_recent();
    ui_keys(main_keys);
}
static uint8_t initialize(const struct net_backend *backend)
{
    int result;
    label(6, "State", "Initializing interface...", UI_TEXT);
    result = backend->init();
    status_label(8, backend->status);
    remember_status(backend);
    if (result < 0) return 0;
    /* NET_PENDING means the IO2 reset and network queries still need polling.
     * Each backend enforces its own command deadlines. */
    while (backend->state() == NET_INITIALIZING) {
        backend->poll();
        show_status_change(backend, 8);
    }
    return backend->state() == NET_CLOSED;
}
/* A one-line field: white text, an amber block cursor at the end, and the
 * tail shown when the text is longer than the field. digits: bit 0 accepts
 * only 0-9; bit 1 lets RETURN enter an empty value (otherwise it cancels). */
static uint8_t edit(const struct net_backend *backend, uint8_t row, uint8_t width,
                    char *text, uint8_t capacity, uint8_t digits)
{
    uint8_t length = (uint8_t)strlen(text), key, start;
    for (;;) {
        start = length >= width ? (uint8_t)(length - width + 1) : 0;
        ui_text_field(VALUE_COL, row, width, text + start, UI_HEADING);
        ui_text_field((uint8_t)(VALUE_COL + length - start), row, 1, "", UI_BAR);
        key = wait_key(backend);
        if (cancelled(key)) return 0;
        if (key == 10 || key == 13) {
            ui_text_field(VALUE_COL, row, width, text + start, UI_HEADING);
            return length != 0 || (digits & 2);
        }
        if (key == PETSCII_HOME) { length = 0; text[0] = 0; }
        else if (key == PETSCII_DEL) { if (length) text[--length] = 0; }
        else if (length < capacity && key >= 33 && key <= 126 &&
                 (!(digits & 1) || (key >= '0' && key <= '9'))) {
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
static void draw_form(const struct net_backend *backend, const char *heading)
{
    ui_clear();
    ui_title("Connect");
    ui_panel(2, 11, heading);
    label(4, "Adapter", backend->name, UI_INFO);
}
/* Hostname, port and terminal type. Returns 1 with request filled in. */
static uint8_t connect_form(const struct net_backend *backend, struct connect_request *request)
{
    uint16_t port;
    uint8_t i, key, valid;
    /* A new transport session starts with an empty host. Do not retain an
     * earlier BBS address in application RAM after returning to the launcher. */
    request->name[0] = request->hostname[0] = 0;
    port_text[0] = '2'; port_text[1] = '3'; port_text[2] = 0;
    draw_form(backend, "New connection");
    status_label(HOST_ROW, backend->status);
    ui_keys(cancel_keys);
    if (!wait_ready(backend)) return 0;
    for (;;) {
        draw_form(backend, "New connection");
        label(HOST_ROW, "Host", "", UI_HEADING);
        label(PORT_ROW, "Port", port_text, UI_HEADING);
        ui_text(LABEL_COL, 14, "Hostname or IPv4 address, then port 1-65535.", UI_MUTED);
        ui_keys(edit_keys);
        if (!edit(backend, HOST_ROW, VALUE_WIDTH, request->hostname, 64, 0) ||
            !edit(backend, PORT_ROW, 6, port_text, 5, 1)) return 0;
        /* At most five digits: reject values above 65535 without overflow. */
        port = 0; valid = 1;
        for (i = 0; port_text[i]; ++i) {
            if (port > 6553 || (port == 6553 && port_text[i] > '5')) valid = 0;
            port = (uint16_t)(port * 10 + (uint8_t)(port_text[i] - '0'));
        }
        if (valid && port) break;
        ui_text(LABEL_COL, PROFILE_ROW, "Invalid port. Press a key to try again.", UI_ERROR);
        wait_key(backend);
    }
    request->port = port;
    ui_text(LABEL_COL, PROFILE_ROW, "Terminal", UI_MUTED);
    terminal_choices(VALUE_COL, PROFILE_ROW);
    ui_text_field(LABEL_COL, 14, 70, "ANSI: CP437, 80x25. VT100: DEC graphics, 80x24. PETSCII: C128, 80x25.", UI_MUTED);
    ui_text(LABEL_COL, 15, "Mono: white text without colour attributes, for faster output.", UI_MUTED);
    ui_text(LABEL_COL, 16, "During a session: F7 disconnects; CTRL-C goes to the host.", UI_MUTED);
    ui_keys(profile_keys);
    do { key = wait_key(backend); if (cancelled(key)) return 0; }
    while (!terminal_for_key(key));
    request->profile = (enum telnet_profile)terminal_for_key(key);
    return 1;
}
/* ---- IRC server settings ----
 * "irc" on the boot disk, plain ASCII text, one value on each line:
 *   80TIRC 1
 *   irc.libera.chat
 *   6667
 *   c128user
 *   #c64,#retro           (may be empty)
 */
static char *irc_field(uint8_t index, uint8_t *capacity)
{
    if (index == 1) { *capacity = sizeof(irc_config.host) - 1; return irc_config.host; }
    if (index == 2) { *capacity = sizeof(irc_config.port_text) - 1; return irc_config.port_text; }
    if (index == 3) { *capacity = sizeof(irc_config.nick) - 1; return irc_config.nick; }
    *capacity = sizeof(irc_config.channels) - 1;
    return irc_config.channels;
}
/* What is wrong with the settings, or 0. Sets the port number. */
static const char *irc_problem(void)
{
    irc_config.port = irc_port(irc_config.port_text);
    if (!irc_config.host[0]) return "Enter a server hostname or IPv4 address.";
    if (!irc_config.port) return "Enter a port from 1 to 65535.";
    if (!irc_valid_nick(irc_config.nick)) return "Enter a nickname: letters, digits and - _ [ ], starting with a letter.";
    if (!irc_valid_channels(irc_config.channels)) return "Channels start with # and are separated by commas, without spaces.";
    return 0;
}
static void irc_defaults(void)
{
    strcpy(irc_config.host, "irc.libera.chat");
    strcpy(irc_config.port_text, "6667");
    strcpy(irc_config.nick, "c128user");
    irc_config.channels[0] = 0;
}
static void irc_load(void)
{
    uint8_t n, i, line = 0, length = 0, capacity, c, after_cr = 0;
    char *field;
    irc_defaults();
    if (disk_open("irc", ",s,r", 2)) return;
    field = irc_field(0, &capacity);
    while ((n = disk_read(target, 64)) != 0) {
        for (i = 0; i < n; ++i) {
            c = (uint8_t)target[i];
            if (c == 13 || c == 10) {
                /* CR LF counts once. */
                if (after_cr && c == 10) { after_cr = 0; continue; }
                after_cr = c == 13;
                if (line && line <= 4) field[length] = 0;
                ++line; length = 0;
                if (line <= 4) field = irc_field(line, &capacity);
            } else {
                after_cr = 0;
                if (line && line <= 4 && length < capacity && c >= 33 && c <= 126) field[length++] = (char)c;
            }
        }
    }
    if (line && line <= 4) field[length] = 0;
    disk_close();
    if (line < 4 || irc_problem()) irc_defaults();
}
static uint8_t irc_write_line(const char *value)
{
    uint8_t code = disk_write(value, (uint8_t)strlen(value));
    return code ? code : disk_write("\r\n", 2);
}
static void irc_save(void)
{
    uint8_t line, capacity;
    disk_command("s:irc");
    if (disk_open("irc", ",s,w", 2)) return;
    if (!irc_write_line("80TIRC 1"))
        for (line = 1; line <= 4 && !irc_write_line(irc_field(line, &capacity)); ++line) {}
    disk_close();
}
/* Changes when any setting does. */
static uint16_t irc_sum(void)
{
    uint16_t sum = 0;
    uint8_t line, capacity;
    const char *s;
    for (line = 1; line <= 4; ++line)
        for (s = irc_field(line, &capacity); *s; ++s) sum = (uint16_t)(sum * 31 + (uint8_t)*s + line);
    return sum;
}
#define IRC_NOTE_ROW 15
UI_ENTRY uint8_t ui_irc_form(const struct net_backend *backend)
{
    uint16_t before;
    uint8_t line, capacity;
    char *field;
    const char *problem = 0;
    if (!irc_config_loaded) {
        ui_clear();
        ui_title("IRC");
        ui_text(LABEL_COL, 3, "Reading the IRC settings...", UI_TEXT);
        irc_load();
        irc_config_loaded = 1;
    }
    before = irc_sum();
    for (;;) {
        ui_clear();
        ui_title("IRC");
        ui_panel(2, 13, "IRC server");
        label(4, "Adapter", backend->name, UI_INFO);
        label(6, "Server", irc_config.host, UI_HEADING);
        label(8, "Port", irc_config.port_text, UI_HEADING);
        label(10, "Nickname", irc_config.nick, UI_HEADING);
        label(12, "Channels", irc_config.channels, UI_HEADING);
        if (problem) ui_text(LABEL_COL, IRC_NOTE_ROW, problem, UI_ERROR);
        ui_text(LABEL_COL, 17, "Plain IRC only: this C128 client has no TLS. The usual port is 6667.", UI_MUTED);
        ui_text(LABEL_COL, 18, "Channels to join: #one,#two - or leave empty and use /join later.", UI_MUTED);
        ui_keys(edit_keys);
        for (line = 1; line <= 4; ++line) {
            field = irc_field(line, &capacity);
            if (edit(backend, (uint8_t)(4 + line * 2), line == 1 || line == 4 ? VALUE_WIDTH : 17,
                     field, capacity, line == 2 ? 3 : 2)) continue;
            /* Cancelled: forget the edits, which may be invalid. */
            irc_config_loaded = 0;
            return 0;
        }
        problem = irc_problem();
        if (!problem) break;
    }
    if (irc_sum() != before) {
        ui_text_field(LABEL_COL, IRC_NOTE_ROW, 70, "Saving the IRC settings...", UI_TEXT);
        irc_save();
    }
    return 1;
}
/* Drawn before the resident session starts the (possibly blocking) open;
 * it prints the adapter's status at SESSION_STATUS_ROW. */
static void draw_connecting(const struct net_backend *backend, const struct connect_request *request)
{
    format_target(request->hostname, request->port);
    ui_clear();
    ui_title("Connecting");
    ui_panel(2, 11, "Opening connection");
    label(4, "Adapter", backend->name, UI_INFO);
    label(6, "Host", target, UI_INFO);
    ui_text(LABEL_COL, 8, "Resolving host and waiting for the remote server.", UI_TEXT);
    ui_text(LABEL_COL, 14, "WiC64 opens are synchronous; this can take several seconds.", UI_MUTED);
    ui_text(LABEL_COL, 15, "A cancelled open is closed when it completes.", UI_MUTED);
    ui_keys(cancel_keys);
}
static void show_result(const struct net_backend *backend, struct session_result *result)
{
    char number[6];
    uint8_t failed = result->outcome == SESSION_CONNECT_FAILED;
    ui_clear();
    ui_title("Session");
    ui_panel(2, failed ? 6 : 9, failed ? "Connection failed" : "Session ended");
    ui_text_field(LABEL_COL, 4, 70, result->message, failed ? UI_ERROR : UI_HEADING);
    if (result->detail[0]) ui_text_field(LABEL_COL, 5, 70, result->detail, UI_ERROR);
    if (!failed) {
        decimal(number, result->keys);
        ui_text(LABEL_COL, 7, "Keys read", UI_MUTED);
        ui_text(36, 7, number, UI_INFO);
        decimal(number, result->sent);
        ui_text(LABEL_COL, 8, "TCP bytes sent (incl. Telnet)", UI_MUTED);
        ui_text(36, 8, number, UI_INFO);
    }
    ui_keys(any_key);
    result->outcome = SESSION_NONE;
    wait_key(backend);
}
static void close_backend(const struct net_backend *backend)
{
    backend->close();
    while (backend->state() == NET_CLOSING) backend->poll();
}
/* For a board from the phonebook or the recent list. */
UI_ENTRY uint8_t ui_prepare_connect(const struct net_backend *backend, const struct connect_request *request)
{
    draw_connecting(backend, request);
    if (wait_ready(backend)) return 1;
    close_backend(backend);
    return 0;
}

UI_ENTRY uint8_t ui_main(const struct net_backend *backend, struct connect_request *request,
                         struct session_result *result)
{
    uint8_t key;
    if (result->outcome != SESSION_NONE) show_result(backend, result);
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
            draw_state();
        }
        show_status_change(backend, 8);
        if (!platform_key_pending()) continue;
        key = (uint8_t)getch();
        if (key == PETSCII_F8) return UI_QUIT;
        if (key == PETSCII_F1) {
            close_backend(backend);
            ready = initialize(backend);
            draw_state();
        } else if (key == PETSCII_F3 && ready) {
            if (connect_form(backend, request)) {
                draw_connecting(backend, request);
                return UI_CONNECT;
            }
            /* Cancelled: leave the adapter closed, as before. */
            close_backend(backend);
            draw_main(backend);
        } else if (key == PETSCII_F5) {
            return UI_PHONEBOOK;
        } else if (key == PETSCII_F2 && ready) {
            if (ui_irc_form(backend)) return UI_IRC;
            draw_main(backend);
        } else if (key >= '1' && key < '1' + book.recent_count && ready) {
            bank1_read(&recent, BANK1_RECENT + (uint16_t)(key - '1') * BANK1_ENTRY_STRIDE, sizeof(recent));
            strcpy(request->name, recent.name);
            strcpy(request->hostname, recent.hostname);
            request->port = recent.port;
            request->profile = recent.profile;
            if (ui_prepare_connect(backend, request)) return UI_CONNECT;
            draw_main(backend);
        }
    }
}

#ifdef __OSCAR64C__
#pragma optimize(pop)
#endif
#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
