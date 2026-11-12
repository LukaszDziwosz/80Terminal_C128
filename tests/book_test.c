/* The uibook overlay on the host: an in-memory Commodore DOS, bank 1 and
 * VDC screen, and a scripted keyboard. */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "stubs/conio.h"
#include "book.h"
#include "book_disk.h"
#include "bank1.h"
#include "phonebook.h"

struct book_state book;
static struct connect_request spare_request;
/* Only the phonebook part of bank 1, so this also fits a 6502 test run. */
static uint8_t ram1[BANK1_RECENT_END - BANK1_PHONEBOOK], vdc[0x1000];
#define RAM1(a) (ram1 + (a) - BANK1_PHONEBOOK)

/* ---- Bank 1 and VDC ---- */
void bank1_read(void *destination, uint16_t source, uint16_t length)
{ assert(source >= BANK1_PHONEBOOK && source + length <= BANK1_RECENT_END); memcpy(destination, RAM1(source), length); }
void bank1_write(uint16_t destination, const void *source, uint16_t length)
{ assert(destination >= BANK1_PHONEBOOK && destination + length <= BANK1_RECENT_END); memcpy(RAM1(destination), source, length); }
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
static int on_screen(uint8_t row, const char *text) { return strstr(row_text(row), text) != NULL; }
static int anywhere(const char *text)
{
    uint8_t row;
    for (row = 0; row < 25; ++row) if (on_screen(row, text)) return 1;
    return 0;
}

/* ---- Keyboard ---- */
static const char *script;
static int script_at;
/* Like Oscar64's conio getch(): RETURN arrives as 10, not 13. */
char getch(void) { char c; assert(script[script_at]); c = script[script_at++]; return c == 13 ? 10 : c; }
void clrscr(void) {}
void gotoxy(char x, char y) { (void)x; (void)y; }
void putch(char c) { (void)c; }
uint8_t platform_key_pending(void) { return 1; }
static void keys(const char *text) { script = text; script_at = 0; }
static void all_keys_used(void) { assert(!script[script_at]); }

/* ---- Commodore DOS on one drive ---- */
#define FILES 4
static struct { char name[17]; char data[1024]; uint16_t length; uint8_t used; } files[FILES];
static int open_file = -1, write_mode, fail_write, commands;
static uint16_t cursor;
char book_disk_message[40];
static int find(const char *name)
{
    int i;
    for (i = 0; i < FILES; ++i) if (files[i].used && !strcmp(files[i].name, name)) return i;
    return -1;
}
static void put_file(const char *name, const char *data)
{
    int i = find(name);
    if (i < 0) for (i = 0; files[i].used; ++i) {}
    files[i].used = 1;
    strcpy(files[i].name, name);
    files[i].length = (uint16_t)strlen(data);
    memcpy(files[i].data, data, files[i].length);
}
static const char *file(const char *name)
{
    int i = find(name);
    if (i < 0) return NULL;
    files[i].data[files[i].length] = 0;
    return files[i].data;
}
static uint8_t status(uint8_t code, const char *text) { strcpy(book_disk_message, text); return code; }
uint8_t book_disk_open(const char *name, uint8_t write)
{
    int i = find(name);
    assert(open_file < 0);
    if (!write) {
        if (i < 0) return status(62, "62,FILE NOT FOUND,00,00");
    } else {
        if (i >= 0) return status(63, "63,FILE EXISTS,00,00");
        for (i = 0; files[i].used; ++i) {}
        files[i].used = 1; strcpy(files[i].name, name); files[i].length = 0;
    }
    open_file = i; write_mode = write; cursor = 0;
    return status(0, "00, OK,00,00");
}
uint8_t book_disk_read(char *buffer, uint8_t capacity)
{
    uint8_t n = 0;
    assert(open_file >= 0 && !write_mode);
    /* Odd chunk sizes split lines across reads. */
    if (capacity > 7) capacity = 7;
    while (n < capacity && cursor < files[open_file].length) buffer[n++] = files[open_file].data[cursor++];
    return n;
}
uint8_t book_disk_write(const char *data, uint8_t length)
{
    assert(open_file >= 0 && write_mode);
    if (fail_write && files[open_file].length + length > 60) return 1;
    memcpy(files[open_file].data + files[open_file].length, data, length);
    files[open_file].length += length;
    return 0;
}
uint8_t book_disk_close(void)
{
    assert(open_file >= 0);
    open_file = -1;
    return fail_write ? status(72, "72,DISK FULL,00,00") : status(0, "00, OK,00,00");
}
uint8_t book_disk_command(const char *command)
{
    char target[20];
    const char *eq;
    int i;
    ++commands;
    assert(strlen(command) <= 40);
    if (command[0] == 's' && command[1] == ':') {
        i = find(command + 2);
        if (i >= 0) files[i].used = 0;
        return status(1, "01, FILES SCRATCHED,01,00");
    }
    assert(command[0] == 'r' && command[1] == ':');
    eq = strchr(command, '=');
    memcpy(target, command + 2, (size_t)(eq - command - 2)); target[eq - command - 2] = 0;
    if (find(target) >= 0) return status(63, "63,FILE EXISTS,00,00");
    i = find(eq + 1);
    if (i < 0) return status(62, "62,FILE NOT FOUND,00,00");
    strcpy(files[i].name, target);
    return status(0, "00, OK,00,00");
}

