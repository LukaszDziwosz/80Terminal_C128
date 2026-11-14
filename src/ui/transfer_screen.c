/* The uixfer overlay: file transfers during a session (HELP). */
#ifdef XFER_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#endif
#include <stddef.h>
#include <string.h>
#include "transfer.h"
#include "disk.h"
#include "telnet.h"
#include "xmodem.h"
#include "platform.h"
#include "bank1.h"
#include "session.h"

#if defined(__OSCAR64C__) && !defined(XFER_TEST)
#pragma code(xfercode)
#pragma data(xferdata)
#pragma bss(xferbss)
#endif
#ifdef __OSCAR64C__
/* UI speed does not matter; slot space does. The transfer loop itself is
 * bounded by the network and the drive, not by this code. */
#pragma optimize(push, size, outline)
#define XFER_ENTRY __noinline
#else
#define XFER_ENTRY
#endif
#include "draw.h"

#define LABEL_COL 5
#define VALUE_COL 16
#define TICKS_PER_SECOND 60
#define START_TIMEOUT  (3 * TICKS_PER_SECOND)   /* repeat 'C' while waiting */
#define BLOCK_TIMEOUT  (10 * TICKS_PER_SECOND)
#define UPLOAD_START_TIMEOUT (60 * TICKS_PER_SECOND)
#define TYPE_PRG 1
#define TYPE_SEQ 2
#define TYPE_USR 3

/* Directory entries in bank 1 (BANK1_DIRECTORY). */
struct dir_entry {
    char name[17];   /* ASCII */
    uint8_t type;
    uint16_t blocks;
};
#define DIR_STRIDE 20
#define DIR_MAX ((BANK1_DIRECTORY_END - BANK1_DIRECTORY) / DIR_STRIDE)
#define LIST_ROW 4
#define LIST_ROWS 15

/* xferbss: outside the overlay file, set before use. */
static const struct net_backend *net;
static uint8_t out_queue[200], out_count, link_failed;
static uint8_t input[64];
static char file_name[17], drive_text[3], text[72];
static uint8_t file_type, file_open;
static uint32_t bytes;
static uint16_t retries, deadline;
static struct xmodem_receiver receiver;
static struct xmodem_sender sender;
static struct dir_entry dir;
static uint8_t dir_count, dir_selected, dir_top;
static char line_buffer[40];

static const char *const menu_keys[] = { "F1", "Download", "F3", "Upload", "F7", "Terminal", 0 };
static const char *const edit_keys[] = {
    "RETURN", "Next", "DEL", "Erase", "HOME", "Clear", "F7", "Cancel", 0 };
static const char *const type_keys[] = { "1", "PRG", "2", "SEQ", "3", "USR", "F7", "Cancel", 0 };
static const char *const upload_keys[] = { "1", "XMODEM", "2", "XMODEM-1K", "F7", "Cancel", 0 };
static const char *const list_keys[] = { "RETURN", "Send", "F1", "Drive", "F7", "Cancel", 0 };
static const char *const confirm_keys[] = { "Y", "Replace", "N", "Keep", 0 };
static const char *const cancel_keys[] = { "F7", "Cancel", 0 };
static const char *const any_key[] = { "Any key", "Transfer menu", 0 };
static const char *const type_names[] = { "", "PRG", "SEQ", "USR" };
static const char *const type_modes[] = { "", ",p", ",s", ",u" };

/* ---- Keyboard ---- */
static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
}
/* Oscar64's getch() returns RETURN as 10, not 13. */
static uint8_t is_return(uint8_t key) { return key == 10 || key == 13; }
static uint8_t wait_key(void)
{
    while (!platform_key_pending()) net->poll();
    return (uint8_t)getch();
}

/* ---- Text ---- */
/* By subtraction: a 32-bit division would pull Oscar64's divmod32 into
 * the resident program, which has no room for it. */
