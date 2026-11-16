#ifndef T80_IRC_H
#define T80_IRC_H
#include <stdint.h>

/* IRC protocol helpers shared by the uiirc overlay and its tests. No
 * network, screen or disk access here. Text is treated as bytes: ASCII is
 * shown, UTF-8 sequences are reduced to '?'. */
#define IRC_LINE_SIZE 512   /* a line with its NUL; longer lines are dropped */
#define IRC_PARAMS 15

struct irc_message {
    char *nick;                 /* source up to '!', or "" without a source */
    char *command;              /* upper case */
    char *param[IRC_PARAMS];
    uint8_t count;
};
/* Splits line in place (it is modified). IRCv3 tags are skipped. Returns 0
 * for an empty or malformed line. */
uint8_t irc_parse(char *line, struct irc_message *message);
/* rfc1459 case mapping: A-Z and [ \ ] ^ equal a-z and { | } ~. */
uint8_t irc_same(const char *a, const char *b);
uint8_t irc_is_channel(const char *name);
/* In place: removes mIRC colour and style codes and other control bytes,
 * and turns each UTF-8 sequence into one '?'. */
void irc_clean(char *text);
/* Whether text names nick as a whole word. */
uint8_t irc_mentions(const char *text, const char *nick);
#endif
