#include <string.h>
#include "phonebook.h"

struct mode_name { const char *name; uint8_t profile; };
/* The first three are written; the rest are accepted when reading. */
static const struct mode_name modes[] = {
    {"petscii80", TELNET_PROFILE_PETSCII_80},
    {"ansi80", TELNET_PROFILE_ANSI_80},
    {"vt100", TELNET_PROFILE_VT100_80},
    {"petscii40", TELNET_PROFILE_PETSCII_80},
    {"amiga80", TELNET_PROFILE_ANSI_80},
    {"ascii80", TELNET_PROFILE_ANSI_80},
};
#define WRITTEN_MODES 3
#define MODE_COUNT (sizeof(modes) / sizeof(modes[0]))

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

const char *phonebook_mode_name(enum telnet_profile profile)
{
    uint8_t i;
    for (i = 0; i < WRITTEN_MODES; ++i)
        if (modes[i].profile == (uint8_t)profile) return modes[i].name;
    return 0;
}
/* Returns the field length; *next is set past the separator, or to the end. */
static uint8_t field(const char *line, const char **next)
{
    uint8_t n = 0;
    while (line[n] && line[n] != '|') { if (n == 255) break; ++n; }
    *next = line[n] == '|' ? line + n + 1 : line + n;
    return n;
}
static uint8_t printable(const char *s, uint8_t n, uint8_t allow_space)
{
    uint8_t i;
    for (i = 0; i < n; ++i)
        if ((uint8_t)s[i] < (allow_space ? 32 : 33) || (uint8_t)s[i] > 126) return 0;
    return 1;
}
uint8_t phonebook_parse_line(const char *line, struct phonebook_entry *entry)
{
    const char *name, *host, *port, *mode, *end;
    uint8_t name_length, host_length, port_length, mode_length, i, j;
    uint16_t value = 0;
    while (*line == ' ') ++line;
    if (!*line || *line == '#') return PHONEBOOK_LINE_SKIP;
    if (!strncmp(line, "80TPB", 5)) return PHONEBOOK_LINE_SKIP;
    name = line;
    name_length = field(name, &host);
    if (!*host && host[-1] != '|') return PHONEBOOK_LINE_INVALID;
    host_length = field(host, &port);
    port_length = field(port, &mode);
    mode_length = field(mode, &end);
    while (name_length && name[name_length - 1] == ' ') --name_length;
    if (!name_length || !printable(name, name_length, 1)) return PHONEBOOK_LINE_INVALID;
    if (!host_length || host_length > PHONEBOOK_HOST_SIZE || !printable(host, host_length, 0))
        return PHONEBOOK_LINE_INVALID;
    if (!port_length || port_length > 5) return PHONEBOOK_LINE_INVALID;
    for (i = 0; i < port_length; ++i) {
        if (port[i] < '0' || port[i] > '9') return PHONEBOOK_LINE_INVALID;
        if (value > 6553 || (value == 6553 && port[i] > '5')) return PHONEBOOK_LINE_INVALID;
        value = (uint16_t)(value * 10 + (uint8_t)(port[i] - '0'));
    }
    if (!value || !mode_length) return PHONEBOOK_LINE_INVALID;
    for (i = 0; i < MODE_COUNT; ++i) {
        for (j = 0; j < mode_length && modes[i].name[j] && lower(mode[j]) == modes[i].name[j]; ++j) {}
        if (j == mode_length && !modes[i].name[j]) break;
    }
    if (i == MODE_COUNT) return PHONEBOOK_LINE_UNSUPPORTED;
    /* Long names from other clients are shortened, not rejected. */
    if (name_length > PHONEBOOK_NAME_SIZE) name_length = PHONEBOOK_NAME_SIZE;
    memcpy(entry->name, name, name_length); entry->name[name_length] = 0;
    memcpy(entry->hostname, host, host_length); entry->hostname[host_length] = 0;
    entry->port = value;
    entry->profile = (enum telnet_profile)modes[i].profile;
    return PHONEBOOK_LINE_ENTRY;
}
static char *append(char *out, const char *s)
{
    while (*s) *out++ = *s++;
    return out;
}
uint8_t phonebook_format_line(char *out, const struct phonebook_entry *entry)
{
    char digits[5], *p = out;
    const char *mode = phonebook_mode_name(entry->profile);
    uint16_t port = entry->port;
    uint8_t n = 0, name_length = (uint8_t)strlen(entry->name);
    uint8_t host_length = (uint8_t)strlen(entry->hostname);
    if (!mode || !port || !name_length || name_length > PHONEBOOK_NAME_SIZE ||
        !host_length || host_length > PHONEBOOK_HOST_SIZE) return 0;
    /* '|' would split the name; the editor never accepts it. */
    if (strchr(entry->name, '|') || strchr(entry->hostname, '|')) return 0;
    p = append(p, entry->name); *p++ = '|';
    p = append(p, entry->hostname); *p++ = '|';
    do { digits[n++] = (char)('0' + port % 10); port /= 10; } while (port);
    while (n) *p++ = digits[--n];
    *p++ = '|';
    p = append(p, mode);
    *p++ = 13; *p++ = 10;
    return (uint8_t)(p - out);
}
