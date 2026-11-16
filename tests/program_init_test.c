#include "check.h"
#include <stdio.h>
#include <string.h>
#include "stubs/conio.h"
#include "program.h"
#include "overlay.h"
#include "session.h"
#include "ui.h"
#include "vdc_screen.h"
#include "book.h"
#include "transfer.h"
#include "ircui.h"
#include "ircconfig.h"
#include "disk.h"
#include "memory_map.h"

static int state, calls, polls, sessions, key_index, fail_first, immediate, idle_polls;
static int disk_loads, disk_fail, bank1_reads, bank1_writes, summaries, book_runs, recents_noted, startups;
static char last_load[8];
static uint8_t vdc[0x1000];
/* F3 opens the connect form: host "a", RETURN, port 23, RETURN, ANSI (or
 * ANSI mono, after a key that is not a terminal). Each
 * session ends on the summary screen, which waits for a key. On the main
 * screen with the interface down, these keys are ignored. */
#define CONNECT PETSCII_F3, 'a', 13, 13, '1', ' '
#define CONNECT_MONO PETSCII_F3, 'a', 13, 13, '9', '4', ' '
/* F5: the phonebook (stubbed) dials "a" too, then the summary. */
#define PHONEBOOK PETSCII_F5, ' '
/* F2: the IRC form with its defaults (no settings file), accepted with four
 * RETURNs, then the IRC overlay (stubbed), which returns to the main screen. */
#define IRC PETSCII_F2, 13, 13, 13, 13
static const unsigned char keys[] = {CONNECT, PETSCII_F1, CONNECT_MONO, PHONEBOOK, IRC, PETSCII_F8};
static const unsigned char *script = keys;
static int script_length = (int)sizeof(keys);

/* The UI draws screen codes straight into VDC RAM. */
void screen_write_run(uint16_t at, const uint8_t *data, uint16_t length)
{ assert(at + length <= sizeof(vdc)); memcpy(vdc + at, data, length); }
void screen_fill_run(uint16_t at, uint8_t value, uint16_t length)
{ assert(at + length <= sizeof(vdc)); memset(vdc + at, value, length); }
static const char *row_text(uint8_t row)
{
    static char text[81];
    uint8_t c, i;
    for (i = 0; i < 80; ++i) {
        c = vdc[row * 80 + i];
        text[i] = c >= 1 && c <= 26 ? (char)(c + 96) : (c >= 32 && c <= 63) || (c >= 65 && c <= 90) ? (char)c : '#';
    }
    text[80] = 0;
    return text;
}
static uint8_t attr_at(uint8_t col, uint8_t row) { return vdc[0x800 + row * 80 + col]; }
static int on_screen(uint8_t row, const char *text) { return strstr(row_text(row), text) != NULL; }

void clrscr(void) {}
void gotoxy(char x, char y) { (void)x; (void)y; }
void putch(char c) { (void)c; }
char getch(void)
{
    assert(state != NET_INITIALIZING);
    /* Every UI screen has the title and the function-key bar. */
    assert(on_screen(0, "80Terminal ") && (attr_at(0, 0) & 0x40));
    assert(attr_at(1, 24) & 0x40);
    if (on_screen(0, "Main"))
        assert(on_screen(6, "Ready for a connection.") == (state == NET_CLOSED));
    if (on_screen(0, "Session")) {
        assert(on_screen(4, "Disconnected.") && on_screen(7, "3") && on_screen(8, "42"));
        ++summaries;
    }
    assert(key_index < script_length);
    /* Like Oscar64's conio getch(): RETURN arrives as 10, not 13. */
    return script[key_index] == 13 ? (++key_index, (char)10) : (char)script[key_index++];
}
uint8_t platform_device(void) { return 8; }
uint8_t platform_key_pending(void) { return idle_polls > 0; }
uint16_t overlay_disk_load(const char *name)
{
    assert(!strcmp(name, "uimain") || !strcmp(name, "uibook") || !strcmp(name, "uiterm") || !strcmp(name, "uixfer") ||
           !strcmp(name, "uiirc"));
    strcpy(last_load, name);
    ++disk_loads;
    return disk_fail ? 0 : UI_SLOT_START + 0x100;
}
static int irc_runs;
void irc_run(const struct net_backend *backend)
{
    /* Only offered with the interface ready, after the form. */
    assert(backend->state() == NET_CLOSED);
    assert(irc_config_loaded && irc_config.port == 6667);
    assert(!strcmp(irc_config.host, "irc.libera.chat") && !strcmp(irc_config.nick, "c128user"));
    ++irc_runs;
}
/* No "irc" file on the disk, and nothing changed to save. */
char disk_message[40];
uint8_t disk_device = 8;
uint8_t disk_open(const char *name, const char *mode, uint8_t secondary)
{
    assert(!strcmp(name, "irc") && !strcmp(mode, ",s,r") && secondary == 2);
    return 62;
}
uint8_t disk_read(char *buffer, uint8_t capacity) { (void)buffer; (void)capacity; assert(0); return 0; }
uint8_t disk_write(const char *data, uint8_t length) { (void)data; (void)length; assert(0); return 1; }
uint8_t disk_close(void) { assert(0); return 0; }
uint8_t disk_command(const char *command) { (void)command; assert(0); return 0; }
void book_startup(void) { ++startups; }
uint8_t book_run(const struct net_backend *backend, struct connect_request *request)
{
    (void)backend;
    ++book_runs;
    strcpy(request->name, "A board");
    strcpy(request->hostname, "a");
    request->port = 23;
    request->profile = TELNET_PROFILE_ANSI_80;
    return UI_CONNECT;
}
void book_note_recent(const struct connect_request *request)
{
    assert(!strcmp(request->hostname, "a"));
    ++recents_noted;
}
void bank1_read(void *destination, uint16_t source, uint16_t length)
{
    (void)destination; (void)source; (void)length; ++bank1_reads;
}
void bank1_write(uint16_t destination, const void *source, uint16_t length)
{
    (void)destination; (void)source; (void)length; ++bank1_writes;
}
static int transfers, resumes;
/* The first session of each run presses HELP once: the program loop runs
 * the transfer overlay, then resumes the same session. */
