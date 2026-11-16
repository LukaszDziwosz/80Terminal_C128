/* IRC on the host: the server form of the uimain overlay, then the uiirc
 * overlay's chat, with a scripted IRC server and keyboard, an in-memory
 * "irc" settings file, bank 1 and the VDC screen. */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "stubs/conio.h"
#include "ircui.h"
#include "irc.h"
#include "ircconfig.h"
#include "ui.h"
#include "book.h"
#include "disk.h"
#include "bank1.h"
#include "session.h"

uint8_t irc_config_loaded;
struct irc_settings irc_config;
struct book_state book;   /* the main screen's recent list: not shown here */
uint8_t session_buffer[1024];
/* As 6502 code this test fills the default memory: its bank 1 and VDC
 * stand-ins go above it, at $A000-$CFFF, which is RAM in the emulator. */
#ifdef __OSCAR64C__
#pragma section(testbss, 0, , , bss)
#pragma region(testram, 0xa000, 0xd000, , , {testbss})
#pragma bss(testbss)
#endif
static uint8_t ram1[BANK1_IRC_ROWS_END - BANK1_IRC_ROWS], vdc[0x1000];
#ifdef __OSCAR64C__
#pragma bss(bss)
#endif

/* ---- Bank 1 and VDC ---- */
void bank1_read(void *destination, uint16_t source, uint16_t length)
{
    assert(source >= BANK1_IRC_ROWS && source + length <= BANK1_IRC_ROWS_END);
    memcpy(destination, ram1 + (source - BANK1_IRC_ROWS), length);
}
void bank1_write(uint16_t destination, const void *source, uint16_t length)
{
    assert(destination >= BANK1_IRC_ROWS && destination + length <= BANK1_IRC_ROWS_END);
    memcpy(ram1 + (destination - BANK1_IRC_ROWS), source, length);
}
void screen_write_run(uint16_t at, const uint8_t *data, uint16_t length)
{ assert(at + length <= sizeof(vdc)); memcpy(vdc + at, data, length); }
void screen_fill_run(uint16_t at, uint8_t value, uint16_t length)
{ assert(at + length <= sizeof(vdc)); memset(vdc + at, value, length); }
void screen_copy_run(uint16_t destination, uint16_t source, uint16_t length)
{
    /* The VDC copies upwards, one byte at a time. */
    assert(destination < source && source + length <= sizeof(vdc));
    memmove(vdc + destination, vdc + source, length);
}
static char shown[81];
static const char *row_text(uint8_t row)
{
    uint8_t c, i;
    for (i = 0; i < 80; ++i) {
        c = vdc[row * 80 + i];
        shown[i] = c >= 1 && c <= 26 ? (char)(c + 96) : (c >= 32 && c <= 63) || (c >= 65 && c <= 90) ? (char)c : '#';
    }
    shown[80] = 0;
    return shown;
}
static uint8_t attr_at(uint8_t col, uint8_t row) { return vdc[0x800 + row * 80 + col] & 0x4f; }
static int on_screen(uint8_t row, const char *text) { return strstr(row_text(row), text) != NULL; }
/* The row holding text, or 0 (the title row never matches these). */
static uint8_t find_row(const char *text)
{
    uint8_t row;
    for (row = 1; row < 25; ++row) if (on_screen(row, text)) return row;
    return 0;
}

/* ---- The "irc" file ---- */
static char file_data[200];
static uint8_t file_exists, file_open, file_writing, file_at, file_length, saves;
char disk_message[40];
uint8_t disk_device = 8;
static void put_file(const char *data)
{
    file_exists = 1;
    file_length = (uint8_t)strlen(data);
    memcpy(file_data, data, file_length);
}
uint8_t disk_open(const char *name, const char *mode, uint8_t secondary)
{
    assert(!strcmp(name, "irc") && secondary == 2 && !file_open);
    file_writing = mode[3] == 'w';
    if (!file_writing && !file_exists) return 62;
    assert(!file_writing || !file_exists);   /* scratched first */
    if (file_writing) { file_exists = 1; file_length = 0; ++saves; }
    file_open = 1; file_at = 0;
    return 0;
}
uint8_t disk_read(char *buffer, uint8_t capacity)
{
    uint8_t n = 0;
    assert(file_open && !file_writing);
    /* Odd chunk sizes split lines across reads. */
    if (capacity > 7) capacity = 7;
    while (n < capacity && file_at < file_length) buffer[n++] = file_data[file_at++];
    return n;
}
uint8_t disk_write(const char *data, uint8_t length)
{
    assert(file_open && file_writing && file_length + length < sizeof(file_data));
    memcpy(file_data + file_length, data, length);
    file_length = (uint8_t)(file_length + length);
    return 0;
}
uint8_t disk_close(void) { assert(file_open); file_open = 0; return 0; }
uint8_t disk_command(const char *command)
{
    assert(!strcmp(command, "s:irc"));
    file_exists = 0;
    return 1;
}

