/* The uibook overlay: phonebook screen, board editor and recent connections. */
#ifdef BOOK_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#include <c64/kernalio.h>
#endif
#include <stddef.h>
#include <string.h>
#include "book.h"
#include "book_disk.h"
#include "bank1.h"
#include "phonebook.h"
#include "platform.h"

#if defined(__OSCAR64C__) && !defined(BOOK_TEST)
#pragma code(bookcode)
#pragma data(bookdata)
#pragma bss(bookbss)
#endif
#ifdef __OSCAR64C__
/* UI speed does not matter; slot space does. */
#pragma optimize(push, size, outline)
#define BOOK_ENTRY __noinline
#else
#define BOOK_ENTRY
#endif
#include "draw.h"
/* The file format code is only needed here, so it is part of this overlay. */
#include "../core/phonebook.c"

typedef char entry_fits_stride[sizeof(struct phonebook_entry) <= BANK1_ENTRY_STRIDE ? 1 : -1];

#define LIST_ROW 4
#define LIST_ROWS 15
#define NOTE_ROW 21
#define NAME_COL 5
#define HOST_COL 31
#define MODE_COL 69

/* bookbss: outside the overlay file, set before use. */
static struct phonebook_entry entry;
static char line[PHONEBOOK_LINE_SIZE];
static char chunk[64];
static char text[72];
static char note_text[72];
static uint8_t selected, top, skipped, unreadable, load_status;
static const char *note;
static uint8_t note_attr;

static const char *const list_keys[] = {
    "RETURN", "Dial", "F1", "Add", "F3", "Edit", "F5", "Delete", "F7", "Back", 0 };
static const char *const edit_keys[] = {
    "RETURN", "Next", "DEL", "Erase", "HOME", "Clear", "F7", "Cancel", 0 };
static const char *const profile_keys[] = {
    "1", "ANSI", "2", "PETSCII", "3", "VT100", "RETURN", "Keep", "F7", "Cancel", 0 };
static const char *const confirm_keys[] = { "Y", "Delete", "N", "Keep", 0 };

/* ---- C128 disk access (tests provide their own) ---- */
#ifndef BOOK_TEST
char book_disk_message[40];
/* DOS commands such as "r:phonebook=phonebook.new" need the room. */
static char disk_name[41];
static uint8_t disk_speed;