static const uint32_t powers[] = { 1000000000UL, 100000000UL, 10000000UL, 1000000UL,
                                   100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
static char *decimal(char *out, uint32_t value)
{
    uint8_t i, digit, started = 0;
    for (i = 0; i < 10; ++i) {
        digit = 0;
        while (value >= powers[i]) { value -= powers[i]; ++digit; }
        if (digit || started || i == 9) { *out++ = (char)('0' + digit); started = 1; }
    }
    *out = 0;
    return out;
}
static char *append(char *out, const char *s)
{
    while (*s) *out++ = *s++;
    *out = 0;
    return out;
}
static void label(uint8_t row, const char *name, const char *value, uint8_t attr)
{
    ui_text(LABEL_COL, row, name, UI_MUTED);
    ui_text_field(VALUE_COL, row, 60, value, attr);
}
static void show_number(uint8_t row, const char *name, uint32_t value)
{
    decimal(text, value);
    label(row, name, text, UI_INFO);
}
/* kind: 0 file name (any printable), 1 digits. */
static uint8_t edit(uint8_t row, uint8_t width, char *value, uint8_t capacity, uint8_t kind)
{
    uint8_t length = (uint8_t)strlen(value), key;
    for (;;) {
        ui_text_field(VALUE_COL, row, width, value, UI_HEADING);
        ui_text_field((uint8_t)(VALUE_COL + length), row, 1, "", UI_BAR);
        key = wait_key();
        if (cancelled(key)) return 0;
        if (is_return(key)) {
            ui_text_field(VALUE_COL, row, width, value, UI_HEADING);
            return length != 0;
        }
        if (key == PETSCII_HOME) { length = 0; value[0] = 0; }
        else if (key == PETSCII_DEL) { if (length) value[--length] = 0; }
        else if (length < capacity && key >= 32 && key <= 126 &&
                 (kind ? key >= '0' && key <= '9' :
                         key != '"' && key != ',' && key != ':' && key != '*' && key != '?' && key != '=')) {
            value[length++] = (char)key; value[length] = 0;
        }
    }
}
static uint8_t drive_number(void)
{
    uint8_t n = 0, i;
    for (i = 0; drive_text[i]; ++i) n = (uint8_t)(n * 10 + drive_text[i] - '0');
    return n;
}
static void drive_to_text(void)
{
    decimal(drive_text, disk_device);
}
/* Asks for a drive 8-30; returns 0 if cancelled. */
static uint8_t edit_drive(uint8_t row)
{
    drive_to_text();
    for (;;) {
        label(row, "Drive", drive_text, UI_HEADING);
        if (!edit(row, 3, drive_text, 2, 1)) { drive_to_text(); return 0; }
        if (drive_number() >= 8 && drive_number() <= 30) break;
        ui_text(LABEL_COL, (uint8_t)(row + 1), "Drives are numbered 8 to 30.", UI_ERROR);
    }
    disk_device = drive_number();
    return 1;
}

/* ---- Link: Telnet-escaped output, unescaped input ---- */
static void flush(void)
{
    uint8_t at = 0;
    int n;
    while (at < out_count && !link_failed) {
        n = net->write(out_queue + at, (uint16_t)(out_count - at));
        if (n < 0) link_failed = 1;
        else if (!n) net->poll();
        else at = (uint8_t)(at + n);
    }
    out_count = 0;
}
static void queue(const uint8_t *data, uint8_t length)
{
    if (out_count + length > sizeof(out_queue)) flush();
    memcpy(out_queue + out_count, data, length);
    out_count = (uint8_t)(out_count + length);
}
static void send_bytes(const uint8_t *data, uint16_t length)
{
    while (length--) telnet_send_byte(*data++);
}
static void send_byte(uint8_t value)
{
    telnet_send_byte(value);
    flush();
}
/* CAN CAN CAN is what senders look for; the backspaces clear them from
 * the screen of boards that echo them as text. */
static void send_abort(void)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) telnet_send_byte(XMODEM_CAN);
    for (i = 0; i < 5; ++i) telnet_send_byte(8);
    flush();
}
/* Reads unescaped bytes into input; returns the count, or -1 if the
 * connection ended. */
static int receive_some(void)
{
    int count, i, n = 0;
    uint8_t value;
    net->poll();
    if (link_failed || net->state() == NET_FAILED || net->state() == NET_EOF) return -1;
    count = net->read(input, sizeof(input));
    if (count < 0) return -1;
    for (i = 0; i < count; ++i)
        if (telnet_receive(input[i], &value)) input[n++] = value;
    return n;
}
static void arm(uint16_t ticks) { deadline = (uint16_t)(platform_ticks() + ticks); }
static uint8_t expired(void) { return (int16_t)(platform_ticks() - deadline) >= 0; }

