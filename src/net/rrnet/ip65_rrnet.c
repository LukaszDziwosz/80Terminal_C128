/*
 * CS8900A/RR-Net driver adapted from IP65 drivers/cs8900a.s.
 *
 * Copyright (c) 2007, Adam Dunkels and Oliver Schmidt.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain this notice, this list of
 *    conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce this notice, this list of
 *    conditions and the following disclaimer in the documentation and/or
 *    other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE, ARE DISCLAIMED. IN NO EVENT SHALL THE INSTITUTE
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES.
 */
#include "ip65_rrnet.h"

#ifndef RRNET_IP65_TEST
#pragma code(rrcode)
#pragma data(rrdata)
#pragma bss(rrbss)
#define READ(address) (*(volatile uint8_t *)(address))
#define WRITE(address, value) (*(volatile uint8_t *)(address) = (value))
#else
#define READ(address) rrnet_test_read(address)
#define WRITE(address, value) rrnet_test_write(address, value)
#endif

#define RR_ISQ_HI       0xde01U
#define RR_PACKETPP_LO  0xde02U
#define RR_PACKETPP_HI  0xde03U
#define RR_PPDATA_LO    0xde04U
#define RR_PPDATA_HI    0xde05U
#define RR_RXTX_LO      0xde08U
#define RR_RXTX_HI      0xde09U
#define RR_TXCMD_LO     0xde0cU
#define RR_TXCMD_HI     0xde0dU
#define RR_TXLEN_LO     0xde0eU
#define RR_TXLEN_HI     0xde0fU

#define PP_RX_CFG       0x0104U
#define PP_LINE_CTL     0x0112U
#define PP_SELF_CTL     0x0114U
#define PP_RX_EVENT     0x0124U
#define PP_BUS_STATUS   0x0138U
#define PP_INDIVIDUAL   0x0158U
#define PP_SKIP_FRAME   0x0102U

uint8_t rrnet_ip65_mac[6];
/* The original cc65 RR-Net driver used this locally administered address.
 * Some adapters do not expose a readable EEPROM MAC in this configuration. */
static const uint8_t fallback_mac[6] = {0x02, 0x55, 0x54, 0x80, 0x00, 0x01};

static void select_page(uint16_t page)
{
    WRITE(RR_PACKETPP_LO, (uint8_t)page);
    WRITE(RR_PACKETPP_HI, (uint8_t)(page >> 8));
}
#if defined(__OSCAR64C__) && !defined(RRNET_IP65_TEST)
__noinline
#endif
static void write_page(uint16_t page, uint16_t value)
{
#if defined(__OSCAR64C__) && !defined(RRNET_IP65_TEST)
    /* Oscar -O2 moved C volatile data writes before the page selection in
     * the MAC loop. Keep this hardware transaction indivisible. */
    __asm volatile {
        lda page
        sta $de02
        lda page + 1
        sta $de03
        lda value
        sta $de04
        lda value + 1
        sta $de05
    }
#else
    select_page(page);
    WRITE(RR_PPDATA_LO, (uint8_t)value);
    WRITE(RR_PPDATA_HI, (uint8_t)(value >> 8));
#endif
}
#if defined(__OSCAR64C__) && !defined(RRNET_IP65_TEST)
__noinline
#endif
static uint16_t receive_length(void)
{
#if defined(__OSCAR64C__) && !defined(RRNET_IP65_TEST)
    /* Both discarded status reads are required FIFO operations. Oscar -O2
     * eliminated the C (void) volatile reads and reordered the length pair. */
    return __asm volatile {
        lda $de09
        lda $de08
        lda $de09
        sta accu + 1
        lda $de08
        sta accu
    };
#else
    uint8_t high, low;
    (void)READ(RR_RXTX_HI); (void)READ(RR_RXTX_LO);
    high = READ(RR_RXTX_HI); low = READ(RR_RXTX_LO);
    return low | ((uint16_t)high << 8);
#endif
}
static uint8_t chip_present(void)
{
    uint8_t low, high;

    /* This is the bytewise EISA-ID check from cs8900a.s.  It avoids relying
     * on a 16-bit comparison while probing a byte-wide I/O device. */
    select_page(0);
    low = READ(RR_PPDATA_LO);
    high = READ(RR_PPDATA_HI);
    return low == 0x0e && high == 0x63;
}
static void skip_frame(void)
{
    uint8_t value;
    select_page(PP_SKIP_FRAME);
    value = READ(RR_PPDATA_LO);
    WRITE(RR_PPDATA_LO, value | 0x40);
}
static uint8_t load_mac(void)
{
    uint8_t low, high, checksum;
    uint8_t i;

    /* Cartridge-mode RR-Net exposes the complete EEPROM MAC here. */
    for (i = 0; i < 3; ++i) {
        select_page(PP_INDIVIDUAL + (uint16_t)i * 2);
        rrnet_ip65_mac[(uint16_t)i * 2] = READ(RR_PPDATA_LO);
        rrnet_ip65_mac[(uint16_t)i * 2 + 1] = READ(RR_PPDATA_HI);
    }
    if (rrnet_ip65_mac[0] == 0x28 && rrnet_ip65_mac[1] == 0xcd &&
        rrnet_ip65_mac[2] == 0x4c && rrnet_ip65_mac[3] == 0xff) return 1;

    /* Clock-port RR-Net exposes its last two MAC bytes and checksums in the
     * normally write-only transmit registers. This is IP65's fallback. */
    low = READ(RR_TXCMD_LO);
    high = READ(RR_TXCMD_HI);
    checksum = (uint8_t)(low ^ high ^ 0x55);
    if (checksum != READ(RR_TXLEN_LO)) return 0;
    checksum = (uint8_t)(checksum + low + high);
    if ((uint8_t)(checksum ^ 0xaa) != READ(RR_TXLEN_HI)) return 0;
    rrnet_ip65_mac[0] = 0x28; rrnet_ip65_mac[1] = 0xcd;
    rrnet_ip65_mac[2] = 0x4c; rrnet_ip65_mac[3] = 0xff;
    rrnet_ip65_mac[4] = low; rrnet_ip65_mac[5] = high;
    return 1;
}

