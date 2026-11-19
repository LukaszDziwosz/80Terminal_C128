#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "telnet.h"

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240

static uint8_t sent[128];
static uint8_t sent_length;

static void capture_send(const uint8_t* data, uint8_t length)
{
    assert((unsigned)sent_length + length <= sizeof(sent));
    memcpy(sent + sent_length, data, length);
    sent_length += length;
}

static void reset(enum telnet_profile profile)
{
    sent_length = 0;
    memset(sent, 0, sizeof(sent));
    telnet_init(profile, capture_send);
}

static void expect_sent(const uint8_t* expected, uint8_t length)
{
    assert(sent_length == length);
    assert(memcmp(sent, expected, length) == 0);
}

static void feed(const uint8_t* input, uint8_t length)
{
    uint8_t output;
    uint8_t i;

    for (i = 0; i < length; ++i) {
        (void)telnet_receive(input[i], &output);
    }
}

static void test_data_and_escaped_iac(void)
{
    uint8_t output;

    reset(TELNET_PROFILE_ANSI_80);
    assert(telnet_receive('A', &output) == 1);
    assert(output == 'A');
    assert(telnet_receive(IAC, &output) == 0);
    assert(telnet_receive(IAC, &output) == 1);
    assert(output == IAC);
}

static void test_supported_and_unknown_options(void)
{
    static const uint8_t offer_echo[] = {IAC, WILL, 1};
    static const uint8_t accept_echo[] = {IAC, DO, 1};
    static const uint8_t offer_unknown[] = {IAC, WILL, 42};
    static const uint8_t refuse_unknown[] = {IAC, DONT, 42};

    reset(TELNET_PROFILE_ANSI_80);
    feed(offer_echo, sizeof(offer_echo));
    expect_sent(accept_echo, sizeof(accept_echo));

    reset(TELNET_PROFILE_ANSI_80);
    feed(offer_unknown, sizeof(offer_unknown));
    expect_sent(refuse_unknown, sizeof(refuse_unknown));
}

static void test_terminal_type(void)
{
    static const uint8_t request[] = {
        IAC, DO, 24,
        IAC, SB, 24, 1, IAC, SE
    };
    static const uint8_t expected[] = {
        IAC, WILL, 24,
        IAC, SB, 24, 0, 0x41, 0x4E, 0x53, 0x49, IAC, SE
    };
    static const uint8_t expected_c128[] = {
        IAC, WILL, 24,
        IAC, SB, 24, 0, 0x43, 0x31, 0x32, 0x38, IAC, SE
    };
    static const uint8_t expected_vt100[] = {
        IAC, WILL, 24,
        IAC, SB, 24, 0, 0x56, 0x54, 0x31, 0x30, 0x30, IAC, SE
    };

    reset(TELNET_PROFILE_ANSI_80);
    feed(request, sizeof(request));
    expect_sent(expected, sizeof(expected));


    reset(TELNET_PROFILE_PETSCII_80);
    feed(request, sizeof(request));
    expect_sent(expected_c128, sizeof(expected_c128));

    reset(TELNET_PROFILE_VT100_80);
    feed(request, sizeof(request));
    expect_sent(expected_vt100, sizeof(expected_vt100));

    /* Amiga is a local display: the host sees ANSI, as from the Flutter app. */
    reset(TELNET_PROFILE_AMIGA_80);
    feed(request, sizeof(request));
    expect_sent(expected, sizeof(expected));
}

static void test_window_size(void)
{
    static const uint8_t request[] = {IAC, DO, 31};
    static const uint8_t expected_80[] = {
        IAC, WILL, 31,
        IAC, SB, 31, 0, 80, 0, 25, IAC, SE
    };
    static const uint8_t expected_vt100[] = {
        IAC, WILL, 31,
        IAC, SB, 31, 0, 80, 0, 24, IAC, SE
    };


    reset(TELNET_PROFILE_ANSI_80);
    feed(request, sizeof(request));
    expect_sent(expected_80, sizeof(expected_80));

    reset(TELNET_PROFILE_PETSCII_80);
    feed(request, sizeof(request));
    expect_sent(expected_80, sizeof(expected_80));

    reset(TELNET_PROFILE_VT100_80);
    feed(request, sizeof(request));
    expect_sent(expected_vt100, sizeof(expected_vt100));

    assert(telnet_profile_columns(TELNET_PROFILE_VT100_80) == 80);
    assert(telnet_profile_rows(TELNET_PROFILE_VT100_80) == 24);
    assert(telnet_profile_is_ansi(TELNET_PROFILE_VT100_80));
    assert(telnet_profile_is_vt100(TELNET_PROFILE_VT100_80));

    /* Monochrome profiles negotiate exactly as their colour profiles. */
    reset(TELNET_PROFILE_ANSI_MONO_80);
    feed(request, sizeof(request));
    expect_sent(expected_80, sizeof(expected_80));
    reset(TELNET_PROFILE_VT100_MONO_80);
    feed(request, sizeof(request));
    expect_sent(expected_vt100, sizeof(expected_vt100));
    assert(telnet_profile_rows(TELNET_PROFILE_VT100_MONO_80) == 24);
    assert(telnet_profile_rows(TELNET_PROFILE_ANSI_MONO_80) == 25);
    assert(telnet_profile_is_ansi(TELNET_PROFILE_ANSI_MONO_80) && !telnet_profile_is_vt100(TELNET_PROFILE_ANSI_MONO_80));
    assert(telnet_profile_is_ansi(TELNET_PROFILE_VT100_MONO_80) && telnet_profile_is_vt100(TELNET_PROFILE_VT100_MONO_80));
    assert(telnet_profile_is_mono(TELNET_PROFILE_ANSI_MONO_80) && telnet_profile_is_mono(TELNET_PROFILE_VT100_MONO_80));
    assert(!telnet_profile_is_mono(TELNET_PROFILE_ANSI_80) && !telnet_profile_is_mono(TELNET_PROFILE_VT100_80));
    assert(!telnet_profile_is_mono(TELNET_PROFILE_PETSCII_80));
    reset(TELNET_PROFILE_AMIGA_80);
    feed(request, sizeof(request));
    expect_sent(expected_80, sizeof(expected_80));
    assert(telnet_profile_is_ansi(TELNET_PROFILE_AMIGA_80) && !telnet_profile_is_vt100(TELNET_PROFILE_AMIGA_80));
    assert(!telnet_profile_is_mono(TELNET_PROFILE_AMIGA_80));
}

