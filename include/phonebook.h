#ifndef T80_PHONEBOOK_H
#define T80_PHONEBOOK_H
#include <stdint.h>
#include "telnet.h"

/* Phonebook and recent-connection files: plain ASCII text, shareable on a
 * BBS and editable on any computer. CR, LF or CRLF line endings.
 *
 *   80TPB 1
 *   # comment
 *   LukasSoft BBS|bbs.lukassoft.com|48822|ansi80
 *
 * Fields: name|host|port|mode. Extra fields after mode are ignored, so a
 * later format version can add some. Mode names follow the 80Terminal
 * Flutter app: petscii80, ansi80, plus vt100. Modes this client cannot run
 * map to the nearest one (petscii40, amiga80, ascii80) or are skipped. */
#define PHONEBOOK_MAX 50
#define RECENT_MAX 9   /* dialled with keys 1-9 on the main screen */
#define PHONEBOOK_NAME_SIZE 24
#define PHONEBOOK_HOST_SIZE 64
/* Longest line written: name, host, port, mode, separators and CRLF. */
#define PHONEBOOK_LINE_SIZE 112
#define PHONEBOOK_HEADER "80TPB 1"

struct phonebook_entry {
    char name[PHONEBOOK_NAME_SIZE + 1];
    char hostname[PHONEBOOK_HOST_SIZE + 1];
    uint16_t port;
    enum telnet_profile profile;
};

enum phonebook_line {
    PHONEBOOK_LINE_SKIP = 0,     /* blank, comment or header */
    PHONEBOOK_LINE_ENTRY,        /* entry filled in */
    PHONEBOOK_LINE_UNSUPPORTED,  /* well formed, mode not available here */
    PHONEBOOK_LINE_INVALID
};
/* line: one line without its ending, NUL terminated. */
uint8_t phonebook_parse_line(const char *line, struct phonebook_entry *entry);
/* Writes the entry and CRLF to out (PHONEBOOK_LINE_SIZE bytes); returns the
 * length, or 0 if the entry cannot be written. */
uint8_t phonebook_format_line(char *out, const struct phonebook_entry *entry);
const char *phonebook_mode_name(enum telnet_profile profile);
#endif