uint8_t rrnet_ip65_init(void)
{
    uint16_t attempts;
    uint8_t i, reset_done;

    WRITE(RR_ISQ_HI, READ(RR_ISQ_HI) | 1); /* Enable the RR-Net clock port. */
    if (!chip_present()) return RRNET_IP65_NO_CHIP;
    if (!load_mac()) {
        for (i = 0; i < 6; ++i) rrnet_ip65_mac[i] = fallback_mac[i];
    }

    write_page(PP_SELF_CTL, 0x0040);
    /* IP65 waits indefinitely here.  Keep the same register protocol but
     * bound the wait so the terminal can report a missing/faulty adapter. */
    attempts = 0;
    reset_done = 0;
    do {
        select_page(PP_SELF_CTL);
        if (!(READ(RR_PPDATA_LO) & 0x40)) {
            reset_done = 1;
            break;
        }
        ++attempts;
    } while (attempts);
    if (!reset_done) return RRNET_IP65_RESET_TIMEOUT;

    write_page(PP_RX_CFG, 0x0d05); /* Accepted unicast and broadcast frames. */
    for (i = 0; i < 3; ++i) {
        write_page(PP_INDIVIDUAL + (uint16_t)i * 2,
                   rrnet_ip65_mac[(uint16_t)i * 2] |
                   ((uint16_t)rrnet_ip65_mac[(uint16_t)i * 2 + 1] << 8));
    }
    write_page(PP_LINE_CTL, 0x00d3); /* Enable receiver and transmitter. */
    return 0;
}

uint16_t rrnet_ip65_receive(uint8_t *buffer, uint16_t capacity)
{
    uint16_t length, wire_length, i;

    select_page(PP_RX_EVENT);
    if (!(READ(RR_PPDATA_HI) & 0x0d)) return 0;
    /* RR-Net's byte ports sample receive status and length high, then low. */
    length = receive_length();
    wire_length = (length + 1U) & (uint16_t)~1U;
    if (!length || wire_length > capacity || wire_length > RRNET_IP65_FRAME_CAPACITY) {
        skip_frame();
        return 0;
    }
    for (i = 0; i < wire_length; i += 2) {
        buffer[i] = READ(RR_RXTX_LO);
        buffer[i + 1] = READ(RR_RXTX_HI);
    }
    return length;
}

uint8_t rrnet_ip65_send(const uint8_t *buffer, uint16_t length)
{
    uint16_t wire_length, i;
    uint8_t retries;

    if (!length || length > RRNET_IP65_FRAME_CAPACITY) return 1;
    /* The CS8900A frame FIFO is word-wide.  As in IP65's cs8900a.s, keep
     * the requested Ethernet length in TXLEN and add one FIFO byte only
     * when it is odd.  The adapter performs Ethernet minimum-frame padding. */
    wire_length = (length + 1U) & (uint16_t)~1U;
    WRITE(RR_TXCMD_LO, 0xc9); WRITE(RR_TXCMD_HI, 0);
    WRITE(RR_TXLEN_LO, (uint8_t)length);
    WRITE(RR_TXLEN_HI, (uint8_t)(length >> 8));
    for (retries = 0; retries != 8; ++retries) {
        select_page(PP_BUS_STATUS);
        if (READ(RR_PPDATA_HI) & 1) break;
        skip_frame();
    }
    if (retries == 8) return 1;
    for (i = 0; i < wire_length; i += 2) {
        WRITE(RR_RXTX_LO, i < length ? buffer[i] : 0);
        WRITE(RR_RXTX_HI, i + 1 < length ? buffer[i + 1] : 0);
    }
    return 0;
}

void rrnet_ip65_shutdown(void) { }

#ifndef RRNET_IP65_TEST
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