static void test_subnegotiation_is_not_displayed(void)
{
    static const uint8_t input[] = {
        'A', IAC, SB, 42, 1, 2, 3, IAC, SE, 'B'
    };
    uint8_t output;
    uint8_t rendered[2];
    uint8_t rendered_length;
    uint8_t i;

    reset(TELNET_PROFILE_ANSI_80);
    rendered_length = 0;
    for (i = 0; i < sizeof(input); ++i) {
        if (telnet_receive(input[i], &output)) {
            rendered[rendered_length++] = output;
        }
    }
    assert(rendered_length == 2);
    assert(rendered[0] == 'A');
    assert(rendered[1] == 'B');
}

static void test_outbound_iac_is_escaped(void)
{
    static const uint8_t expected[] = {IAC, IAC};

    reset(TELNET_PROFILE_ANSI_80);
    telnet_send_byte(IAC);
    expect_sent(expected, sizeof(expected));
}

static void test_binary_request(void)
{
    static const uint8_t expected[] = {
        IAC, DO, 0,
        IAC, WILL, 0
    };

    reset(TELNET_PROFILE_ANSI_80);
    telnet_request_binary();
    expect_sent(expected, sizeof(expected));
}

static void test_return_negotiation(void)
{
    reset(TELNET_PROFILE_ASCII_80);
    telnet_send_enter();
    expect_sent((const uint8_t[]){13}, 1);

    reset(TELNET_PROFILE_PETSCII_80);
    telnet_send_enter();
    expect_sent((const uint8_t[]){13}, 1);

    /* A new session resets accepted binary mode. */
    reset(TELNET_PROFILE_ASCII_80);
    telnet_send_enter();
    expect_sent((const uint8_t[]){13}, 1);
}

static void test_transfer_mode(void)
{
    static const uint8_t will_binary[] = {IAC, WILL, 0};
    static const uint8_t do_binary[] = {IAC, DO, 0};
    static const uint8_t cr_nul[] = {13, 0, 'x', 13, 13, 0};
    uint8_t output, got[8], n = 0, i;

    reset(TELNET_PROFILE_ANSI_80);
    /* Terminal use is unchanged: CR alone, NUL after CR delivered. */
    telnet_send_byte(13);
    assert(sent_length == 1 && sent[0] == 13);
    assert(telnet_receive(13, &output) == 1 && telnet_receive(0, &output) == 1 && output == 0);
    /* Transfer without binary mode: CR NUL both ways. */
    telnet_set_transfer(1);
    sent_length = 0;
    telnet_send_byte(13);
    telnet_send_byte(0);
    assert(sent_length == 3 && sent[0] == 13 && sent[1] == 0 && sent[2] == 0);
    for (i = 0; i < sizeof(cr_nul); ++i)
        if (telnet_receive(cr_nul[i], &output)) got[n++] = output;
    assert(n == 4 && got[0] == 13 && got[1] == 'x' && got[2] == 13 && got[3] == 13);
    /* A NUL that is real data (not after CR) passes. */
    assert(telnet_receive(0, &output) == 1 && output == 0);
    /* Once both directions are binary, bytes pass unchanged. */
    feed(will_binary, sizeof(will_binary));
    feed(do_binary, sizeof(do_binary));
    sent_length = 0;
    telnet_send_byte(13);
    assert(sent_length == 1);
    assert(telnet_receive(13, &output) == 1 && telnet_receive(0, &output) == 1 && output == 0);
    telnet_set_transfer(0);
}

int main(void)
{
    test_data_and_escaped_iac();
    test_supported_and_unknown_options();
    test_terminal_type();
    test_window_size();
    test_subnegotiation_is_not_displayed();
    test_outbound_iac_is_escaped();
    test_binary_request();
    test_return_negotiation();
    test_transfer_mode();
    puts("telnet tests passed");
    return 0;
}