void session_run(const struct net_backend *backend, const struct connect_request *request,
                 struct session_result *result, uint8_t resuming)
{
    if (resuming) {
        assert(transfers == resumes + 1);
        ++resumes;
    } else {
        assert(backend->state() == NET_CLOSED);
        assert(!strcmp(request->hostname, "a") && request->port == 23);
        assert(!strcmp(request->name, book_runs ? "A board" : ""));
        /* The second connect form picks "4" (after an ignored key); with
         * fail_first it is the first session. The phonebook dials ANSI. */
        assert(request->profile == (!book_runs && sessions == (fail_first ? 0 : 1) ?
               TELNET_PROFILE_ANSI_MONO_80 : TELNET_PROFILE_ANSI_80));
        /* The Connecting screen is up while the adapter opens. */
        assert(on_screen(0, "Connecting") && on_screen(6, "a:23"));
        ++sessions;
        if (sessions == 1) { result->outcome = SESSION_TRANSFER; return; }
    }
    result->outcome = SESSION_ENDED;
    strcpy(result->message, "Disconnected.");
    result->detail[0] = 0;
    result->keys = 3; result->sent = 42;
}
void transfer_run(const struct net_backend *backend, const struct connect_request *request)
{
    (void)backend;
    assert(!strcmp(request->hostname, "a"));
    ++transfers;
}
static int initialize(void)
{
    ++calls; polls = 0;
    state = immediate ? NET_CLOSED : NET_INITIALIZING;
    return immediate ? NET_OK : NET_PENDING;
}
static void poll(void)
{
    if (state != NET_INITIALIZING) { ++idle_polls; return; }
    if (++polls == 3) state = fail_first && calls == 1 ? NET_FAILED : NET_CLOSED;
}
static int get_state(void) { return state; }
static void close_connection(void) { state = NET_CLOSED; }
static struct net_backend backend = {
    "Ultimate test", "Checking network", initialize, NULL, poll,
    NULL, NULL, get_state, close_connection
};
static void test_overlay_cache(void)
{
    /* Unit-level: the first use loads from disk and fills the bank-1 copy;
     * a failed load is retried from disk, never served from the cache. */
    assert(overlay_use(OVERLAY_COUNT) == 0);
    disk_fail = 1;
    assert(overlay_use(OVERLAY_UI_MAIN) == 0 && disk_loads == 1 && bank1_writes == 0);
    disk_fail = 0;
    assert(overlay_use(OVERLAY_UI_MAIN) == 1 && disk_loads == 2 && bank1_writes == 1);
    assert(overlay_use(OVERLAY_UI_MAIN) == 1 && disk_loads == 2 && bank1_reads == 0);
    /* Switching overlays: a new one comes from disk, a cached one from bank 1. */
    assert(overlay_use(OVERLAY_UI_BOOK) == 1 && disk_loads == 3 && !strcmp(last_load, "uibook"));
    assert(overlay_use(OVERLAY_UI_MAIN) == 1 && disk_loads == 3 && bank1_reads == 1);
    disk_loads = bank1_reads = bank1_writes = 0;
}
static void test_launcher(void)
{
    /* Only function keys choose: digits and q are ignored. */
    static const unsigned char launcher_keys[] = {'1', 'q', PETSCII_F3, PETSCII_F8};
    script = launcher_keys; script_length = (int)sizeof(launcher_keys); key_index = 0;
    assert(ui_launcher() == 2 && key_index == 3);
    assert(on_screen(0, "Launcher") && on_screen(8, "1541 Ultimate II+"));
    assert(ui_launcher() == 0 && key_index == 4);
    script = keys; script_length = (int)sizeof(keys);
}
int main(void)
{
    test_overlay_cache();
    test_launcher();
    for (int mode = 0; mode < 3; ++mode) {
        calls = polls = sessions = key_index = idle_polls = summaries = 0;
        book_runs = recents_noted = startups = transfers = resumes = irc_runs = 0;
        fail_first = mode == 1; immediate = mode == 2;
        ui_reset();
        program_run(&backend);
        assert(key_index == (int)sizeof(keys));
        /* fail_first: the first connect keys arrive while the interface is down. */
        assert(calls == 2 && sessions == (fail_first ? 2 : 3));
        assert(summaries == sessions && recents_noted == sessions);
        assert(startups == 1 && book_runs == 1 && irc_runs == 1);
        /* One transfer, then back to the same session. */
        assert(transfers == 1 && resumes == 1);
        assert(idle_polls >= 4);
    }
    /* test_overlay_cache already loaded main and book: only the terminal
     * transfer and IRC overlays came from disk here, once each; every other
     * switch was a bank-1 copy. */
    assert(disk_loads == 3 && bank1_writes == 3);
    assert(bank1_reads > 10);
    puts("Main-screen, connect form, summary and overlay cache tests passed");
    return 0;
}
