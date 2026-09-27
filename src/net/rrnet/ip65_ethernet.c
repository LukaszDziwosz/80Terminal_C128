/*
 * Ethernet framing ported from IP65 ip65/eth.s and eth_buffer.s.
 * Original Code: ip65; Initial Developer: Per Olofsson <MagerValp@gmail.com>.
 * This file is a modification of that source, made 2026-09-15.  It is
 * available under MPL 1.1; the unmodified license is in
 * References/80Terminal_cc65/third_party/ip65/LICENSE.txt.
 */
#include "ip65_ethernet.h"
#include "platform.h"

#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#endif

uint8_t rrnet_ip65_eth_in[RRNET_IP65_FRAME_CAPACITY];
uint8_t rrnet_ip65_eth_out[RRNET_IP65_FRAME_CAPACITY];
uint16_t rrnet_ip65_eth_in_length;
uint16_t rrnet_ip65_eth_out_length;

void rrnet_ip65_eth_set_broadcast_destination(void)
{
#ifdef __OSCAR64C__
    __asm volatile {
        ldx #5
        lda #$ff
    rrnet_ip65_eth_broadcast_loop:
        sta rrnet_ip65_eth_out,x
        dex
        bpl rrnet_ip65_eth_broadcast_loop
    }
#else
    uint8_t i;
    for (i = 0; i < 6; ++i) rrnet_ip65_eth_out[i] = 0xff;
#endif
}

void rrnet_ip65_eth_set_source(void)
{
#ifdef __OSCAR64C__
    __asm volatile {
        ldx #5
    rrnet_ip65_eth_source_loop:
        lda rrnet_ip65_mac,x
        sta rrnet_ip65_eth_out + 6,x
        dex
        bpl rrnet_ip65_eth_source_loop
    }
#else
    uint8_t i;
    for (i = 0; i < 6; ++i) rrnet_ip65_eth_out[6U + i] = rrnet_ip65_mac[i];
#endif
}

void rrnet_ip65_eth_set_protocol(uint8_t protocol)
{
#ifdef __OSCAR64C__
    __asm volatile {
        lda protocol
        sta rrnet_ip65_eth_out + 13
        lda #8
        sta rrnet_ip65_eth_out + 12
    }
#else
    rrnet_ip65_eth_out[12] = 8;
    rrnet_ip65_eth_out[13] = protocol;
#endif
}

uint8_t rrnet_ip65_eth_poll(void)
{
    uint8_t speed = platform_slow();
    rrnet_ip65_eth_in_length = rrnet_ip65_receive(
        rrnet_ip65_eth_in, RRNET_IP65_FRAME_CAPACITY);
    platform_restore_speed(speed);
    return rrnet_ip65_eth_in_length >= RRNET_IP65_ETH_HEADER;
}

uint8_t rrnet_ip65_eth_send(void)
{
    uint8_t speed, result;
    if (rrnet_ip65_eth_out_length < RRNET_IP65_ETH_HEADER ||
        rrnet_ip65_eth_out_length > RRNET_IP65_FRAME_CAPACITY) return 1;
    speed = platform_slow();
    result = rrnet_ip65_send(rrnet_ip65_eth_out, rrnet_ip65_eth_out_length);
    platform_restore_speed(speed);
    return result;
}

#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