/* ---- The script: keys, server lines and checks, in order ----
 * K keys typed, S lines the server sends, W what the client must have
 * written by now (exactly), R text that must be on the screen, N text that
 * must not be, T and t the same for the row of window tabs, X the server
 * closes the connection. */
struct step { char kind; const char *text; };
static const struct step *steps;
static uint8_t step_at;
static uint16_t step_offset;
static char written[600];
static uint16_t written_length;
static int state, connects, closes;
static uint16_t ticks;
static char connected_host[65];
static uint16_t connected_port;
static uint8_t write_limit;
static uint16_t waits;

static void run_checks(void)
{
    for (;;) {
        char kind = steps[step_at].kind;
        const char *text = steps[step_at].text;
        if (kind == 'W') {
            written[written_length] = 0;
            /* Queued lines go out on the client's next turns. */
            if (strcmp(written, text) && ++waits < 400) return;
            waits = 0;
            if (strcmp(written, text)) {
                printf("wrote [%s], expected [%s]\n", written, text);
                printf("tabs [%s]\n", row_text(1));
                printf("last [%s]\n", row_text(21));
            }
            assert(!strcmp(written, text));
            written_length = 0;
        } else if (kind == 'R') {
            if (!find_row(text)) printf("not on screen: [%s]\n", text);
            assert(find_row(text) != 0);
        } else if (kind == 'N') {
            if (find_row(text)) printf("on screen: [%s]\n", text);
            assert(find_row(text) == 0);
        } else if (kind == 'T' || kind == 't') {
            /* A window tab is there (T) or not (t). */
            if (on_screen(1, text) != (kind == 'T')) printf("tabs [%s]: [%s]\n", row_text(1), text);
            assert(on_screen(1, text) == (kind == 'T'));
        } else if (kind == 'X') {
            /* The client sees it on its next turn. */
            state = NET_EOF;
            ++step_at;
            return;
        } else return;
        ++step_at; step_offset = 0;
    }
}
uint8_t platform_key_pending(void)
{
    run_checks();
    return steps[step_at].kind == 'K';
}
char getch(void)
{
    char c;
    assert(steps[step_at].kind == 'K');
    c = steps[step_at].text[step_offset++];
    if (!steps[step_at].text[step_offset]) { ++step_at; step_offset = 0; }
    /* Like Oscar64's conio getch(): RETURN arrives as 10, not 13. */
    return c == 13 ? 10 : c;
}
void clrscr(void) {}
void gotoxy(char x, char y) { (void)x; (void)y; }
void putch(char c) { (void)c; }
uint16_t platform_ticks(void) { ticks += 7; return ticks; }

/* Not named net_*: Oscar64 crashes when a function shares a name with a
 * struct or enum tag (enum net_state). */
