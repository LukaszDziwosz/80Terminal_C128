#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stubs/conio.h"
#include "program.h"

static int state, calls, polls, sessions, key_index, fail_first, immediate;
static unsigned row, col;
static char screen[25][81];
static const unsigned char keys[] = {PETSCII_F3, PETSCII_F1, PETSCII_F3, PETSCII_F8};
void clrscr(void) { memset(screen, 0, sizeof(screen)); }
void gotoxy(char x, char y) { col = (unsigned char)x; row = (unsigned char)y; }
void putch(char c) { assert(row < 25 && col < 80); screen[row][col++] = c; }
char getch(void)
{
    assert(state != NET_INITIALIZING);
    assert((strstr(screen[9] + 3, "Ready for a connection.") != NULL) == (state == NET_CLOSED));
    assert(key_index < 4);
    return (char)keys[key_index++];
}
uint8_t platform_device(void) { return 8; }
void screen_font_preview(uint8_t device) { (void)device; }
void session_open(const struct net_backend *backend)
{
    assert(backend->state() == NET_CLOSED);
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
    assert(state == NET_INITIALIZING);
    if (++polls == 3) state = fail_first && calls == 1 ? NET_FAILED : NET_CLOSED;
}
static int get_state(void) { return state; }
static void close_connection(void) { state = NET_CLOSED; }
static struct net_backend backend = {
    "Ultimate test", "Checking network", initialize, NULL, poll,
    NULL, NULL, get_state, close_connection
};
int main(void)
{
    for (int mode = 0; mode < 3; ++mode) {
        calls = polls = sessions = key_index = 0;
        fail_first = mode == 1; immediate = mode == 2;
        program_run(&backend);
        assert(calls == 2 && sessions == (fail_first ? 1 : 2));
    }
    puts("Main-screen initialization and retry tests passed");
    return 0;
}
