#include <c64/kernalio.h>
#include <string.h>
#include "disk.h"
#include "platform.h"

uint8_t disk_device = 8;
char disk_message[40];
/* DOS commands such as "r:phonebook=phonebook.new" need the room. */
static char disk_name[41];

/* ASCII to PETSCII as a C128 lists it in lower/upper case: lowercase
 * letters become $41-$5A (also what DOS reads in ",s,w" or "s:"), capitals
 * $C1-$DA, so "Game" keeps its case on the disk. */
static char petscii(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return (char)(c + 128);
    return c;
}
static void petscii_name(const char *name, const char *suffix)
{
    uint8_t n = 0;
    while (*name && n < sizeof(disk_name) - 1) disk_name[n++] = petscii(*name++);
    while (*suffix && n < sizeof(disk_name) - 1) disk_name[n++] = petscii(*suffix++);
    disk_name[n] = 0;
}
static uint8_t not_ready(void)
{
    strcpy(disk_message, "74,DRIVE NOT READY");
    return 74;
}
static uint8_t read_status(void)
{
    int n;
    krnio_pstatus[15] = KRNIO_OK;
    n = krnio_read(15, disk_message, sizeof(disk_message) - 1);
    if (n < 2) return not_ready();
    while (n && (disk_message[n - 1] == 13 || disk_message[n - 1] == 10)) --n;
    disk_message[n] = 0;
    return (uint8_t)((disk_message[0] - '0') * 10 + disk_message[1] - '0');
}
static uint8_t open_command(const char *command)
{
    krnio_setbnk(0, 0);
    petscii_name(command, "");
    krnio_setnam(disk_name);
    return krnio_open(15, disk_device, 15) ? 0 : not_ready();
}
uint8_t disk_command(const char *command)
{
    uint8_t speed = platform_slow(), code = open_command(command);
    if (!code) {
        code = read_status();
        krnio_close(15);
    }
    platform_restore_speed(speed);
    return code;
}
uint8_t disk_open(const char *name, const char *mode, uint8_t secondary)
{
    uint8_t speed = platform_slow(), code = open_command("");
    if (!code) {
        petscii_name(name, mode);
        krnio_setnam(disk_name);
        if (!krnio_open(2, disk_device, secondary)) {
            /* The KERNAL refused the file (its table, not the drive). */
            krnio_close(15);
            platform_restore_speed(speed);
            strcpy(disk_message, "70,NO CHANNEL (KERNAL OPEN)");
            return 70;
        }
        code = read_status();
        if (code) {
            /* Close the file before the command channel, which closes all. */
            krnio_close(2);
            krnio_close(15);
        }
    }
    platform_restore_speed(speed);
    return code;
}
uint8_t disk_read(char *buffer, uint8_t capacity)
{
    uint8_t speed = platform_slow();
    int n = krnio_read(2, buffer, capacity);
    platform_restore_speed(speed);
    return n > 0 ? (uint8_t)n : 0;
}
uint8_t disk_write(const char *data, uint8_t length)
{
    uint8_t speed = platform_slow();
    int n = krnio_write(2, data, length);
    platform_restore_speed(speed);
    return n == length ? 0 : 1;
}
uint8_t disk_close(void)
{
    uint8_t speed = platform_slow(), code;
    krnio_close(2);
    code = read_status();
    krnio_close(15);
    platform_restore_speed(speed);
    return code;
}
