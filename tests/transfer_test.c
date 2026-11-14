/* The uixfer overlay against lrzsz (lsz/lrz), the XMODEM/ZMODEM tools BBS
 * doors use. A harness plays the Telnet server between them: it escapes
 * IAC and, in one variant, refuses binary mode, so CR NUL is used. Disk,
 * screen, keyboard and bank 1 are host stand-ins. Host only (POSIX). */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "stubs/conio.h"
#include "transfer.h"
#include "disk.h"
#include "telnet.h"
#include "bank1.h"
#include "session.h"

uint8_t session_buffer[1024];
static uint8_t ram1[0x10000], vdc[0x1000];
static char dir_path[256];

/* ---- Screen, bank 1 ---- */
void screen_write_run(uint16_t at, const uint8_t *data, uint16_t length)
{ assert(at + length <= sizeof(vdc)); memcpy(vdc + at, data, length); }
void screen_fill_run(uint16_t at, uint8_t value, uint16_t length)
{ assert(at + length <= sizeof(vdc)); memset(vdc + at, value, length); }
void bank1_read(void *destination, uint16_t source, uint16_t length)
{ memcpy(destination, ram1 + source, length); }
void bank1_write(uint16_t destination, const void *source, uint16_t length)
{ memcpy(ram1 + destination, source, length); }
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

/* ---- Clock and keyboard: keys wait while a transfer runs ---- */
uint16_t platform_ticks(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint16_t)(t.tv_sec * 60 + t.tv_nsec / 16666667);
}
static const char *script;
static int script_at;
static int transferring(void)
{
    return (on_screen(2, "Downloading") || on_screen(2, "Uploading")) && !on_screen(24, "Any key");
}
uint8_t platform_key_pending(void) { return script[script_at] && !transferring(); }  /* script ends with NUL */
static int check_at = -1;
static void (*check_screen)(void);
char getch(void)
{
    char c;
    assert(script[script_at]);
    if (script_at == check_at) check_screen();
    c = script[script_at++];
    return c == 13 ? 10 : c;  /* like Oscar64's conio */
}
void clrscr(void) {}
void gotoxy(char x, char y) { (void)x; (void)y; }
void putch(char c) { (void)c; }

/* ---- Disk: files in a host directory, and a synthesized "$" listing ---- */
uint8_t disk_device = 8;
char disk_message[40];
static FILE *open_file;
static const uint8_t *listing;
static size_t listing_size, listing_at;
static uint8_t status(uint8_t code, const char *text) { strcpy(disk_message, text); return code; }
static void host_path(char *out, const char *name) { snprintf(out, 300, "%s/%s", dir_path, name); }
uint8_t disk_open(const char *name, const char *mode, uint8_t secondary)
{
    char path[300];
    assert(!open_file && !listing);
    if (!strcmp(name, "$")) {
        /* load address, then: link, blocks, text, 0 ... ; up.bin is 12 blocks */
        static const uint8_t dir[] = {
            0x01, 0x08,
            0x01, 0x01, 0, 0, 0x12, '"', 0x54, 0x45, 0x53, 0x54, '"', ' ', '0', '0', ' ', '2', 'a', 0,
            0x01, 0x01, 12, 0, ' ', ' ', '"', 0x55, 0x50, '.', 0x42, 0x49, 0x4e, '"', ' ', ' ', 0x50, 0x52, 0x47, 0,
            0x01, 0x01, 3, 0, ' ', ' ', '"', 0x44, 0x41, 0x54, 0x41, '"', ' ', ' ', 0x53, 0x45, 0x51, ' ', 0x3c, 0,
            0x01, 0x01, 0x10, 0x02, 0x42, 0x4c, 0x4f, 0x43, 0x4b, 0x53, ' ', 0x46, 0x52, 0x45, 0x45, '.', 0,
            0, 0 };
        assert(secondary == 0);
        listing = dir; listing_size = sizeof(dir); listing_at = 0;
        return status(0, "00, OK,00,00");
    }
    assert(secondary == 2 && strlen(mode) == 4);
    host_path(path, name);
    if (mode[3] == 'w') {
        if (access(path, F_OK) == 0) return status(63, "63,FILE EXISTS,00,00");
        open_file = fopen(path, "wb");
    } else {
        open_file = fopen(path, "rb");
        if (!open_file) return status(62, "62,FILE NOT FOUND,00,00");
    }
    assert(open_file);
    return status(0, "00, OK,00,00");
}
uint8_t disk_read(char *buffer, uint8_t capacity)
{
    size_t n;
    if (listing) {
        n = listing_size - listing_at < capacity ? listing_size - listing_at : capacity;
        memcpy(buffer, listing + listing_at, n);
        listing_at += n;
        return (uint8_t)n;
    }
    return (uint8_t)fread(buffer, 1, capacity, open_file);
}
uint8_t disk_write(const char *data, uint8_t length)
{
    return fwrite(data, 1, length, open_file) == length ? 0 : 1;
}
uint8_t disk_close(void)
{
    if (listing) listing = NULL;
    else { fclose(open_file); open_file = NULL; }
    return status(0, "00, OK,00,00");
}
uint8_t disk_command(const char *command)
{
    char path[300];
    assert(command[0] == 's' && command[1] == ':');
    host_path(path, command + 2);
    unlink(path);
    return status(1, "01, FILES SCRATCHED,01,00");
}