static int fake_init(void) { return NET_OK; }
static int fake_connect(const char *hostname, uint16_t port)
{
    assert(state == NET_CLOSED);
    strcpy(connected_host, hostname);
    connected_port = port;
    ++connects;
    state = NET_CONNECTING;
    return NET_PENDING;
}
static void fake_poll(void)
{
    if (state == NET_CONNECTING) state = NET_CONNECTED;
    else if (state == NET_CLOSING) state = NET_CLOSED;
}
static int fake_read(uint8_t *buffer, uint16_t capacity)
{
    const char *text;
    uint16_t n = 0;
    assert(state == NET_CONNECTED || state == NET_EOF);
    if (steps[step_at].kind != 'S') return 0;
    text = steps[step_at].text;
    /* Short reads split lines, and CR from LF. */
    if (capacity > 5) capacity = 5;
    while (n < capacity && text[step_offset]) buffer[n++] = (uint8_t)text[step_offset++];
    if (!text[step_offset]) { ++step_at; step_offset = 0; }
    return (int)n;
}
static int fake_write(const uint8_t *buffer, uint16_t length)
{
    assert(state == NET_CONNECTED);
    /* Partial writes: the client keeps the unsent tail. */
    if (write_limit && length > write_limit) length = write_limit;
    assert(written_length + length < sizeof(written));
    memcpy(written + written_length, buffer, length);
    written_length = (uint16_t)(written_length + length);
    return (int)length;
}
static int fake_state(void) { return state; }
static void fake_close(void) { ++closes; state = state == NET_CLOSED ? NET_CLOSED : NET_CLOSING; }
static const struct net_backend backend = {
    "Test adapter", "Connection closed by the server.", fake_init, fake_connect, fake_poll,
    fake_read, fake_write, fake_state, fake_close
};
static void run(const struct step *script)
{
    steps = script; step_at = step_offset = 0;
    written_length = 0;
    state = NET_CLOSED;
    /* As the program loop does after F2 on the main screen. */
    if (ui_irc_form(&backend)) irc_run(&backend);
    run_checks();
    assert(steps[step_at].kind == 0);
    assert(state == NET_CLOSED);
}

#define F1 "\x85"
#define F3 "\x86"
#define F5 "\x87"
#define F7 "\x88"
#define HOME "\x13"
#define DEL "\x14"
#define REGISTER "NICK c128user\r\nUSER c128user 0 * :80Terminal C128\r\n"

