#ifndef T80_IRCCONFIG_H
#define T80_IRCCONFIG_H
#include <stdint.h>

/* The IRC server settings: edited and saved by the uimain overlay's IRC
 * form, used by the uiirc overlay. Resident RAM outside both overlays. */
#define IRC_NICK_SIZE 16    /* longest nickname the form accepts */
struct irc_settings {
    char host[65];
    char port_text[6];
    char nick[IRC_NICK_SIZE + 1];
    char channels[61];      /* #one,#two - or empty */
    uint16_t port;          /* port_text as a number, set by the form */
};
extern struct irc_settings irc_config;
/* Zero at startup: set once the "irc" file has been read into irc_config. */
extern uint8_t irc_config_loaded;

/* Decimal 1-65535, or 0 if text is not one. */
uint16_t irc_port(const char *text);
uint8_t irc_valid_nick(const char *nick);
/* A comma-separated list of channel names; an empty list is valid. */
uint8_t irc_valid_channels(const char *list);
#endif
