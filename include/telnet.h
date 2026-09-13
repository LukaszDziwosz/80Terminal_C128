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
/* Advertise the options a terminal needs before the first receive poll. */
void telnet_startup(void);
uint8_t telnet_receive(uint8_t input, uint8_t* output);
void telnet_send_byte(uint8_t value);
void telnet_send_enter(void);
void telnet_request_binary(void);
uint8_t telnet_profile_columns(enum telnet_profile profile);
uint8_t telnet_profile_rows(enum telnet_profile profile);
uint8_t telnet_profile_is_ansi(enum telnet_profile profile);
uint8_t telnet_profile_is_vt100(enum telnet_profile profile);
const char* telnet_profile_name(enum telnet_profile profile);

#endif