/* No file yet: the defaults, edited, are saved; then a whole session. */
static const struct step session[] = {
    {'R', "irc.libera.chat"}, {'R', "6667"}, {'R', "no TLS"},
    /* There is no default nickname: one must be chosen. */
    {'K', "\r\r\r\r"}, {'R', "Choose your nickname"},
    /* Server: replaced. Port kept. Nickname and a channel typed. */
    {'K', HOME "irc.example.org\r\rc128user\r#retro\r"},
    {'R', "Connecting to irc.example.org:6667"},
    {'W', REGISTER},
    {'S', ":irc.example.org NOTICE * :*** Looking up your hostname\r\n"},
    {'R', "-irc.example.org- *** Looking up your hostname"},
    {'S', "PING :abc 123\r\n"},
    {'W', "PONG :abc 123\r\n"},
    /* Typing before the welcome is refused. */
    {'K', "hi\r"}, {'R', "Not registered with the server yet."}, {'W', ""},
    {'S', ":irc.example.org 001 c128user :Welcome to the \002Example\002 network\r\n"},
    {'R', "Welcome to the Example network"},
    {'W', "JOIN #retro\r\n"},
    {'S', ":irc.example.org 372 c128user :- message of the day\r\n"
          ":c128user!u@h JOIN #retro\r\n"
          ":irc.example.org 332 c128user #retro :Old computers \00304welcome\r\n"
          ":irc.example.org 333 c128user #retro bob 1700000000\r\n"
          ":irc.example.org 353 c128user = #retro :@bob +amy c128user\r\n"
          ":irc.example.org 366 c128user #retro :End of /NAMES list.\r\n"},
    /* The channel window is selected; names from the join are not listed. */
    {'R', "c128user joined #retro"}, {'R', "Topic: Old computers welcome"},
    {'N', "message of the day"}, {'N', "Names:"}, {'N', "End of /NAMES"},
    {'S', ":bob!b@h PRIVMSG #retro :hello \xf0\x9f\x98\x80 world\r\n"
          ":amy!a@h PRIVMSG #retro :\001ACTION waves\001\r\n"
          ":amy!a@h PRIVMSG #retro :\001VERSION\001\r\n"
          ":bob!b@h PRIVMSG #retro :C128USER: are you there?\r\n"},
    {'R', "<bob> hello ? world"}, {'R', "* amy waves"}, {'N', "VERSION"},
    {'R', "<bob> C128USER: are you there?"},
    /* A message, an action, and text that starts with a slash. */
    {'K', "Hello ax" DEL "ll!\r"}, {'W', "PRIVMSG #retro :Hello all!\r\n"}, {'R', "<c128user> Hello all!"},
    {'K', "/me types\r"}, {'W', "PRIVMSG #retro :\001ACTION types\001\r\n"}, {'R', "* c128user types"},
    {'K', "//etc\r"}, {'W', "PRIVMSG #retro :/etc\r\n"}, {'R', "<c128user> /etc"},
    {'K', "/bogus\r"}, {'R', "Unknown command."}, {'W', ""},
    /* Names on request. */
    {'K', F5}, {'W', "NAMES #retro\r\n"},
    {'S', ":irc.example.org 353 c128user = #retro :@bob +amy c128user\r\n"
          ":irc.example.org 366 c128user #retro :End\r\n"},
    {'R', "Names: "}, {'R', "bob +amy c128user"},
    /* Other people come and go. */
    {'S', ":dan!d@h JOIN #retro\r\n:dan!d@h NICK :daniel\r\n"
          ":daniel!d@h PART #retro :bye now\r\n:amy!a@h QUIT :Ping timeout\r\n"
          ":bob!b@h TOPIC #retro :New topic\r\n:bob!b@h MODE #retro +o amy\r\n"},
    {'R', "dan joined #retro"}, {'R', "dan is now known as daniel"},
    {'R', "daniel left #retro (bye now)"}, {'R', "amy quit (Ping timeout)"},
    {'R', "bob changed the topic to New topic"}, {'R', "bob sets mode +o amy"},
    /* A private message opens a window, marked but not selected. */
    {'S', ":amy!a@h PRIVMSG c128user :psst\r\n"},
    {'T', " amy "}, {'N', "psst"},
    {'K', F1}, {'R', "<amy> psst"}, {'N', "Hello all!"},
    {'K', "ok\r"}, {'W', "PRIVMSG amy :ok\r\n"}, {'R', "<c128user> ok"},
    /* F1 goes round: Server, then the channel again, redrawn from bank 1. */
    {'K', F1}, {'R', "message of the day"}, {'R', "Welcome to the Example network"}, {'N', "<amy> psst"},
    {'K', "text\r"}, {'R', "Join a channel first"}, {'W', ""},
    {'K', F1}, {'R', "<c128user> Hello all!"}, {'R', "bob sets mode +o amy"},
    /* /msg to a new nickname selects its window. */
    {'K', "/msg bob hi there\r"}, {'W', "PRIVMSG bob :hi there\r\n"}, {'R', "<c128user> hi there"}, {'T', " bob "},
    /* F3 closes a private window at once. */
    {'K', F3}, {'t', " bob "}, {'R', "message of the day"},
    /* A second channel, a nickname change, and errors from the server. */
    {'K', "/join #c64 key\r"}, {'W', "JOIN #c64 key\r\n"},
    {'S', ":c128user!u@h JOIN :#c64\r\n"}, {'R', "c128user joined #c64"},
    {'K', "/nick Lukas\r"}, {'W', "NICK Lukas\r\n"},
    {'S', ":c128user!u@h NICK Lukas\r\n"},
    {'K', "now\r"}, {'W', "PRIVMSG #c64 :now\r\n"}, {'R', "<Lukas> now"},
    {'K', "/join nochannel\r"}, {'R', "Use /join #channel"},
    {'S', ":irc.example.org 404 Lukas #c64 :Cannot send to channel\r\n"},
    {'R', "#c64 Cannot send to channel"},
    /* F3 on a channel parts; the window closes when the server confirms. */
    {'K', F3}, {'W', "PART #c64\r\n"}, {'T', " #c64 "},
    {'S', ":Lukas!u@h PART #c64\r\n"}, {'t', " #c64 "}, {'R', "You left #c64"},
    /* Kicked: the window stays, but nothing can be sent there. */
    {'K', F1},
    {'S', ":bob!b@h KICK #retro Lukas :behave\r\n"}, {'R', "Lukas was kicked by bob (behave)"},
    {'K', "oops\r"}, {'R', "Join that channel first"}, {'W', ""},
    {'K', "/help\r"}, {'R', "/query name  /me action  /quit"},
    {'K', F7}, {'W', "QUIT :80Terminal C128\r\n"},
    {0, 0}
};
/* The saved settings are used; the nickname is taken; the server ends it. */
static const struct step refused[] = {
    {'R', "irc.example.org"}, {'R', "#retro"},
    {'K', "\r\r\r\r"},
    {'W', REGISTER},
    {'S', ":srv 433 * c128user :Nickname is already in use\r\n"},
    {'W', "NICK c128user_\r\n"}, {'R', "Nickname in use, trying c128user"},
    /* Nothing is read after the line that ends the session: no CR LF pair. */
    {'S', ":srv 465 c128user_ :You are banned\n"},
    {'R', "You are banned"}, {'R', "Disconnected: the server refused the registration."},
    {'K', " "},
    {0, 0}
};
/* Long lines wrap at spaces; lines over 512 bytes are dropped whole; the
 * connection closing ends the session. */
