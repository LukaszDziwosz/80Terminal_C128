#include "ircconfig.h"

uint16_t irc_port(const char *text)
{
    uint16_t port = 0;
    if (!*text) return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return 0;
        /* At most 65535, without overflow. */
        if (port > 6553 || (port == 6553 && *text > '5')) return 0;
        port = (uint16_t)(port * 10 + (uint8_t)(*text - '0'));
    }
    return port;
}
static uint8_t nick_letter(char c)
{
    /* Letters and [ \ ] ^ _ ` { | } */
    return (c >= 'A' && c <= '}' && c != '~');
}
uint8_t irc_valid_nick(const char *nick)
{
    uint8_t i;
    char c;
    if (!nick_letter(nick[0])) return 0;
    for (i = 1; (c = nick[i]) != 0; ++i) {
        if (i >= IRC_NICK_SIZE) return 0;
        if (!(nick_letter(c) || (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return 1;
}
uint8_t irc_valid_channels(const char *list)
{
    uint8_t length;
    while (*list) {
        if (*list != '#' && *list != '&') return 0;
        for (length = 0; *list && *list != ','; ++list, ++length)
            if (*list == ' ' || *list == ':' || (uint8_t)*list < 32) return 0;
        if (length < 2) return 0;
        if (*list == ',' && !*++list) return 0;
    }
    return 1;
}