static char petscii(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
/* ASCII to PETSCII for DOS names, modes and commands: lowercase letters
 * become $41-$5A, which a C128 lists in lowercase and DOS reads as the
 * letters of ",s,w" or "s:". */
static void petscii_name(const char *name, const char *suffix)
{
    uint8_t n = 0;
    while (*name && n < sizeof(disk_name) - 1) disk_name[n++] = petscii(*name++);
    while (*suffix && n < sizeof(disk_name) - 1) disk_name[n++] = petscii(*suffix++);
    disk_name[n] = 0;
}
static uint8_t read_status(void)
{
    int n;
    krnio_pstatus[15] = KRNIO_OK;
    n = krnio_read(15, book_disk_message, sizeof(book_disk_message) - 1);
    if (n < 2) { strcpy(book_disk_message, "74,DRIVE NOT READY"); return 74; }
    while (n && (book_disk_message[n - 1] == 13 || book_disk_message[n - 1] == 10)) --n;
    book_disk_message[n] = 0;
    return (uint8_t)((book_disk_message[0] - '0') * 10 + book_disk_message[1] - '0');
}
static uint8_t open_command(const char *command)
{
    disk_speed = platform_slow();
    krnio_setbnk(0, 0);
    petscii_name(command, "");
    krnio_setnam(disk_name);
    if (!krnio_open(15, platform_device(), 15)) {
        platform_restore_speed(disk_speed);
        strcpy(book_disk_message, "74,DRIVE NOT READY");
        return 74;
    }
    return 0;
}
uint8_t book_disk_command(const char *command)
{
    uint8_t code = open_command(command);
    if (code) return code;
    code = read_status();
    krnio_close(15);
    platform_restore_speed(disk_speed);
    return code;
}
uint8_t book_disk_open(const char *name, uint8_t write)
{
    uint8_t code = open_command("");
    if (code) return code;
    petscii_name(name, write ? ",s,w" : ",s,r");
    krnio_setnam(disk_name);
    krnio_open(2, platform_device(), 2);
    code = read_status();
    if (code) {
        /* Close the file before the command channel, which closes all. */
        krnio_close(2);
        krnio_close(15);
        platform_restore_speed(disk_speed);
    }
    return code;
}
uint8_t book_disk_read(char *buffer, uint8_t capacity)
{
    int n = krnio_read(2, buffer, capacity);
    return n > 0 ? (uint8_t)n : 0;
}
uint8_t book_disk_write(const char *data, uint8_t length)
{
    return krnio_write(2, data, length) == length ? 0 : 1;
}
uint8_t book_disk_close(void)
{
    uint8_t code;
    krnio_close(2);
    code = read_status();
    krnio_close(15);
    platform_restore_speed(disk_speed);
    return code;
}
#endif

/* ---- Entries in bank 1 ---- */
static uint16_t slot(uint16_t base, uint8_t index)
{
    return base + (uint16_t)index * BANK1_ENTRY_STRIDE;
}
static void get(uint16_t base, uint8_t index, struct phonebook_entry *to)
{
    bank1_read(to, slot(base, index), sizeof(*to));
}
static void put(uint16_t base, uint8_t index, const struct phonebook_entry *from)
{
    bank1_write(slot(base, index), from, sizeof(*from));
}

/* ---- Files ---- */
static void add_line(uint16_t base, uint8_t *count, uint8_t max)
{
    switch (phonebook_parse_line(line, &entry)) {
    case PHONEBOOK_LINE_ENTRY:
        if (*count < max) put(base, (*count)++, &entry);
        else ++skipped;
        break;
    case PHONEBOOK_LINE_UNSUPPORTED: ++skipped; break;
    case PHONEBOOK_LINE_INVALID: ++unreadable; break;
    }
}
/* Returns the number of entries read; load_status is the DOS status. */
static uint8_t load(const char *name, uint16_t base, uint8_t max)
{
    uint8_t count = 0, length = 0, overflow = 0, n, i;
    char c;
    skipped = unreadable = 0;
    load_status = book_disk_open(name, 0);
    if (load_status) return 0;
    while ((n = book_disk_read(chunk, sizeof(chunk))) != 0) {
        for (i = 0; i < n; ++i) {
            c = chunk[i];
            if (c == 13 || c == 10) {
                line[length] = 0;
                if (overflow) ++unreadable;
                else if (length) add_line(base, &count, max);
                length = overflow = 0;
            } else if (length < sizeof(line) - 1) line[length++] = c;
            else overflow = 1;
        }
    }
    line[length] = 0;
    if (overflow) ++unreadable;
    else if (length) add_line(base, &count, max);
    load_status = book_disk_close();
    return count;
}
static void copy_name(char *to, const char *a, const char *b)
{
    while (*a) *to++ = *a++;
    while (*b) *to++ = *b++;
    *to = 0;
}
/* Writes name.new, then replaces name with it, so a failed write (a full
 * disk, say) leaves the previous file intact. Returns the DOS status. */
static uint8_t save(const char *name, uint16_t base, uint8_t count)
{
    char temp[16], command[40];
    uint8_t code, i, n;
    copy_name(temp, name, ".new");
    copy_name(command, "s:", temp);
    book_disk_command(command);
    code = book_disk_open(temp, 1);
    if (code) return code;
    copy_name(line, PHONEBOOK_HEADER, "\r\n");
    code = book_disk_write(line, (uint8_t)strlen(line));
    for (i = 0; i < count && !code; ++i) {
        get(base, i, &entry);
        n = phonebook_format_line(line, &entry);
        if (n) code = book_disk_write(line, n);
    }
    i = book_disk_close();
    if (code || i) return i ? i : 74;
    copy_name(command, "s:", name);
    book_disk_command(command);
    copy_name(command, "r:", name);
    copy_name(command + strlen(command), "=", temp);
    return book_disk_command(command);
}

/* ---- Startup and recent connections ---- */
BOOK_ENTRY void book_startup(void)
{
    uint8_t not_found;
    ui_clear();
    ui_title("Phonebook");
    ui_text(NAME_COL, 3, "Reading the phonebook and recent connections...", UI_TEXT);
    note = NULL;
    book.count = load("phonebook", BANK1_PHONEBOOK, PHONEBOOK_MAX);
    /* A save interrupted between scratching the old file and renaming the
     * new one leaves only phonebook.new: use it. */
    if (load_status == 62) book.count = load("phonebook.new", BANK1_PHONEBOOK, PHONEBOOK_MAX);
    not_found = load_status == 62;
    if (not_found) {
        /* A first start, or a deleted file: offer the support board. */
        strcpy(entry.name, "LukasSoft BBS");
        strcpy(entry.hostname, "bbs.lukassoft.com");
        entry.port = 48822;
        entry.profile = TELNET_PROFILE_ANSI_80;
        put(BANK1_PHONEBOOK, 0, &entry);
        book.count = 1;
    } else if (load_status) {
        note = "The phonebook file could not be read completely.";
        note_attr = UI_ERROR;
    } else if (skipped || unreadable) {
        note = skipped ? "Some boards use modes this C128 client cannot run, or exceed 50."
                       : "Some lines in the phonebook file could not be read.";
        note_attr = UI_MUTED;
    }
    book.recent_count = load("recent", BANK1_RECENT, RECENT_MAX);
    selected = top = 0;
}
static uint8_t same_host(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { ++a; ++b; }
    return !*a && !*b;
}
BOOK_ENTRY void book_note_recent(const struct connect_request *request)
{
    struct phonebook_entry *recent = &entry;
    uint8_t i, match = book.recent_count;
    static struct phonebook_entry other;
    for (i = 0; i < book.recent_count; ++i) {
        get(BANK1_RECENT, i, &other);
        if (other.port == request->port && other.profile == request->profile &&
            same_host(other.hostname, request->hostname)) { match = i; break; }
    }
    if (match == RECENT_MAX) match = RECENT_MAX - 1;   /* full: drop the oldest */
    else if (match == book.recent_count) ++book.recent_count;
    for (i = match; i; --i) {
        get(BANK1_RECENT, (uint8_t)(i - 1), &other);
        put(BANK1_RECENT, i, &other);
    }
    strncpy(recent->name, request->name[0] ? request->name : request->hostname, PHONEBOOK_NAME_SIZE);
    recent->name[PHONEBOOK_NAME_SIZE] = 0;
    strcpy(recent->hostname, request->hostname);
    recent->port = request->port;
    recent->profile = request->profile;
    put(BANK1_RECENT, 0, recent);
    ui_clear();
    ui_title("Phonebook");
    ui_text(NAME_COL, 3, "Saving recent connections...", UI_TEXT);
    save("recent", BANK1_RECENT, book.recent_count);
}

/* ---- Screens ---- */
static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
}
/* Oscar64's getch() returns RETURN as 10 ('\n'), not 13. */
static uint8_t is_return(uint8_t key) { return key == 10 || key == 13; }
static uint8_t wait_key(const struct net_backend *backend)
{
    while (!platform_key_pending()) backend->poll();
    return (uint8_t)getch();
}
static const char *mode_label(enum telnet_profile profile)
{
    return profile == TELNET_PROFILE_PETSCII_80 ? "PETSCII" :
           profile == TELNET_PROFILE_VT100_80 ? "VT100" : "ANSI";
}
static void host_port(const struct phonebook_entry *e)
{
    char digits[5], *out = text;
    uint16_t port = e->port;
    uint8_t n = 0;
    strcpy(text, e->hostname);
    while (*out) ++out;
    *out++ = ':';
    do { digits[n++] = (char)('0' + port % 10); port /= 10; } while (port);
    while (n) *out++ = digits[--n];
    *out = 0;
}
static void draw_row(uint8_t row)
{
    uint8_t index = (uint8_t)(top + row), y = (uint8_t)(LIST_ROW + row), bar;
    if (index >= book.count) { ui_text_field(4, y, 72, "", UI_TEXT); return; }
    get(BANK1_PHONEBOOK, index, &entry);
    bar = index == selected;
    host_port(&entry);
    ui_text_field(4, y, 1, "", bar ? UI_BAR : UI_TEXT);
    ui_text_field(NAME_COL, y, HOST_COL - NAME_COL, entry.name, bar ? UI_BAR : UI_HEADING);
    ui_text_field(HOST_COL, y, MODE_COL - HOST_COL, text, bar ? UI_BAR : UI_INFO);
    ui_text_field(MODE_COL, y, 7, mode_label(entry.profile), bar ? UI_BAR : UI_MUTED);
}
static void draw_list(void)
{
    uint8_t row;
    char heading[24];
    copy_name(heading, "Saved boards ", "");
    heading[13] = (char)('0' + book.count / 10);
    heading[14] = (char)('0' + book.count % 10);
    copy_name(heading + 15, "/50", "");
    if (heading[13] == '0') memmove(heading + 13, heading + 14, 5);
    ui_clear();
    ui_title("Phonebook");
    ui_panel(2, 19, heading);
    if (!book.count) ui_text(NAME_COL, LIST_ROW, "No saved boards. F1 adds one.", UI_MUTED);
    for (row = 0; row < LIST_ROWS; ++row) draw_row(row);
    if (note) ui_text_field(NAME_COL, NOTE_ROW, 70, note, note_attr);
    ui_text(NAME_COL, NOTE_ROW + 1, "CRSR up/down selects a board.", UI_MUTED);
    ui_keys(list_keys);
}
static void show_save(uint8_t code)
{
    if (code) {
        copy_name(note_text, "Not saved, disk error ", book_disk_message);
        note = note_text; note_attr = UI_ERROR;
    } else {
        note = "Phonebook saved."; note_attr = UI_GOOD;
    }
}
/* kind: 0 hostname (no spaces), 1 digits, 2 name (spaces allowed).
 * '|' separates fields in the file, so it is never accepted. */