/* ---- Adapter ---- */
static void poll(void) {}
static const struct net_backend backend = { "Test", "", NULL, NULL, poll, NULL, NULL, NULL, NULL };

static void reset(void)
{
    memset(files, 0, sizeof(files));
    memset(ram1, 0xaa, sizeof(ram1));
    open_file = -1; fail_write = commands = 0;
}
static void get_board(uint8_t i, struct phonebook_entry *e)
{ memcpy(e, RAM1(BANK1_PHONEBOOK + i * BANK1_ENTRY_STRIDE), sizeof(*e)); }
static void get_recent(uint8_t i, struct phonebook_entry *e)
{ memcpy(e, RAM1(BANK1_RECENT + i * BANK1_ENTRY_STRIDE), sizeof(*e)); }

static void test_first_start(void)
{
    struct connect_request request;
    reset();
    book_startup();
    assert(book.count == 1 && book.recent_count == 0);
    keys("\r");
    assert(book_run(&backend, &request) == UI_CONNECT);
    assert(!strcmp(request.name, "LukasSoft BBS") && !strcmp(request.hostname, "bbs.lukassoft.com"));
    assert(request.port == 48822 && request.profile == TELNET_PROFILE_ANSI_80);
    /* Nothing is written until the user changes something. */
    assert(file("phonebook") == NULL && commands == 0);
}
static void test_import(void)
{
    struct phonebook_entry e;
    reset();
    /* Mixed endings, comments, a Flutter-only mode, a broken line, no final newline. */
    put_file("phonebook", "80TPB 1\r\n# shared on a BBS\nOne|one.example|23|ansi80\r"
                          "Two|two.example|6400|petscii40\r\nVIC|vic.example|64|vic20\n"
                          "broken line\nThree|three.example|2323|VT100");
    put_file("recent", "80TPB 1\r\nOne|one.example|23|ansi80\r\n");
    book_startup();
    assert(book.count == 3 && book.recent_count == 1);
    get_board(1, &e);
    assert(!strcmp(e.name, "Two") && e.port == 6400 && e.profile == TELNET_PROFILE_PETSCII_80);
    get_board(2, &e);
    assert(!strcmp(e.hostname, "three.example") && e.profile == TELNET_PROFILE_VT100_80);
    keys("\x88");  /* F7 */
    assert(book_run(&backend, &spare_request) == UI_BACK);
    assert(on_screen(0, "Phonebook") && anywhere("Saved boards 3/50") && anywhere("Some boards use modes"));
    assert(anywhere("two.example:6400") && anywhere("PETSCII"));
}
static void test_add_edit_delete(void)
{
    struct phonebook_entry e;
    struct connect_request request;
    reset();
    put_file("phonebook", "80TPB 1\r\nOne|one.example|23|ansi80\r\n");
    book_startup();
    /* F1 add: name, host, port 23 kept, then VT100. */
    keys("\x85" "My board\r" "host.example\r" "\x14\x14" "6400\r" "3" "\x88");
    assert(book_run(&backend, &request) == UI_BACK);
    all_keys_used();
    assert(book.count == 2 && anywhere("Phonebook saved."));
    assert(!strcmp(file("phonebook"), "80TPB 1\r\nOne|one.example|23|ansi80\r\nMy board|host.example|6400|vt100\r\n"));
    assert(file("phonebook.new") == NULL);
    /* The new board is selected: F3 edit it to PETSCII (port kept). */
    /* F4 is not a phonebook key and is ignored. */
    keys("\x8a" "\x86" "\r" "\r" "\r" "2" "\x88");
    assert(book_run(&backend, &request) == UI_BACK);
    get_board(1, &e);
    assert(e.profile == TELNET_PROFILE_PETSCII_80 && e.port == 6400);
    /* '|' is never accepted in a name. */
    keys("\x91" "\x86" "|X\r" "\r" "\r" "\r" "\x88");
    assert(book_run(&backend, &request) == UI_BACK);
    get_board(0, &e);
    assert(!strcmp(e.name, "OneX"));
    /* F5 delete, answer N, then Y. */
    keys("\x85" "\x88" "\x87" "n" "\x87" "y" "\x88");
    assert(book_run(&backend, &request) == UI_BACK);
    assert(book.count == 1);
    get_board(0, &e);
    assert(!strcmp(e.name, "My board"));
    assert(!strcmp(file("phonebook"), "80TPB 1\r\nMy board|host.example|6400|petscii80\r\n"));
    /* CRSR down at the end does nothing; RETURN dials. */
    keys("\x11\r");
    assert(book_run(&backend, &request) == UI_CONNECT && !strcmp(request.hostname, "host.example"));
}
static void test_interrupted_save(void)
{
    /* Only phonebook.new survived: startup reads it. */
    reset();
    put_file("phonebook.new", "80TPB 1\r\nKept|kept.example|23|ansi80\r\n");
    book_startup();
    assert(book.count == 1);
    keys("\r");
    assert(book_run(&backend, &spare_request) == UI_CONNECT && !strcmp(spare_request.hostname, "kept.example"));
}
static void test_failed_save_keeps_file(void)
{
    const char *before = "80TPB 1\r\nOne|one.example|23|ansi80\r\nTwo|two.example|23|ansi80\r\n";
    reset();
    put_file("phonebook", before);
    book_startup();
    fail_write = 1;
    keys("\x87" "y" "\x88");
    assert(book_run(&backend, &spare_request) == UI_BACK);
    assert(anywhere("Not saved, disk error 72,DISK FULL"));
    assert(!strcmp(file("phonebook"), before));
}
static void test_recent(void)
{
    struct phonebook_entry e;
    struct connect_request request;
    uint8_t i;
    reset();
    book_startup();
    memset(&request, 0, sizeof(request));
    request.port = 23; request.profile = TELNET_PROFILE_ANSI_80;
    /* Ten different hosts: only the newest nine are kept, newest first. */
    for (i = 0; i < 10; ++i) {
        sprintf(request.hostname, "host%u.example", i);
        book_note_recent(&request);
    }
    assert(book.recent_count == RECENT_MAX);
    get_recent(0, &e);
    assert(!strcmp(e.hostname, "host9.example") && !strcmp(e.name, "host9.example"));
    get_recent(8, &e);
    assert(!strcmp(e.hostname, "host1.example"));
    /* Dialling one again moves it to the top without a duplicate; a
     * phonebook name is kept; the host comparison ignores case. */
    strcpy(request.hostname, "HOST5.example");
    strcpy(request.name, "Five");
    book_note_recent(&request);
    assert(book.recent_count == RECENT_MAX);
    get_recent(0, &e);
    assert(!strcmp(e.name, "Five"));
    for (i = 1; i < RECENT_MAX; ++i) { get_recent(i, &e); assert(strcmp(e.hostname, "host5.example")); }
    {
        static const char start[] = "80TPB 1\r\nFive|HOST5.example|23|ansi80\r\nhost9.example|";
        assert(!strncmp(file("recent"), start, sizeof(start) - 1));
    }
    /* The phonebook file is never touched by recents. */
    assert(file("phonebook") == NULL);
}
int main(void)
{
    test_first_start();
    test_import();
    test_add_edit_delete();
    test_interrupted_save();
    test_failed_save_keeps_file();
    test_recent();
    puts("Phonebook screen, file, editor and recent tests passed");
    return 0;
}
