#ifndef T80_BOOK_DISK_H
#define T80_BOOK_DISK_H
#include <stdint.h>

/* Sequential-file access for the phonebook overlay, on the boot device.
 * Names are ASCII ("phonebook"); the C128 implementation converts them to
 * PETSCII. Results are Commodore DOS status codes: 0 is OK, 62 is
 * FILE NOT FOUND, other values are drive errors. Tests replace these. */
uint8_t book_disk_open(const char *name, uint8_t write);
/* Returns the number of bytes read; 0 at the end of the file. */
uint8_t book_disk_read(char *buffer, uint8_t capacity);
uint8_t book_disk_write(const char *data, uint8_t length);
/* Closes the open file; returns the drive status after closing. */
uint8_t book_disk_close(void);
/* Sends a DOS command such as "s:recent.new" and returns its status. */
uint8_t book_disk_command(const char *command);
/* The drive's last status message, e.g. "72,DISK FULL,00,00". */
extern char book_disk_message[40];
#endif
