/* Ported from References/80Terminal_cc65/src/telnet.c. */
#include <stdint.h>

#include "telnet.h"

#define TELNET_IAC   255
#define TELNET_DONT  254
#define TELNET_DO    253
#define TELNET_WONT  252
#define TELNET_WILL  251
#define TELNET_SB    250
#define TELNET_SE    240

#define TELNET_OPTION_BINARY 0
#define TELNET_OPTION_ECHO   1
#define TELNET_OPTION_SGA    3
#define TELNET_OPTION_TTYPE  24
#define TELNET_OPTION_NAWS   31

#define TELNET_TTYPE_IS   0
#define TELNET_TTYPE_SEND 1

#define TELNET_STATE_DATA       0
#define TELNET_STATE_IAC        1
#define TELNET_STATE_NEGOTIATE  2
#define TELNET_STATE_SB_OPTION  3
#define TELNET_STATE_SB_DATA    4
#define TELNET_STATE_SB_IAC     5

#define TELNET_FLAG_BINARY 0x01
#define TELNET_FLAG_ECHO   0x02
#define TELNET_FLAG_SGA    0x04
#define TELNET_FLAG_TTYPE  0x08
#define TELNET_FLAG_NAWS   0x10

static const uint8_t terminal_ansi[] = {0x41, 0x4E, 0x53, 0x49};
static const uint8_t terminal_vt100[] = {0x56, 0x54, 0x31, 0x30, 0x30};
static const uint8_t terminal_c128[] = {0x43, 0x31, 0x32, 0x38};
static const uint8_t terminal_dumb[] = {0x44, 0x55, 0x4d, 0x42};

static uint8_t state;
static uint8_t command;
static uint8_t suboption;
static uint8_t suboption_first;
static uint8_t suboption_length;
static uint8_t remote_options;
static uint8_t local_options;
static enum telnet_profile current_profile;
static telnet_send_callback send_bytes;

static uint8_t option_flag(uint8_t option)
{
    switch (option) {
    case TELNET_OPTION_BINARY:
        return TELNET_FLAG_BINARY;
    case TELNET_OPTION_ECHO:
        return TELNET_FLAG_ECHO;
    case TELNET_OPTION_SGA:
        return TELNET_FLAG_SGA;
    case TELNET_OPTION_TTYPE:
        return TELNET_FLAG_TTYPE;
    case TELNET_OPTION_NAWS:
        return TELNET_FLAG_NAWS;
    default:
        return 0;
    }
}

static uint8_t accept_remote_option(uint8_t option)
{
    return option == TELNET_OPTION_BINARY ||
           option == TELNET_OPTION_ECHO ||
           option == TELNET_OPTION_SGA;
}

static uint8_t accept_local_option(uint8_t option)
{
    return option == TELNET_OPTION_BINARY ||
           option == TELNET_OPTION_SGA ||
           option == TELNET_OPTION_TTYPE ||
           option == TELNET_OPTION_NAWS;
}

static void send_command(uint8_t reply, uint8_t option)
{
    uint8_t data[3];

    if (send_bytes == 0) {
        return;
    }
    data[0] = TELNET_IAC;
    data[1] = reply;
    data[2] = option;
    send_bytes(data, sizeof(data));
}

static void send_window_size(void)
{
    uint8_t data[9];

    if (send_bytes == 0) {
        return;
    }
    data[0] = TELNET_IAC;
    data[1] = TELNET_SB;
    data[2] = TELNET_OPTION_NAWS;
    data[3] = 0;
    data[4] = telnet_profile_columns(current_profile);
    data[5] = 0;
    data[6] = telnet_profile_rows(current_profile);
    data[7] = TELNET_IAC;
    data[8] = TELNET_SE;
    send_bytes(data, sizeof(data));
}

static void send_terminal_type(void)
{
    uint8_t data[11];
    const uint8_t* name;
    uint8_t length;
    uint8_t i;

    if (send_bytes == 0) {
        return;
    }
    if (current_profile == TELNET_PROFILE_PETSCII_80) {
        name = terminal_c128;
        length = sizeof(terminal_c128);
    } else if (current_profile == TELNET_PROFILE_ASCII_80) {
        name = terminal_dumb;
        length = sizeof(terminal_dumb);
    } else if (current_profile == TELNET_PROFILE_VT100_80) {
        name = terminal_vt100;
        length = sizeof(terminal_vt100);
    } else {
        name = terminal_ansi;
        length = sizeof(terminal_ansi);
    }

    data[0] = TELNET_IAC;
    data[1] = TELNET_SB;
    data[2] = TELNET_OPTION_TTYPE;
    data[3] = TELNET_TTYPE_IS;
    for (i = 0; i < length; ++i) {
        data[4 + i] = name[i];
    }
    data[4 + length] = TELNET_IAC;
    data[5 + length] = TELNET_SE;
    send_bytes(data, (uint8_t)(6 + length));
}

