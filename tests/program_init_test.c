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
#include "memory_map.h"

static int state, calls, polls, sessions, key_index, fail_first, immediate, idle_polls;
static int disk_loads, disk_fail, bank1_reads, bank1_writes, summaries, book_runs, recents_noted, startups;
static char last_load[8];
static uint8_t vdc[0x1000];
/* F3 opens the connect form: host "a", RETURN, port 23, RETURN, ANSI. Each
 * session ends on the summary screen, which waits for a key. On the main
 * screen with the interface down, these keys are ignored. */
#define CONNECT PETSCII_F3, 'a', 13, 13, '1', ' '
/* F5: the phonebook (stubbed) dials "a" too, then the summary. */
#define PHONEBOOK PETSCII_F5, ' '
static const unsigned char keys[] = {CONNECT, PETSCII_F1, CONNECT, PHONEBOOK, PETSCII_F8};
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
    return (char)script[key_index++];
}
uint8_t platform_device(void) { return 8; }
uint8_t platform_key_pending(void) { return idle_polls > 0; }
uint16_t overlay_disk_load(const char *name)
{
    assert(!strcmp(name, "uimain") || !strcmp(name, "uibook") || !strcmp(name, "uiterm"));
    strcpy(last_load, name);
    ++disk_loads;
    return disk_fail ? 0 : UI_SLOT_START + 0x100;
}
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
void session_run(const struct net_backend *backend, const struct connect_request *request,
                 struct session_result *result)
{
    assert(backend->state() == NET_CLOSED);
    assert(!strcmp(request->hostname, "a") && request->port == 23);
    assert(!strcmp(request->name, book_runs ? "A board" : ""));
    assert(request->profile == TELNET_PROFILE_ANSI_80);
    /* The Connecting screen is up while the adapter opens. */
    assert(on_screen(0, "Connecting") && on_screen(6, "a:23"));
    result->outcome = SESSION_ENDED;
    strcpy(result->message, "Disconnected.");
    result->detail[0] = 0;
    result->keys = 3; result->sent = 42;
    ++sessions;
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
        book_runs = recents_noted = startups = 0;
        fail_first = mode == 1; immediate = mode == 2;
        ui_reset();
        program_run(&backend);
        assert(key_index == (int)sizeof(keys));
        /* fail_first: the first connect keys arrive while the interface is down. */
        assert(calls == 2 && sessions == (fail_first ? 2 : 3));
        assert(summaries == sessions && recents_noted == sessions);
        assert(startups == 1 && book_runs == 1);
        assert(idle_polls >= 4);
    }
    /* test_overlay_cache already loaded main and book: only the terminal
     * came from disk here, once; every other switch was a bank-1 copy. */
    assert(disk_loads == 1 && bank1_writes == 1 && !strcmp(last_load, "uiterm"));
    assert(bank1_reads > 10);
    puts("Main-screen, connect form, summary and overlay cache tests passed");
    return 0;
}
