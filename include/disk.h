#ifndef T80_DISK_H
#define T80_DISK_H
#include <stdint.h>

/* Resident file access on one drive, through the KERNAL. One file is open
 * at a time, on logical file 2, with the command channel (15) beside it.
 * Names and modes are ASCII ("phonebook", ",s,w"); they are converted to
 * PETSCII (a-z to $41-$5A, A-Z to $C1-$DA). Results are Commodore DOS status codes: 0 OK, 62 FILE NOT
 * FOUND, 63 FILE EXISTS, others are drive errors; disk_message holds the
 * drive's text, e.g. "72,DISK FULL,00,00". Each call runs at 1 MHz and
 * restores the caller's speed, so a file can stay open while the network
 * is polled at 2 MHz. Tests replace these functions. */
extern uint8_t disk_device;       /* defaults to the boot device */
extern char disk_message[40];
/* secondary: 2 for data files; 0 opens "$" as a BASIC-format listing. */
uint8_t disk_open(const char *name, const char *mode, uint8_t secondary);
/* Returns the number of bytes read; 0 at the end of the file. */
uint8_t disk_read(char *buffer, uint8_t capacity);
uint8_t disk_write(const char *data, uint8_t length);
/* Closes the open file; returns the drive status after closing. */
uint8_t disk_close(void);
/* Sends a DOS command such as "s:recent.new" and returns its status. */
uint8_t disk_command(const char *command);
#endif