/* ---- The "BBS": lsz or lrz behind a Telnet server ---- */
static pid_t child;
static int to_child = -1, from_child = -1, binary_agreed;
static uint8_t net_in[4096];
static size_t net_in_count;
static int iac_state, cr_pending, eof;
static void serve(void)
{
    /* Child output -> Telnet: IAC doubled; CR NUL when not binary. */
    uint8_t buffer[512];
    ssize_t n, i;
    while (net_in_count < sizeof(net_in) - 1024 && (n = read(from_child, buffer, sizeof(buffer))) > 0) {
        for (i = 0; i < n; ++i) {
            net_in[net_in_count++] = buffer[i];
            if (buffer[i] == 0xff) net_in[net_in_count++] = 0xff;
            else if (buffer[i] == 13 && !binary_agreed) net_in[net_in_count++] = 0;
        }
    }
    if (n == 0) eof = 1;
}
static void server_receive(const uint8_t *data, uint16_t length)
{
    /* Telnet -> child: IAC IAC is one 0xFF, option commands are answered
     * (or refused), CR NUL is CR when not binary. */
    uint8_t out[2200];
    size_t n = 0;
    uint16_t i;
    for (i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (iac_state == 1) {
            if (c == 0xff) { out[n++] = 0xff; iac_state = 0; }
            else iac_state = c;  /* DO/DONT/WILL/WONT: option next */
            continue;
        }
        if (iac_state) {
            /* Option: binary is agreed only in the binary variant. */
            if (c == 0 && binary_agreed) {
                static const uint8_t will[] = {0xff, 251, 0}, do_[] = {0xff, 253, 0};
                const uint8_t *reply = iac_state == 253 ? will : do_;
                memcpy(net_in + net_in_count, reply, 3); net_in_count += 3;
            }
            iac_state = 0;
            continue;
        }
        if (c == 0xff) { iac_state = 1; continue; }
        if (cr_pending) { cr_pending = 0; if (c == 0) continue; }
        if (c == 13 && !binary_agreed) cr_pending = 1;
        out[n++] = c;
    }
    if (n) assert(write(to_child, out, n) == (ssize_t)n);
}
static void net_poll(void) { serve(); }
static int net_read(uint8_t *buffer, uint16_t capacity)
{
    size_t n;
    serve();
    n = net_in_count < capacity ? net_in_count : capacity;
    memcpy(buffer, net_in, n);
    memmove(net_in, net_in + n, net_in_count - n);
    net_in_count -= n;
    return (int)n;
}
static int net_write(const uint8_t *buffer, uint16_t length) { server_receive(buffer, length); return length; }
static int net_state(void) { return NET_CONNECTED; }
static const struct net_backend backend = {
    "Test", "", NULL, NULL, net_poll, net_read, net_write, net_state, NULL };

