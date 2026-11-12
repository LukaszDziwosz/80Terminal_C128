#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stubs/conio.h"
#include "program.h"
#include "overlay.h"
#include "session.h"
#include "ui.h"

static int state, calls, polls, sessions, key_index, fail_first, immediate, idle_polls;
static int disk_loads, disk_fail, bank1_reads, bank1_writes;
static unsigned row, col;
static char screen[25][81];
/* F3 opens the connect form: host "a", RETURN, port 23, RETURN, ANSI. On
 * the main screen with the interface down, those keys are ignored. */
#define CONNECT PETSCII_F3, 'a', 13, 13, '1'
static const unsigned char keys[] = {CONNECT, PETSCII_F1, CONNECT, PETSCII_F8};
void clrscr(void) { memset(screen, 0, sizeof(screen)); }
void gotoxy(char x, char y) { col = (unsigned char)x; row = (unsigned char)y; }
void putch(char c) { assert(row < 25 && col < 80); screen[row][col++] = c; }
char getch(void)
{
    assert(state != NET_INITIALIZING);
    if (strstr(screen[2] + 3, "80Terminal / MAIN"))
        assert((strstr(screen[9] + 3, "Ready for a connection.") != NULL) == (state == NET_CLOSED));
    assert(key_index < (int)sizeof(keys));
    return (char)keys[key_index++];
}
uint8_t platform_device(void) { return 8; }
uint8_t platform_key_pending(void) { return idle_polls > 0; }
uint8_t overlay_disk_load(const char *name)
{
    assert(!strcmp(name, "uimain"));
    ++disk_loads;
    return !disk_fail;
}
void bank1_read(void *destination, uint16_t source, uint16_t length)
{
    (void)destination; (void)source; (void)length; ++bank1_reads;
}
void bank1_write(uint16_t destination, const void *source, uint16_t length)
{
    (void)destination; (void)source; (void)length; ++bank1_writes;
}
void session_run(const struct net_backend *backend, const struct connect_request *request)
{
    assert(backend->state() == NET_CLOSED);
    assert(!strcmp(request->hostname, "a") && request->port == 23);
    assert(request->profile == TELNET_PROFILE_ANSI_80);
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
}
int main(void)
{
    test_overlay_cache();
    for (int mode = 0; mode < 3; ++mode) {
        calls = polls = sessions = key_index = idle_polls = 0;
        fail_first = mode == 1; immediate = mode == 2;
        ui_reset();
        program_run(&backend);
        assert(key_index == (int)sizeof(keys));
        assert(calls == 2 && sessions == (fail_first ? 1 : 2));
        assert(idle_polls >= 4);
    }
    /* The overlay stayed in its slot: one disk load, no bank-1 reloads. */
    assert(disk_loads == 2 && bank1_reads == 0);
    puts("Main-screen, connect form and overlay cache tests passed");
    return 0;
}