static void negotiate(uint8_t option)
{
    uint8_t flag;

    flag = option_flag(option);
    if (command == TELNET_WILL) {
        if (!accept_remote_option(option)) {
            send_command(TELNET_DONT, option);
        } else if ((remote_options & flag) == 0) {
            remote_options |= flag;
            send_command(TELNET_DO, option);
        }
    } else if (command == TELNET_WONT) {
        remote_options &= (uint8_t)~flag;
    } else if (command == TELNET_DO) {
        if (!accept_local_option(option)) {
            send_command(TELNET_WONT, option);
        } else if ((local_options & flag) == 0) {
            local_options |= flag;
            send_command(TELNET_WILL, option);
            if (option == TELNET_OPTION_NAWS) {
                send_window_size();
            }
        }
    } else {
        local_options &= (uint8_t)~flag;
    }
}

static void finish_suboption(void)
{
    if (suboption == TELNET_OPTION_TTYPE &&
        suboption_length != 0 &&
        suboption_first == TELNET_TTYPE_SEND &&
        (local_options & TELNET_FLAG_TTYPE) != 0) {
        send_terminal_type();
    }
}

void telnet_init(enum telnet_profile profile,
                 telnet_send_callback send_callback)
{
    state = TELNET_STATE_DATA;
    command = 0;
    suboption = 0;
    suboption_first = 0;
    suboption_length = 0;
    remote_options = 0;
    local_options = 0;
    current_profile = profile;
    send_bytes = send_callback;
}

uint8_t telnet_receive(uint8_t input, uint8_t* output)
{
    switch (state) {
    case TELNET_STATE_DATA:
        if (input == TELNET_IAC) {
            state = TELNET_STATE_IAC;
            return 0;
        }
        *output = input;
        return 1;

    case TELNET_STATE_IAC:
        if (input == TELNET_IAC) {
            state = TELNET_STATE_DATA;
            *output = input;
            return 1;
        }
        if (input == TELNET_DO || input == TELNET_DONT ||
            input == TELNET_WILL || input == TELNET_WONT) {
            command = input;
            state = TELNET_STATE_NEGOTIATE;
        } else if (input == TELNET_SB) {
            state = TELNET_STATE_SB_OPTION;
        } else {
            state = TELNET_STATE_DATA;
        }
        return 0;

    case TELNET_STATE_NEGOTIATE:
        negotiate(input);
        state = TELNET_STATE_DATA;
        return 0;

    case TELNET_STATE_SB_OPTION:
        suboption = input;
        suboption_first = 0;
        suboption_length = 0;
        state = TELNET_STATE_SB_DATA;
        return 0;

    case TELNET_STATE_SB_DATA:
        if (input == TELNET_IAC) {
            state = TELNET_STATE_SB_IAC;
        } else {
            if (suboption_length == 0) {
                suboption_first = input;
            }
            ++suboption_length;
        }
        return 0;

    default:
        if (input == TELNET_SE) {
            finish_suboption();
            state = TELNET_STATE_DATA;
        } else {
            if (input == TELNET_IAC) {
                if (suboption_length == 0) {
                    suboption_first = input;
                }
                ++suboption_length;
            }
            state = TELNET_STATE_SB_DATA;
        }
        return 0;
    }
}

void telnet_send_byte(uint8_t value)
{
    uint8_t data[2];

    if (send_bytes == 0) {
        return;
    }
    data[0] = value;
    if (value == TELNET_IAC) {
        data[1] = value;
        send_bytes(data, 2);
    } else {
        send_bytes(data, 1);
    }
}

void telnet_request_binary(void)
{
    send_command(TELNET_DO, TELNET_OPTION_BINARY);
    send_command(TELNET_WILL, TELNET_OPTION_BINARY);
}

void telnet_send_enter(void)
{
    telnet_send_byte(13);
}

uint8_t telnet_profile_columns(enum telnet_profile profile)
{
    (void)profile;
    return 80;
}

uint8_t telnet_profile_rows(enum telnet_profile profile)
{
    return profile == TELNET_PROFILE_VT100_80 ? 24 : 25;
}

uint8_t telnet_profile_is_ansi(enum telnet_profile profile)
{
    return profile == TELNET_PROFILE_ANSI_80 ||
           profile == TELNET_PROFILE_VT100_80;
}

uint8_t telnet_profile_is_vt100(enum telnet_profile profile)
{
    return profile == TELNET_PROFILE_VT100_80;
}

const char* telnet_profile_name(enum telnet_profile profile)
{
    switch (profile) {
    case TELNET_PROFILE_PETSCII_80:
        return "PETSCII 80 columns";
    case TELNET_PROFILE_ANSI_80:
        return "ANSI 80 columns";
    case TELNET_PROFILE_ASCII_80:
        return "Plain ASCII 80 columns";
    default:
        return "VT100 80x24";
    }
}