static const struct step wrapping[] = {
    {'K', "\r\r\r\r"},
    {'W', REGISTER},
    {'S', ":srv 001 c128user :Hi\r\n"}, {'W', "JOIN #retro\r\n"},
    {'S', ":c128user!u@h JOIN #retro\r\n"},
    {'S', ":bob!b@h PRIVMSG #retro :one two three four five six seven eight nine ten "
          "eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen\r\n"},
    {'R', "<bob> one two three four five six seven eight nine ten eleven twelve thirteen"},
    {'R', "  fourteen fifteen sixteen seventeen eighteen"},
    {'S', ":bob!b@h PRIVMSG #retro :"
          "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
          "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
          "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
          "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
          "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
          "0123456789 TOOLONG\r\n:bob!b@h PRIVMSG #retro :after\r\n"},
    {'N', "TOOLONG"}, {'R', "<bob> after"},
    {'X', ""},
    {'R', "Disconnected: Connection closed by the server."},
    {'K', " "},
    {0, 0}
};
/* F7 on the form goes back without connecting or saving. */
static const struct step cancelled_form[] = {
    {'K', "x" F7},
    {0, 0}
};
/* Invalid entries are reported and can be corrected. */
static const struct step invalid[] = {
    {'K', "\r" HOME "0\r\r\r"}, {'R', "Enter a port from 1 to 65535."},
    {'K', "\r" HOME "6667\r" HOME "9lives\r\r"}, {'R', "Enter a nickname"},
    {'K', "\r\r" HOME "c128user\r" HOME "retro\r"}, {'R', "Channels start with #"},
    {'K', "\r\r\r" HOME "#retro\r"},
    {'W', REGISTER},
    {'K', F7}, {'W', "QUIT :80Terminal C128\r\n"},
    {0, 0}
};

int main(void)
{
    uint8_t row;
    run(session);
    assert(connects == 1 && !strcmp(connected_host, "irc.example.org") && connected_port == 6667);
    assert(saves == 1 && file_exists);
    file_data[file_length] = 0;
    assert(!strcmp(file_data, "80TIRC 1\r\nirc.example.org\r\n6667\r\nc128user\r\n#retro\r\n"));
    /* The screen's frame: title, window tabs, rule, prompt and key bar. */
    assert(on_screen(0, "80Terminal ") && on_screen(0, "IRC") && on_screen(1, " Server "));
    assert(on_screen(23, ">") && on_screen(24, "Next window") && on_screen(24, "Disconnect"));
    /* Colours: own nickname amber, others cyan, events muted, errors red. */
    row = find_row("Lukas was kicked by bob");
    assert(row && attr_at(0, row) == 1);
    row = find_row("Join that channel first");
    assert(row && attr_at(0, row) == 9);

    /* A second visit keeps the settings in RAM: the file is not read again. */
    file_exists = 0;
    write_limit = 3;
    run(refused);
    assert(connects == 2 && saves == 1);
    /* An ended session waits for a key before going back. */
    assert(on_screen(24, "Any key") && on_screen(24, "Main screen"));
    write_limit = 0;
    run(wrapping);
    assert(connects == 3);

    /* A restart reads the file, with LF line endings and no channels. */
    irc_config_loaded = 0;
    put_file("80TIRC 1\nirc.example.org\n6667\nc128user\n\n");
    run(cancelled_form);
    assert(connects == 3 && saves == 1 && closes == 3);
    assert(on_screen(6, "irc.example.org") && on_screen(12, "Channels") && !on_screen(12, "retro"));
    /* The cancelled edit ("x" added to the server) is not kept. */
    run(invalid);
    assert(connects == 4 && !strcmp(connected_host, "irc.example.org") && saves == 2);
    /* A damaged file falls back to the defaults, without a nickname. */
    irc_config_loaded = 0;
    put_file("80TIRC 1\nirc.example.org\nport\nc128user\n");
    run(cancelled_form);
    assert(on_screen(6, "irc.libera.chat") && on_screen(8, "6667") && !on_screen(10, "c128user"));
    puts("IRC form, settings file, chat windows and protocol tests passed");
    return 0;
}
