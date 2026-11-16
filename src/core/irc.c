#include "irc.h"

static char no_nick[1];

static char fold(char c)
{
    return c >= 'A' && c <= '^' ? (char)(c + 32) : c;
}
static char *skip_spaces(char *s)
{
    while (*s == ' ') ++s;
    return s;
}
/* Ends the word at s with a NUL; returns the start of the next word. */
static char *end_word(char *s)
{
    while (*s && *s != ' ') ++s;
    if (!*s) return s;
    *s = 0;
    return skip_spaces(s + 1);
}
uint8_t irc_parse(char *line, struct irc_message *message)
{
    char *s = skip_spaces(line), *c;
    no_nick[0] = 0;
    message->nick = no_nick;
    message->count = 0;
    if (*s == '@') {
        s = end_word(s);
        if (!*s) return 0;
    }
    if (*s == ':') {
        message->nick = s + 1;
        s = end_word(s);
        if (!*s) return 0;
        for (c = message->nick; *c; ++c)
            if (*c == '!') { *c = 0; break; }
    }
    if (!*s) return 0;
    message->command = s;
    s = end_word(s);
    for (c = message->command; *c; ++c)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    while (*s && message->count < IRC_PARAMS) {
        if (*s == ':') {
            message->param[message->count++] = s + 1;
            break;
        }
        message->param[message->count++] = s;
        s = end_word(s);
    }
    return 1;
}
/* Indexed, not "++a, ++b": Oscar64 1.32.273 -O2 inlined this into a loop
 * over names and advanced the caller's own pointer with the parameter, so
 * after "#retro" against "#c64" (one character equal) every later
 * comparison began at "c64". The helpers here and in the IRC overlay
 * therefore never modify a pointer parameter. */
uint8_t irc_same(const char *a, const char *b)
{
    uint8_t i = 0;
    while (a[i] && fold(a[i]) == fold(b[i])) ++i;
    return !a[i] && !b[i];
}
uint8_t irc_is_channel(const char *name)
{
    return name[0] == '#' || name[0] == '&';
}
static uint8_t is_digit(char c) { return c >= '0' && c <= '9'; }
static uint8_t is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
/* Up to count characters accepted by hex (or decimal) digits. */
static const char *skip_digits(const char *s, uint8_t count, uint8_t hex)
{
    while (count && (hex ? is_hex(*s) : is_digit(*s))) { ++s; --count; }
    return s;
}
void irc_clean(char *text)
{
    const char *in = text;
    char *out = text;
    uint8_t c, hex;
    while ((c = (uint8_t)*in++) != 0) {
        if (c == 3 || c == 4) {
            /* Colour: ^C fg[,bg] in decimal, or ^D rrggbb[,rrggbb]. */
            hex = c == 4;
            in = skip_digits(in, hex ? 6 : 2, hex);
            if (*in == ',' && (hex ? is_hex(in[1]) : is_digit(in[1])))
                in = skip_digits(in + 1, hex ? 6 : 2, hex);
        } else if (c == 9) *out++ = ' ';
        else if (c >= 0xc0) *out++ = '?';
        else if (c >= 32 && c < 127) *out++ = (char)c;
    }
    *out = 0;
}
static uint8_t is_word(char c)
{
    c = fold(c);
    return (c >= 'a' && c <= 'z') || is_digit(c) || c == '_';
}
uint8_t irc_mentions(const char *text, const char *nick)
{
    uint16_t at;
    uint8_t i;
    char before = ' ';
    if (!nick[0]) return 0;
    for (at = 0; text[at]; before = text[at++]) {
        if (is_word(before)) continue;
        for (i = 0; nick[i] && fold(text[at + i]) == fold(nick[i]); ++i) {}
        if (!nick[i] && !is_word(text[at + i])) return 1;
    }
    return 0;
}
