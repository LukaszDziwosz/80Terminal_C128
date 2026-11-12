#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "phonebook.h"

static struct phonebook_entry entry;

static uint8_t parse(const char *line) { memset(&entry, 0x55, sizeof(entry)); return phonebook_parse_line(line, &entry); }

int main(void)
{
    char out[PHONEBOOK_LINE_SIZE];
    uint8_t n;
    struct phonebook_entry longest;
    /* Header, comments and blank lines. */
    assert(parse(PHONEBOOK_HEADER) == PHONEBOOK_LINE_SKIP);
    assert(parse("80TPB 2") == PHONEBOOK_LINE_SKIP);
    assert(parse("# LukasSoft|x|1|ansi80") == PHONEBOOK_LINE_SKIP);
    assert(parse("") == PHONEBOOK_LINE_SKIP && parse("   ") == PHONEBOOK_LINE_SKIP);
    /* A board, round trip with CRLF. */
    assert(parse("LukasSoft BBS|bbs.lukassoft.com|48822|ansi80") == PHONEBOOK_LINE_ENTRY);
    assert(!strcmp(entry.name, "LukasSoft BBS") && !strcmp(entry.hostname, "bbs.lukassoft.com"));
    assert(entry.port == 48822 && entry.profile == TELNET_PROFILE_ANSI_80);
    n = phonebook_format_line(out, &entry);
    assert(n == 46 && !memcmp(out, "LukasSoft BBS|bbs.lukassoft.com|48822|ansi80\r\n", 46));
    /* Mode names: written names, case-insensitive, and Flutter-only modes. */
    assert(parse("A|h|23|PETSCII80") == PHONEBOOK_LINE_ENTRY && entry.profile == TELNET_PROFILE_PETSCII_80);
    assert(parse("A|h|23|vt100") == PHONEBOOK_LINE_ENTRY && entry.profile == TELNET_PROFILE_VT100_80);
    assert(parse("A|h|23|petscii40") == PHONEBOOK_LINE_ENTRY && entry.profile == TELNET_PROFILE_PETSCII_80);
    assert(parse("Retro32|bbs.retro32.com|1337|amiga80") == PHONEBOOK_LINE_ENTRY && entry.profile == TELNET_PROFILE_ANSI_80);
    assert(parse("A|h|23|vic20") == PHONEBOOK_LINE_UNSUPPORTED);
    assert(parse("A|h|23|ansi") == PHONEBOOK_LINE_UNSUPPORTED);
    assert(parse("A|h|23|ansi80x") == PHONEBOOK_LINE_UNSUPPORTED);
    /* Later versions may add fields; names are trimmed and shortened. */
    assert(parse("  Board  |h|23|ansi80|future|fields") == PHONEBOOK_LINE_ENTRY && !strcmp(entry.name, "Board"));
    assert(parse("Amiga Underground BBS Long Name|h|23|ansi80") == PHONEBOOK_LINE_ENTRY);
    assert(strlen(entry.name) == PHONEBOOK_NAME_SIZE && !strncmp(entry.name, "Amiga Underground BBS Lo", 24));
    /* Invalid lines. */
    assert(parse("no separators") == PHONEBOOK_LINE_INVALID);
    assert(parse("|h|23|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A||23|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|has space|23|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h|0|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h|65536|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h|99999|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h|65535|ansi80") == PHONEBOOK_LINE_ENTRY && entry.port == 65535);
    assert(parse("A|h|2x|ansi80") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h|23") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h|23|") == PHONEBOOK_LINE_INVALID);
    assert(parse("A|h\x01|23|ansi80") == PHONEBOOK_LINE_INVALID);
    /* The longest entry fits the line buffer. */
    memset(&longest, 0, sizeof(longest));
    memset(longest.name, 'N', PHONEBOOK_NAME_SIZE);
    memset(longest.hostname, 'h', PHONEBOOK_HOST_SIZE);
    longest.port = 65535; longest.profile = TELNET_PROFILE_PETSCII_80;
    n = phonebook_format_line(out, &longest);
    assert(n && n <= PHONEBOOK_LINE_SIZE && out[n - 2] == 13 && out[n - 1] == 10);
    out[n - 2] = 0;
    assert(parse(out) == PHONEBOOK_LINE_ENTRY && !memcmp(&entry.hostname, longest.hostname, PHONEBOOK_HOST_SIZE + 1));
    /* Entries the editor could not have produced are refused. */
    strcpy(longest.name, "A|B");
    assert(!phonebook_format_line(out, &longest));
    strcpy(longest.name, "A"); longest.port = 0;
    assert(!phonebook_format_line(out, &longest));
    puts("phonebook tests passed");
    return 0;
}