/* ---- Progress ---- */
static void draw_progress(const char *heading, const char *protocol)
{
    ui_clear();
    ui_title("Transfer");
    ui_panel(2, 13, heading);
    label(4, "Protocol", protocol, UI_INFO);
    text[0] = 0;
    append(append(append(text, file_name), "  "), type_names[file_type]);
    label(6, "File", text, UI_HEADING);
    show_number(8, "Bytes", 0);
    show_number(10, "Retries", 0);
    ui_keys(cancel_keys);
}
static void status(const char *message, uint8_t attr)
{
    label(12, "Status", message, attr);
}
static void show_counts(void)
{
    show_number(8, "Bytes", bytes);
    show_number(10, "Retries", retries);
}
/* The final message; any key returns to the transfer menu. */
static void finish(const char *message, uint8_t attr)
{
    status(message, attr);
    if (attr == UI_ERROR && disk_message[0] && disk_message[0] != '0')
        label(13, "Drive", disk_message, UI_ERROR);
    ui_keys(any_key);
    wait_key();
}
static uint8_t user_cancel(void)
{
    uint8_t key;
    if (!platform_key_pending()) return 0;
    key = (uint8_t)getch();
    return cancelled(key);
}

/* ---- Download ---- */
static void store(void *context, uint16_t offset, uint8_t value)
{
    (void)context;
    session_buffer[offset] = value;
}
/* Writes a received block in pieces, polling between them so the
 * adapter keeps its connection serviced while a slow drive works. */
static uint8_t write_block(uint16_t length)
{
    uint16_t at;
    for (at = 0; at < length; at += 128) {
        if (disk_write((const char *)session_buffer + at, (uint8_t)(length - at < 128 ? length - at : 128)))
            return 0;
        net->poll();
    }
    return 1;
}
static void run_download(void)
{
    enum xmodem_event event;
    int n, i;
    uint8_t done = 0;
    xmodem_init(&receiver, store, NULL);
    draw_progress("Downloading", "XMODEM");
    status("Waiting for the sender...", UI_TEXT);
    bytes = retries = 0;
    send_byte(XMODEM_CRC_REQUEST);
    arm(START_TIMEOUT);
    while (!done) {
        n = receive_some();
        if (n < 0) { finish("The connection ended.", UI_ERROR); return; }
        event = XMODEM_EVENT_NONE;
        for (i = 0; i < n && !done; ++i) {
            event = xmodem_receive(&receiver, input[i]);
            switch (event) {
            case XMODEM_EVENT_BLOCK_READY:
                if (receiver.blocks_received == 0) {
                    label(4, "Protocol", receiver.checksum_mode ? "XMODEM checksum" :
                          xmodem_current_block_size(&receiver) == XMODEM_1K_BLOCK_SIZE ? "XMODEM-1K" : "XMODEM-CRC", UI_INFO);
                    status("Receiving...", UI_TEXT);
                }
                /* ACK only once the block is on disk: XMODEM waits. */
                if (!write_block(xmodem_current_block_size(&receiver))) {
                    send_abort();
                    finish("Disk write failed; the partial file is kept.", UI_ERROR);
                    return;
                }
                bytes += xmodem_current_block_size(&receiver);
                xmodem_accept_block(&receiver);
                send_byte(XMODEM_ACK);
                show_counts();
                break;
            case XMODEM_EVENT_SEND_ACK: send_byte(XMODEM_ACK); break;
            case XMODEM_EVENT_SEND_NAK:
                ++retries; show_counts();
                send_byte(XMODEM_NAK);
                break;
            case XMODEM_EVENT_SEND_C: send_byte(XMODEM_CRC_REQUEST); break;
            case XMODEM_EVENT_COMPLETE:
                send_byte(XMODEM_ACK);
                done = 1;
                break;
            case XMODEM_EVENT_CANCELLED:
                finish("Cancelled by the BBS; the partial file is kept.", UI_ERROR);
                return;
            case XMODEM_EVENT_ERROR:
                send_abort();
                finish("Too many errors; the partial file is kept.", UI_ERROR);
                return;
            default: break;
            }
        }
        if (done) break;
        if (n) arm(receiver.blocks_received ? BLOCK_TIMEOUT : START_TIMEOUT);
        else if (expired()) {
            event = xmodem_timeout(&receiver);
            if (event == XMODEM_EVENT_ERROR) {
                send_abort();
                finish(receiver.blocks_received ? "The sender stopped; the partial file is kept." :
                                                  "The sender did not start.", UI_ERROR);
                return;
            }
            ++retries; show_counts();
            send_byte(event == XMODEM_EVENT_SEND_C ? XMODEM_CRC_REQUEST : XMODEM_NAK);
            arm(receiver.blocks_received ? BLOCK_TIMEOUT : START_TIMEOUT);
        }
        if (user_cancel()) {
            send_abort();
            finish("Cancelled; the partial file is kept.", UI_ERROR);
            return;
        }
    }
    show_counts();
    finish("Complete. XMODEM pads the last block to its full size.", UI_GOOD);
}
static void open_download(void)
{
    uint8_t code, key;
    char mode[5];
    append(append(mode, type_modes[file_type]), ",w");
    code = disk_open(file_name, mode, 2);
    if (code == 63) {
        ui_text_field(LABEL_COL, 12, 70, "A file with this name exists. Replace it?", UI_ERROR);
        ui_keys(confirm_keys);
        do key = wait_key(); while (key != 'y' && key != 'Y' && key != 'n' && key != 'N' && !cancelled(key));
        if (key != 'y' && key != 'Y') return;
        append(append(text, "s:"), file_name);
        disk_command(text);
        code = disk_open(file_name, mode, 2);
    }
    if (code) {
        draw_progress("Downloading", "XMODEM");
        finish("The file could not be created.", UI_ERROR);
        return;
    }
    file_open = 1;
    run_download();
    file_open = 0;
    code = disk_close();
    (void)code;
}
static void download(void)
{
    uint8_t key;
    ui_clear();
    ui_title("Transfer");
    ui_panel(2, 13, "Download");
    label(4, "Protocol", "XMODEM: CRC, checksum and 1K blocks are detected", UI_INFO);
    label(6, "File name", "", UI_HEADING);
    label(8, "Type", "", UI_HEADING);
    drive_to_text();
    label(10, "Drive", drive_text, UI_HEADING);
    ui_text(LABEL_COL, 16, "Start the XMODEM download on the BBS, then name the file here.", UI_MUTED);
    ui_text(LABEL_COL, 17, "XMODEM sends no file name or exact size.", UI_MUTED);
    ui_keys(edit_keys);
    file_name[0] = 0;
    if (!edit(6, 17, file_name, 16, 0)) return;
    ui_text(VALUE_COL, 8, "1", UI_ACTION); ui_text(18, 8, "PRG", UI_TEXT);
    ui_text(23, 8, "2", UI_ACTION); ui_text(25, 8, "SEQ", UI_TEXT);
    ui_text(30, 8, "3", UI_ACTION); ui_text(32, 8, "USR", UI_TEXT);
    ui_keys(type_keys);
    do { key = wait_key(); if (cancelled(key)) return; } while (key < '1' || key > '3');
    file_type = (uint8_t)(key - '0');
    label(8, "Type", type_names[file_type], UI_HEADING);
    ui_keys(edit_keys);
    if (!edit_drive(10)) return;
    open_download();
}