static uint8_t edit(const struct net_backend *backend, uint8_t row, uint8_t width,
                    char *value, uint8_t capacity, uint8_t kind)
{
    uint8_t length = (uint8_t)strlen(value), key, start;
    for (;;) {
        start = length >= width ? (uint8_t)(length - width + 1) : 0;
        ui_text_field(16, row, width, value + start, UI_HEADING);
        ui_text_field((uint8_t)(16 + length - start), row, 1, "", UI_BAR);
        key = wait_key(backend);
        if (cancelled(key)) return 0;
        if (is_return(key)) {
            ui_text_field(16, row, width, value + start, UI_HEADING);
            return length != 0;
        }
        if (key == PETSCII_HOME) { length = 0; value[0] = 0; }
        else if (key == PETSCII_DEL) { if (length) value[--length] = 0; }
        else if (length < capacity && key <= 126 && key != '|' &&
                 (kind == 1 ? key >= '0' && key <= '9' : key >= (kind == 2 ? 32 : 33))) {
            value[length++] = (char)key; value[length] = 0;
        }
    }
}
static void label(uint8_t row, const char *name)
{
    ui_text(NAME_COL, row, name, UI_MUTED);
}
/* Fills entry in place. Returns 1 if the user finished it. */
static uint8_t edit_entry(const struct net_backend *backend, const char *heading)
{
    char port_text[6];
    uint16_t port;
    uint8_t i, key, valid;
    for (;;) {
        ui_clear();
        ui_title("Phonebook");
        ui_panel(2, 11, heading);
        label(4, "Name"); label(6, "Host"); label(8, "Port"); label(10, "Terminal");
        ui_text_field(16, 4, 25, entry.name, UI_HEADING);
        ui_text_field(16, 6, 60, entry.hostname, UI_HEADING);
        port = entry.port; i = 5; port_text[5] = 0;
        do { port_text[--i] = (char)('0' + port % 10); port /= 10; } while (port);
        memmove(port_text, port_text + i, (uint8_t)(6 - i));
        ui_text_field(16, 8, 6, port_text, UI_HEADING);
        ui_text_field(16, 10, 8, mode_label(entry.profile), UI_HEADING);
        ui_text(NAME_COL, 14, "Name up to 24 characters; hostname or IPv4 address; port 1-65535.", UI_MUTED);
        ui_keys(edit_keys);
        if (!edit(backend, 4, 25, entry.name, PHONEBOOK_NAME_SIZE, 2) ||
            !edit(backend, 6, 60, entry.hostname, PHONEBOOK_HOST_SIZE, 0) ||
            !edit(backend, 8, 6, port_text, 5, 1)) return 0;
        port = 0; valid = 1;
        for (i = 0; port_text[i]; ++i) {
            if (port > 6553 || (port == 6553 && port_text[i] > '5')) valid = 0;
            port = (uint16_t)(port * 10 + (uint8_t)(port_text[i] - '0'));
        }
        /* Names cannot start with spaces in the file: trim them here. */
        while (entry.name[0] == ' ') memmove(entry.name, entry.name + 1, strlen(entry.name));
        if (valid && port && entry.name[0]) break;
        ui_text(NAME_COL, 12, "Check the name and port (1-65535). Press a key.", UI_ERROR);
        wait_key(backend);
    }
    entry.port = port;
    ui_text(30, 10, "1", UI_ACTION); ui_text(32, 10, "ANSI", UI_TEXT);
    ui_text(39, 10, "2", UI_ACTION); ui_text(41, 10, "PETSCII", UI_TEXT);
    ui_text(51, 10, "3", UI_ACTION); ui_text(53, 10, "VT100", UI_TEXT);
    ui_keys(profile_keys);
    for (;;) {
        key = wait_key(backend);
        if (cancelled(key)) return 0;
        if (key == '1') entry.profile = TELNET_PROFILE_ANSI_80;
        else if (key == '2') entry.profile = TELNET_PROFILE_PETSCII_80;
        else if (key == '3') entry.profile = TELNET_PROFILE_VT100_80;
        else if (!is_return(key)) continue;
        return 1;
    }
}
static void move_selection(uint8_t to)
{
    selected = to;
    if (selected < top) top = selected;
    else if (selected >= top + LIST_ROWS) top = (uint8_t)(selected - LIST_ROWS + 1);
}

