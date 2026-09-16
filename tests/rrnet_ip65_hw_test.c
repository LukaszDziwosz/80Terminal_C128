#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ip65_rrnet.h"
#include "ip65_ethernet.h"
#include "ip65_arp.h"

#define PP_LO 0xde02U
#define PP_HI 0xde03U
#define PP_DATA_LO 0xde04U
#define PP_DATA_HI 0xde05U
#define RXTX_LO 0xde08U
#define RXTX_HI 0xde09U

static uint8_t ports[16], rx_stream[32], tx_stream[160];
static uint16_t pages[512], page, rx_size, rx_position, tx_size;

uint8_t rrnet_test_read(uint16_t address)
{
    if (address == PP_DATA_LO) return (uint8_t)pages[page];
    if (address == PP_DATA_HI) return (uint8_t)(pages[page] >> 8);
    if (address == RXTX_LO || address == RXTX_HI) {
        assert(rx_position < rx_size);
        return rx_stream[rx_position++];
    }
    assert(address >= 0xde00U && address <= 0xde0fU);
    return ports[address - 0xde00U];
}
void rrnet_test_write(uint16_t address, uint8_t value)
{
    if (address == PP_LO) { page = (page & 0xff00U) | value; return; }
    if (address == PP_HI) { page = (page & 0x00ffU) | ((uint16_t)value << 8); return; }
    if (address == PP_DATA_LO) {
        if (page == 0x0114U && value == 0x40) pages[page] = 0;
        else pages[page] = (pages[page] & 0xff00U) | value;
        return;
    }
    if (address == PP_DATA_HI) { pages[page] = (pages[page] & 0x00ffU) | ((uint16_t)value << 8); return; }
    if (address == RXTX_LO || address == RXTX_HI) {
        assert(tx_size < sizeof(tx_stream)); tx_stream[tx_size++] = value; return;
    }
    assert(address >= 0xde00U && address <= 0xde0fU);
    ports[address - 0xde00U] = value;
}
static void setup(uint8_t legacy)
{
    memset(ports, 0, sizeof(ports)); memset(pages, 0, sizeof(pages));
    rx_size = rx_position = tx_size = 0;
    pages[0] = 0x630e;
    if (!legacy) {
        pages[0x158] = 0xcd28; pages[0x15a] = 0xff4c; pages[0x15c] = 0x2211;
    } else {
        ports[0x0c] = 0x11; ports[0x0d] = 0x22;
        ports[0x0e] = 0x66; ports[0x0f] = 0x33;
    }
    pages[0x138] = 0x0100;
    assert(rrnet_ip65_init() == 0);
}
static void test_init(void)
{
    setup(0);
    assert((ports[1] & 1) != 0);
    assert(!memcmp(rrnet_ip65_mac, (uint8_t[]){0x28,0xcd,0x4c,0xff,0x11,0x22}, 6));
    assert(pages[0x104] == 0x0d05 && pages[0x112] == 0x00d3);
    setup(1);
    assert(!memcmp(rrnet_ip65_mac, (uint8_t[]){0x28,0xcd,0x4c,0xff,0x11,0x22}, 6));
    memset(ports, 0, sizeof(ports)); memset(pages, 0, sizeof(pages));
    pages[0] = 0x630e;
    assert(rrnet_ip65_init() == 0);
    assert(!memcmp(rrnet_ip65_mac, (uint8_t[]){0x02,0x55,0x54,0x80,0x00,0x01}, 6));
    assert(pages[0x158] == 0x5502 && pages[0x15a] == 0x8054 &&
           pages[0x15c] == 0x0100);
    pages[0] = 0; assert(rrnet_ip65_init() == RRNET_IP65_NO_CHIP);
}
static void test_receive_and_send(void)
{
    uint8_t input[8], output[] = {0x41, 0x00, 0x42, 0xff};
    setup(0);
    pages[0x124] = 0x0d00;
    memcpy(rx_stream, (uint8_t[]){0x0d,0x00,0x00,0x04,0x41,0x00,0x42,0xff}, 8);
    rx_size = 8;
    assert(rrnet_ip65_receive(input, sizeof(input)) == 4);
    assert(!memcmp(input, output, sizeof(output)));
    tx_size = 0;
    assert(rrnet_ip65_send(output, sizeof(output)) == 0);
    assert(ports[0x0e] == sizeof(output) && ports[0x0f] == 0 && tx_size == sizeof(output));
    assert(!memcmp(tx_stream, output, sizeof(output)));

    tx_size = 0;
    assert(rrnet_ip65_send(output, 3) == 0);
    assert(ports[0x0e] == 3 && tx_size == 4);
    assert(!memcmp(tx_stream, output, 3) && tx_stream[3] == 0);
}
static void test_ethernet_headers(void)
{
    uint8_t i;

    setup(0);
    memset(rrnet_ip65_eth_out, 0, RRNET_IP65_FRAME_CAPACITY);
    rrnet_ip65_eth_set_broadcast_destination();
    rrnet_ip65_eth_set_source();
    rrnet_ip65_eth_set_protocol(RRNET_IP65_ETH_ARP);
    for (i = 0; i < 6; ++i) {
        assert(rrnet_ip65_eth_out[i] == 0xff);
        assert(rrnet_ip65_eth_out[6U + i] == rrnet_ip65_mac[i]);
    }
    assert(rrnet_ip65_eth_out[12] == 8 &&
           rrnet_ip65_eth_out[13] == RRNET_IP65_ETH_ARP);
}
static void test_arp_request_and_cache(void)
{
    uint8_t target[] = {192, 168, 1, 10};
    uint8_t local[] = {192, 168, 1, 42};
    uint8_t mask[] = {255, 255, 255, 0};
    uint8_t gateway[] = {192, 168, 1, 1};
    uint8_t remote_mac[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    uint8_t mac[6];

    setup(0);
    rrnet_ip65_arp_set_config(local, mask, gateway);
    rrnet_ip65_arp_init();
    tx_size = 0;
    assert(!rrnet_ip65_arp_lookup(target, mac, 100));
    assert(rrnet_ip65_eth_out_length == 42 && tx_size == 42);
    assert(rrnet_ip65_eth_out[12] == 8 && rrnet_ip65_eth_out[13] == 6);
    assert(rrnet_ip65_eth_out[20] == 0 && rrnet_ip65_eth_out[21] == 1);
    assert(!memcmp(rrnet_ip65_eth_out + 38, target, 4));

    memset(rrnet_ip65_eth_in, 0, 42);
    memcpy(rrnet_ip65_eth_in + 6, remote_mac, 6);
    rrnet_ip65_eth_in[12] = 8; rrnet_ip65_eth_in[13] = 6;
    rrnet_ip65_eth_in[15] = 1; rrnet_ip65_eth_in[16] = 8;
    rrnet_ip65_eth_in[18] = 6; rrnet_ip65_eth_in[19] = 4;
    rrnet_ip65_eth_in[21] = 2;
    memcpy(rrnet_ip65_eth_in + 22, remote_mac, 6);
    memcpy(rrnet_ip65_eth_in + 28, target, 4);
    memcpy(rrnet_ip65_eth_in + 38, local, 4);
    rrnet_ip65_eth_in_length = 42;
    rrnet_ip65_arp_process();
    assert(rrnet_ip65_arp_lookup(target, mac, 101));
    assert(!memcmp(mac, remote_mac, 6));
}
int main(void)
{
    test_init(); test_receive_and_send(); test_ethernet_headers();
    test_arp_request_and_cache();
    puts("RR-Net IP65 hardware tests passed");
    return 0;
}