/* ---- Upload ---- */
static uint8_t petscii_to_ascii(uint8_t c)
{
    if (c >= 0x41 && c <= 0x5a) return (uint8_t)(c + 32);
    if (c >= 0xc1 && c <= 0xda) return (uint8_t)(c - 128);
    return c >= 32 && c < 0x41 ? c : '?';
}
/* One listing line (after its link and block count): "name"  prg */
static void parse_line(uint16_t blocks)
{
    uint8_t i = 0, n = 0, c;
    while (line_buffer[i] && line_buffer[i] != '"') ++i;
    if (!line_buffer[i]) return;
    ++i;
    while (line_buffer[i] && line_buffer[i] != '"' && n < 16)
        dir.name[n++] = (char)petscii_to_ascii((uint8_t)line_buffer[i++]);
    dir.name[n] = 0;
    if (line_buffer[i] != '"' || !n) return;
    ++i;
    while (line_buffer[i] == ' ' || line_buffer[i] == '*') ++i;
    c = (uint8_t)line_buffer[i];
    /* PETSCII "prg", "seq", "usr" ($50 $52 $47 ...). */
    dir.type = c == 0x50 ? TYPE_PRG : c == 0x53 ? TYPE_SEQ : c == 0x55 ? TYPE_USR : 0;
    if (!dir.type || dir_count >= DIR_MAX) return;
    dir.blocks = blocks;
    bank1_write(BANK1_DIRECTORY + (uint16_t)dir_count * DIR_STRIDE, &dir, sizeof(dir));
    ++dir_count;
}
/* Reads "$" as a BASIC listing: load address, then lines of link, block
 * count and text, each ending in 0. The first line is the disk name. */