static void start_child(char *const argv[])
{
    int in[2], out[2];
    assert(pipe(in) == 0 && pipe(out) == 0);
    child = fork();
    assert(child >= 0);
    if (!child) {
        int null = open("/dev/null", O_WRONLY);
        dup2(in[0], 0); dup2(out[1], 1); dup2(null, 2);
        /* lrz refuses absolute paths ("Security Violation"). */
        if (chdir(dir_path)) _exit(126);
        close(in[1]); close(out[0]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(in[0]); close(out[1]);
    to_child = in[1]; from_child = out[0];
    fcntl(from_child, F_SETFL, O_NONBLOCK);
    net_in_count = 0; iac_state = cr_pending = eof = 0;
}
static int finish_child(void)
{
    int status_code;
    close(to_child);
    waitpid(child, &status_code, 0);
    close(from_child);
    return WIFEXITED(status_code) ? WEXITSTATUS(status_code) : -1;
}

/* ---- Tests ---- */
static uint8_t data[5000];
static void make_data(size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) data[i] = (uint8_t)(i * 31 + (i >> 7));
    /* The awkward bytes: IAC, CR NUL, CR LF, SUB. */
    data[10] = 0xff; data[11] = 0xff; data[20] = 13; data[21] = 0; data[30] = 13; data[31] = 10;
    data[size - 1] = 0x1a;
}
static void write_file(const char *name, size_t size)
{
    char path[300];
    FILE *f;
    host_path(path, name);
    f = fopen(path, "wb");
    assert(f && fwrite(data, 1, size, f) == size);
    fclose(f);
}
static size_t read_file(const char *name, uint8_t *out, size_t capacity)
{
    char path[300];
    FILE *f;
    size_t n;
    host_path(path, name);
    f = fopen(path, "rb");
    assert(f);
    n = fread(out, 1, capacity, f);
    fclose(f);
    return n;
}
static struct connect_request request = { "Test board", "bbs.example", 23, TELNET_PROFILE_ANSI_80 };
static void telnet_ignore(const uint8_t *d, uint8_t n) { (void)d; (void)n; }

static void download(int one_k, int binary, size_t size)
{
    static uint8_t got[6000];
    char path[300];
    char *argv[6];
    size_t n, i;
    int a = 0;
    make_data(size);
    write_file("source.bin", size);
    host_path(path, "source.bin");
    argv[a++] = "lsz"; argv[a++] = "-X"; argv[a++] = "-q";
    if (one_k) argv[a++] = "-k";
    argv[a++] = path; argv[a] = NULL;
    binary_agreed = binary;
    telnet_init(TELNET_PROFILE_ANSI_80, telnet_ignore);
    start_child(argv);
    host_path(path, "dl.bin"); unlink(path);
    /* F1 download, name, PRG, drive 8, then the result and F7. */
    script = "\x85" "dl.bin\r" "1" "\r" " " "\x88"; script_at = 0;
    transfer_run(&backend, &request);
    assert(!script[script_at]);
    assert(finish_child() == 0);
    n = read_file("dl.bin", got, sizeof(got));
    /* XMODEM delivers whole blocks: the file, then SUB padding. */
    assert(n >= size && n - size < (one_k ? 1024u : 128u) && !memcmp(got, data, size));
    for (i = size; i < n; ++i) assert(got[i] == 0x1a);
    printf("PASS: download from lsz (%s%s), %zu bytes\n", one_k ? "1K, " : "", binary ? "binary" : "CR NUL", size);
}
static void upload(int one_k, int binary, size_t size)
{
    static uint8_t got[6000];
    char path[300];
    char *argv[6];
    size_t n;
    make_data(size);
    write_file("up.bin", size);
    host_path(path, "received.bin"); unlink(path);
    argv[0] = "lrz"; argv[1] = "-X"; argv[2] = "-q"; argv[3] = "-c"; argv[4] = "received.bin"; argv[5] = NULL;
    binary_agreed = binary;
    telnet_init(TELNET_PROFILE_ANSI_80, telnet_ignore);
    start_child(argv);
    /* F3 upload, the first file (up.bin), the protocol, the result, F7. */
    script = one_k ? "\x86" "\r" "2" " " "\x88" : "\x86" "\r" "1" " " "\x88"; script_at = 0;
    transfer_run(&backend, &request);
    assert(!script[script_at]);
    assert(finish_child() == 0);
    n = read_file("received.bin", got, sizeof(got));
    assert(n >= size && !memcmp(got, data, size));
    printf("PASS: upload to lrz (%s%s), %zu bytes\n", one_k ? "1K, " : "", binary ? "binary" : "CR NUL", size);
}
static void overwrite_prompt(void)
{
    char path[300];
    char *argv[] = { "lsz", "-X", "-q", path, NULL };
    make_data(300);
    write_file("source.bin", 300);
    host_path(path, "source.bin");
    write_file("dl.bin", 10);
    binary_agreed = 1;
    telnet_init(TELNET_PROFILE_ANSI_80, telnet_ignore);
    /* Exists: N keeps it and returns to the menu without a transfer. */
    script = "\x85" "dl.bin\r" "1" "\r" "n" "\x88"; script_at = 0;
    transfer_run(&backend, &request);
    assert(!script[script_at]);
    {
        uint8_t got[20];
        assert(read_file("dl.bin", got, sizeof(got)) == 10);
    }
    /* Y replaces it. */
    start_child(argv);
    script = "\x85" "dl.bin\r" "1" "\r" "y" " " "\x88"; script_at = 0;
    transfer_run(&backend, &request);
    assert(finish_child() == 0);
    {
        static uint8_t got[1000];
        assert(read_file("dl.bin", got, sizeof(got)) == 384 && !memcmp(got, data, 300));
    }
    puts("PASS: an existing file is replaced only after Y");
}
static void listed(void)
{
    assert(on_screen(2, "Upload from drive 8"));
    assert(on_screen(4, "up.bin") && on_screen(4, "PRG") && on_screen(4, "12 blocks"));
    assert(on_screen(5, "data") && on_screen(5, "SEQ") && on_screen(5, "3 blocks"));
    assert(!on_screen(6, "blocks") && !on_screen(4, "TEST"));
    /* The selection bar (reverse attribute) is on the second file. */
    assert((vdc[0x800 + 5 * 80 + 5] & 0x40) && !(vdc[0x800 + 4 * 80 + 5] & 0x40));
}
static void directory(void)
{
    check_at = 3; check_screen = listed;
    /* Two files (PRG, and SEQ locked with "<"); not the disk name or the
     * "blocks free" line. CRSR down moves the bar; F7 cancels. */
    script = "\x86" "\x11" "\x11" "\x88" "\x88"; script_at = 0;
    telnet_init(TELNET_PROFILE_ANSI_80, telnet_ignore);
    transfer_run(&backend, &request);
    assert(!script[script_at]);
    check_at = -1;
    puts("PASS: directory listing and selection");
}
/* On a hang, show where the overlay stopped. */
static void timed_out(int sig)
{
    uint8_t row;
    (void)sig;
    fprintf(stderr, "TIMEOUT: key %d of \"%s\", screen:\n", script_at, script);
    for (row = 0; row < 25; ++row) fprintf(stderr, "%s\n", row_text(row));
    _exit(2);
}
int main(void)
{
    char template[] = "/tmp/t80xferXXXXXX";
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, timed_out);
    alarm(60);
    assert(mkdtemp(template));
    strcpy(dir_path, template);
    download(0, 1, 3000);
    download(1, 1, 3000);
    download(0, 0, 1000);
    upload(0, 1, 3000);
    upload(1, 1, 3000);
    upload(1, 0, 2100);
    upload(0, 1, 0);
    overwrite_prompt();
    directory();
    puts("XMODEM transfers against lrzsz passed");
    return 0;
}