BOOK_ENTRY uint8_t book_run(const struct net_backend *backend, struct connect_request *request)
{
    uint8_t key, i, old_top;
    if (selected >= book.count) move_selection(book.count ? (uint8_t)(book.count - 1) : 0);
    draw_list();
    for (;;) {
        key = wait_key(backend);
        if (cancelled(key)) return UI_BACK;
        old_top = top;
        if (key == PETSCII_CURSOR_UP && selected) {
            i = selected; move_selection((uint8_t)(selected - 1));
            if (top != old_top) draw_list();
            else { draw_row((uint8_t)(i - top)); draw_row((uint8_t)(selected - top)); }
        } else if (key == PETSCII_CURSOR_DOWN && selected + 1 < book.count) {
            i = selected; move_selection((uint8_t)(selected + 1));
            if (top != old_top) draw_list();
            else { draw_row((uint8_t)(i - top)); draw_row((uint8_t)(selected - top)); }
        } else if (is_return(key) && book.count) {
            get(BANK1_PHONEBOOK, selected, &entry);
            strcpy(request->name, entry.name);
            strcpy(request->hostname, entry.hostname);
            request->port = entry.port;
            request->profile = entry.profile;
            return UI_CONNECT;
        } else if (key == PETSCII_F1) {
            if (book.count >= PHONEBOOK_MAX) {
                note = "The phonebook is full (50 boards)."; note_attr = UI_ERROR;
            } else {
                memset(&entry, 0, sizeof(entry));
                entry.port = 23;
                entry.profile = TELNET_PROFILE_ANSI_80;
                if (edit_entry(backend, "Add board")) {
                    put(BANK1_PHONEBOOK, book.count, &entry);
                    move_selection(book.count++);
                    show_save(save("phonebook", BANK1_PHONEBOOK, book.count));
                }
            }
            draw_list();
        } else if (key == PETSCII_F3 && book.count) {
            get(BANK1_PHONEBOOK, selected, &entry);
            if (edit_entry(backend, "Edit board")) {
                put(BANK1_PHONEBOOK, selected, &entry);
                show_save(save("phonebook", BANK1_PHONEBOOK, book.count));
            }
            draw_list();
        } else if (key == PETSCII_F5 && book.count) {
            get(BANK1_PHONEBOOK, selected, &entry);
            copy_name(text, "Delete ", entry.name);
            copy_name(text + strlen(text), "?", "");
            ui_text_field(NAME_COL, NOTE_ROW, 70, text, UI_ERROR);
            ui_keys(confirm_keys);
            do key = wait_key(backend); while (key != 'y' && key != 'Y' && key != 'n' && key != 'N' && !cancelled(key));
            if (key == 'y' || key == 'Y') {
                for (i = selected; i + 1 < book.count; ++i) {
                    get(BANK1_PHONEBOOK, (uint8_t)(i + 1), &entry);
                    put(BANK1_PHONEBOOK, i, &entry);
                }
                --book.count;
                if (selected >= book.count && selected) move_selection((uint8_t)(selected - 1));
                show_save(save("phonebook", BANK1_PHONEBOOK, book.count));
            }
            draw_list();
        }
    }
}

#ifdef __OSCAR64C__
#pragma optimize(pop)
#endif
#if defined(__OSCAR64C__) && !defined(BOOK_TEST)
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