static uint8_t read_directory(void)
{
    uint8_t code, n, i, phase = 0, length = 0, lines = 0;
    uint16_t blocks = 0;
    char c;
    dir_count = 0;
    code = disk_open("$", "", 0);
    if (code) return code;
    while ((n = disk_read((char *)input, sizeof(input))) != 0) {
        for (i = 0; i < n; ++i) {
            c = (char)input[i];
            if (phase < 2) { ++phase; continue; }             /* load address */
            if (phase < 4) { ++phase; continue; }             /* line link */
            if (phase == 4) { blocks = (uint8_t)c; ++phase; continue; }
            if (phase == 5) { blocks |= (uint16_t)(uint8_t)c << 8; ++phase; length = 0; continue; }
            if (c) {
                if (length < sizeof(line_buffer) - 1) line_buffer[length++] = c;
                continue;
            }
            line_buffer[length] = 0;
            if (lines++) parse_line(blocks);
            phase = 2;
        }
    }
    return disk_close();
}
static void get_entry(uint8_t index)
{
    bank1_read(&dir, BANK1_DIRECTORY + (uint16_t)index * DIR_STRIDE, sizeof(dir));
}
static void draw_entry(uint8_t row)
{
    uint8_t index = (uint8_t)(dir_top + row), y = (uint8_t)(LIST_ROW + row), bar;
    if (index >= dir_count) { ui_text_field(4, y, 72, "", UI_TEXT); return; }
    get_entry(index);
    bar = index == dir_selected;
    append(decimal(text, dir.blocks), " blocks");
    ui_text_field(4, y, 1, "", bar ? UI_BAR : UI_TEXT);
    ui_text_field(5, y, 20, dir.name, bar ? UI_BAR : UI_HEADING);
    ui_text_field(25, y, 8, type_names[dir.type], bar ? UI_BAR : UI_MUTED);
    ui_text_field(33, y, 43, text, bar ? UI_BAR : UI_INFO);
}
static void draw_directory(void)
{
    uint8_t row;
    decimal(append(text, "Upload from drive "), disk_device);
    ui_clear();
    ui_title("Transfer");
    ui_panel(2, 19, text);
    if (!dir_count) ui_text(LABEL_COL, LIST_ROW, "No PRG, SEQ or USR files on this disk.", UI_MUTED);
    for (row = 0; row < LIST_ROWS; ++row) draw_entry(row);
    ui_text(LABEL_COL, 22, "CRSR up/down selects a file. Start the upload on the BBS first.", UI_MUTED);
    ui_keys(list_keys);
}
static void move_to(uint8_t index)
{
    uint8_t old = dir_selected, old_top = dir_top;
    dir_selected = index;
    if (dir_selected < dir_top) dir_top = dir_selected;
    else if (dir_selected >= dir_top + LIST_ROWS) dir_top = (uint8_t)(dir_selected - LIST_ROWS + 1);
    if (dir_top != old_top) { draw_directory(); return; }
    draw_entry((uint8_t)(old - dir_top));
    draw_entry((uint8_t)(dir_selected - dir_top));
}
/* Fills the block buffer from the open file; returns the byte count. */
static uint16_t read_block(uint16_t capacity)
{
    uint16_t got = 0;
    uint8_t n;
    while (got < capacity) {
        n = disk_read((char *)session_buffer + got, (uint8_t)(capacity - got > 255 ? 255 : capacity - got));
        if (!n) break;
        got += n;
        net->poll();
    }
    return got;
}
static void send_packet(void)
{
    uint8_t header[3], check[2], n;
    if (xmodem_send_at_end(&sender)) { send_byte(XMODEM_EOT); return; }
    xmodem_send_header(&sender, header);
    n = xmodem_send_check(&sender, session_buffer, check);
    send_bytes(header, 3);
    send_bytes(session_buffer, sender.block_size);
    send_bytes(check, n);
    flush();
}
static void run_upload(uint8_t one_k)
{
    enum xmodem_send_event action;
    int n, i;
    xmodem_send_init(&sender, one_k);
    draw_progress("Uploading", one_k ? "XMODEM-1K" : "XMODEM");
    status("Waiting for the receiver...", UI_TEXT);
    retries = 0;
    arm(UPLOAD_START_TIMEOUT);
    for (;;) {
        n = receive_some();
        if (n < 0) { finish("The connection ended.", UI_ERROR); return; }
        for (i = 0; i < n; ++i) {
            action = xmodem_send_receive(&sender, input[i]);
            if (action == XMODEM_SEND_NONE) continue;
            if (action == XMODEM_SEND_NEXT) {
                if (sender.block_number == 1 && !sender.sent) status("Sending...", UI_TEXT);
                xmodem_send_load(&sender, session_buffer, read_block(xmodem_send_capacity(&sender)));
            } else if (action == XMODEM_SEND_AGAIN) {
                ++retries;
            } else if (action == XMODEM_SEND_COMPLETE) {
                bytes = sender.sent; show_counts();
                finish("Complete.", UI_GOOD);
                return;
            } else if (action == XMODEM_SEND_CANCELLED) {
                finish("Cancelled by the BBS.", UI_ERROR);
                return;
            } else {
                send_abort();
                finish("Too many errors.", UI_ERROR);
                return;
            }
            bytes = sender.sent; show_counts();
            send_packet();
            arm(BLOCK_TIMEOUT);
        }
        if (!n && expired()) {
            if (!xmodem_send_started(&sender)) { finish("The receiver did not start.", UI_ERROR); return; }
            action = xmodem_send_timeout(&sender);
            if (action == XMODEM_SEND_ERROR) { send_abort(); finish("The receiver stopped answering.", UI_ERROR); return; }
            ++retries; show_counts();
            send_packet();
            arm(BLOCK_TIMEOUT);
        }
        if (user_cancel()) { send_abort(); finish("Cancelled.", UI_ERROR); return; }
    }
}
static void upload(void)
{
    uint8_t key, code;
    char mode[5];
    for (;;) {
        code = read_directory();
        dir_selected = dir_top = 0;
        draw_directory();
        if (code) label(20, "Drive", disk_message, UI_ERROR);
        for (;;) {
            key = wait_key();
            if (cancelled(key)) return;
            if (key == PETSCII_CURSOR_UP && dir_selected) move_to((uint8_t)(dir_selected - 1));
            else if (key == PETSCII_CURSOR_DOWN && dir_selected + 1 < dir_count) move_to((uint8_t)(dir_selected + 1));
            else if (key == PETSCII_F1) {
                ui_keys(edit_keys);
                edit_drive(20);
                break;  /* read the (new) drive's directory */
            } else if (is_return(key) && dir_count) {
                get_entry(dir_selected);
                strcpy(file_name, dir.name);
                file_type = dir.type;
                ui_text_field(LABEL_COL, 20, 70, "Protocol: choose what the BBS is waiting for.", UI_TEXT);
                ui_keys(upload_keys);
                do { key = wait_key(); if (cancelled(key)) return; } while (key != '1' && key != '2');
                append(append(mode, type_modes[file_type]), ",r");
                code = disk_open(file_name, mode, 2);
                bytes = 0;
                if (code) {
                    draw_progress("Uploading", "XMODEM");
                    finish("The file could not be opened.", UI_ERROR);
                    return;
                }
                run_upload(key == '2');
                disk_close();
                return;
            }
        }
    }
}

