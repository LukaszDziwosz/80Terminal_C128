#ifndef TERMINAL80_TELNET_H
#define TERMINAL80_TELNET_H

#include <stdint.h>

enum telnet_profile {
    /* Values 1..3 preserve the cc65 phonebook wire format. */
    TELNET_PROFILE_PETSCII_80 = 1,
    TELNET_PROFILE_ANSI_80,
    TELNET_PROFILE_VT100_80,
    TELNET_PROFILE_ASCII_80
};

typedef void (*telnet_send_callback)(const uint8_t* data, uint8_t length);

void telnet_init(enum telnet_profile profile,
                 telnet_send_callback send_callback);
uint8_t telnet_receive(uint8_t input, uint8_t* output);
void telnet_send_byte(uint8_t value);
/* Replaces where escaped output goes, keeping the negotiated options:
 * a file transfer sends through its own queue mid-session. */
void telnet_set_sender(telnet_send_callback send_callback);
void telnet_send_enter(void);
void telnet_request_binary(void);
/* During a file transfer: when an option is not in binary mode, send CR as
 * CR NUL and drop the NUL a server sends after CR, so every byte value
 * passes through unchanged. The terminal itself never uses this. */
void telnet_set_transfer(uint8_t enabled);
uint8_t telnet_profile_columns(enum telnet_profile profile);
uint8_t telnet_profile_rows(enum telnet_profile profile);
uint8_t telnet_profile_is_ansi(enum telnet_profile profile);
uint8_t telnet_profile_is_vt100(enum telnet_profile profile);
const char* telnet_profile_name(enum telnet_profile profile);

#endif