/* ---- Menu ---- */
static void draw_menu(const struct connect_request *request)
{
    char *out;
    ui_clear();
    ui_title("Transfer");
    ui_panel(2, 9, "File transfer");
    out = append(text, request->name[0] ? request->name : request->hostname);
    *out = 0;
    label(4, "Connected", text, UI_INFO);
    ui_text(LABEL_COL, 6, "F1", UI_ACTION); ui_text(10, 6, "Download a file from the BBS", UI_TEXT);
    ui_text(LABEL_COL, 8, "F3", UI_ACTION); ui_text(10, 8, "Upload a file to the BBS", UI_TEXT);
    ui_text(LABEL_COL, 12, "Start the transfer on the BBS first, then choose it here.", UI_MUTED);
    ui_text(LABEL_COL, 13, "The connection stays open; F7 returns to the same screen.", UI_MUTED);
    ui_keys(menu_keys);
}
XFER_ENTRY void transfer_run(const struct net_backend *backend, const struct connect_request *request)
{
    uint8_t key, saved_device = disk_device;
    net = backend;
    out_count = link_failed = file_open = 0;
    telnet_set_sender(queue);
    telnet_set_transfer(1);
    /* Ask for 8-bit clean data both ways; transfer mode copes either way. */
    telnet_request_binary();
    flush();
    for (;;) {
        draw_menu(request);
        key = wait_key();
        if (key == PETSCII_F1) download();
        else if (key == PETSCII_F3) upload();
        else if (cancelled(key)) break;
        if (link_failed) break;
    }
    telnet_set_transfer(0);
    disk_device = saved_device;
}

#ifdef __OSCAR64C__
#pragma optimize(pop)
#endif
#if defined(__OSCAR64C__) && !defined(XFER_TEST)
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
