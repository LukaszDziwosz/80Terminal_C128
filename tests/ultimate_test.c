#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "network.h"
#include "uci.h"

static uint8_t command[896], response[600];
static char status_text[100];
static uint16_t command_size, response_size, position, part_end, status_pos;
static uint16_t ticks;
static uint8_t hw_state, identity, delay, ack_delay, never_ready, split_reply;
static uint8_t read_mode, partial_write, write_calls, close_calls, error_reply;
static uint8_t sent_bytes[1024];
static uint16_t sent_size;
static uint8_t unlock_step, unlock_supported;
static const struct net_backend *net;

void uci_test_unlock(uint16_t address, uint8_t value)
{
    if (unlock_step == 0) { assert(address == 0xd038 && value == 0xab); unlock_step = 1; }
    else {
        assert(address == 0xd036 && value == 0xcd);
        if (unlock_supported) identity = 0xc9;
        unlock_step = 0;
    }
}

static void reply(uint16_t size, const char *status)
{
    response_size = size; position = status_pos = 0;
    strcpy(status_text, status);
    part_end = split_reply && size > 2 ? 2 : size;
    hw_state = part_end < size ? 0x30 : 0x20;
    delay = 2;
}
uint16_t uci_test_ticks(void) { return ticks; }
uint8_t uci_test_read(uint8_t reg)
{
    if (reg == 1) return identity;
    if (reg == 2) { assert(position < part_end); return response[position++]; }
    if (reg == 3) { assert(status_text[status_pos]); return (uint8_t)status_text[status_pos++]; }
    if (never_ready) return 0x10;
    if (delay) { --delay; return 0x11; }
    if (ack_delay) { --ack_delay; return 2; }
    return hw_state | (position < part_end ? 0x80 : 0) |
           (part_end == response_size && status_text[status_pos] ? 0x40 : 0);
}
void uci_test_write(uint8_t reg, uint8_t value)
{
    uint16_t size;
    if (reg == 1) { assert(command_size < sizeof(command)); command[command_size++] = value; return; }
    assert(reg == 0);
    if (value & 4) {
        hw_state = 0; command_size = position = part_end = response_size = status_pos = 0;
        status_text[0] = 0; delay = ack_delay = 0;
        return;
    }
    if (value == 2) {
        assert(position == part_end);
        if (part_end < response_size) {
            part_end = response_size; hw_state = 0x20; delay = 2;
        } else {
            assert(!status_text[status_pos]);
            hw_state = 0; ack_delay = 1;
        }
        return;
    }
    assert(value == 1 && command_size >= 2 && command[0] == 3);
    switch (command[1]) {
    case 1: memcpy(response, "ULTIMATE", 8); reply(8, "00,OK"); break;
    case 5:
        assert(command_size == 3 && command[2] == 0);
        memset(response, 0, 12); response[0] = 192; response[1] = 168;
        response[2] = 1; response[3] = 20; reply(12, "00,OK"); break;
    case 7:
        assert(command_size == 16);
        assert(command[2] == 0x13 && command[3] == 9);
        assert(!strcmp((char *)command + 4, "bbs.example"));
        response[0] = 42;
        reply(1, error_reply ? "84,UNRESOLVED HOST" : "00,OK");
        break;
    case 0x10:
        assert(command_size == 5 && command[2] == 42);
        assert(command[3] == 0 && command[4] == 2);
        if (read_mode == 1) { response[0] = response[1] = 0; reply(2, "01,CONNECTION CLOSED BY HOST"); }
        else if (read_mode == 2) { response[0] = response[1] = 255; reply(2, "02,NO DATA: 11"); }
        else if (read_mode == 3) { response[0] = response[1] = 255; reply(2, "02,NO DATA: 9"); }
        else if (read_mode == 4) { response[0] = 0; response[1] = 2; reply(3, "00,OK"); }
        else if (read_mode == 5) { memset(response, 0, sizeof(response)); reply(600, "00,OK"); }
        else {
            response[0] = 4; response[1] = 0;
            response[2] = 0; response[3] = 255; response[4] = 'A'; response[5] = 13;
            reply(6, "00,OK");
        }
        break;
    case 0x11:
        assert(command[2] == 42);
        size = command_size - 3;
        if (partial_write && write_calls == 0) size = 2;
        assert(sent_size + size <= sizeof(sent_bytes));
        memcpy(sent_bytes + sent_size, command + 3, size); sent_size += size;
        response[0] = (uint8_t)size; response[1] = (uint8_t)(size >> 8);
        ++write_calls;
        reply(2, "00,OK");
        break;
    case 9:
        assert(command_size == 3 && command[2] == 42);
        ++close_calls; reply(0, "00,OK"); break;
    default: assert(0);
    }
    command_size = 0;
}
static void step(void) { ++ticks; net->poll(); }
static void until(enum net_state state)
{
    unsigned i;
    for (i = 0; i < 2000 && net->state() != (int)state; ++i) step();
    if (net->state() != (int)state) {
        printf("Expected state %d, got %d: %s\n", (int)state, (int)net->state(), net->status);
        assert(net->state() == (int)state);
    }
}
static void reset(void)
{
    identity = 0xc9; never_ready = split_reply = read_mode = error_reply = 0;
    unlock_step = unlock_supported = 0;
    partial_write = write_calls = close_calls = 0;
    sent_size = 0; ticks = 0;
    net = ultimate_backend();
    assert(net->init() == NET_PENDING);
    until(NET_CLOSED);
}
static void connect_ok(void)
{
    assert(net->connect("bbs.example", 2323) == NET_PENDING);
    until(NET_CONNECTED);
}
static void test_connect_and_binary_read(void)
{
    uint8_t bytes[8];
    reset(); split_reply = 1; connect_ok();
    for (unsigned i = 0; i < 30; ++i) step();
    assert(net->read(bytes, 2) == 2 && bytes[0] == 0 && bytes[1] == 255);
    assert(net->read(bytes + 2, 6) == 2 && bytes[2] == 'A' && bytes[3] == 13);
    net->close(); until(NET_CLOSED); assert(close_calls == 1);
}
static void test_partial_write(void)
{
    static const uint8_t text[] = {0, 255, 'A', 'B', 'C'};
    reset(); connect_ok(); partial_write = 1;
    assert(net->write(text, sizeof(text)) == sizeof(text));
    assert(net->write(text, 1) == 0);
    for (unsigned i = 0; i < 30; ++i) step();
    assert(write_calls == 2 && sent_size == sizeof(text));
    assert(!memcmp(sent_bytes, text, sizeof(text)));
    net->close(); until(NET_CLOSED);
}
static void test_no_data_and_close(void)
{
    uint8_t byte;
    reset(); connect_ok(); read_mode = 2;
    for (unsigned i = 0; i < 20; ++i) step();
    assert(net->state() == NET_CONNECTED && net->read(&byte, 1) == 0);
    read_mode = 1; until(NET_EOF);
    net->close(); until(NET_CLOSED); assert(close_calls == 0);
}
static void test_failures(void)
{
    reset(); error_reply = 1;
    assert(net->connect("bbs.example", 2323) == NET_PENDING);
    until(NET_FAILED); assert(strstr(net->status, "UNRESOLVED") != 0);
    net->close(); until(NET_CLOSED);
    for (uint8_t mode = 3; mode <= 5; ++mode) {
        reset(); connect_ok(); read_mode = mode;
        until(NET_FAILED);
        net->close(); until(NET_CLOSED); assert(close_calls == 1);
    }
    reset(); never_ready = 1;
    assert(net->connect("bbs.example", 2323) == NET_IO_ERROR);
    assert(net->state() == NET_FAILED);
    reset(); connect_ok(); never_ready = 1;
    step(); /* start cannot write a command over a busy interface */
    assert(net->state() == NET_FAILED);
    reset();
    assert(net->connect("bbs.example", 2323) == NET_PENDING);
    never_ready = 1; ticks += 1800; step();
    assert(net->state() == NET_FAILED && strstr(net->status, "timed out") != 0);
    identity = 255;
    assert(net->init() == NET_NO_DEVICE);
    assert(net->state() == NET_FAILED);
}
static void test_cancel_during_connect_and_validation(void)
{
    char too_long[66];
    reset();
    memset(too_long, 'a', 65); too_long[65] = 0;
    assert(net->connect(too_long, 23) == NET_BAD_ARGUMENT);
    assert(net->connect("", 23) == NET_BAD_ARGUMENT);
    assert(net->connect("bbs.example", 0) == NET_BAD_ARGUMENT);
    assert(net->connect("bbs.example", 2323) == NET_PENDING);
    net->close(); until(NET_CLOSED); assert(close_calls == 1);
}
int main(void)
{
    reset(); identity = 255; unlock_supported = 1;
    assert(net->init() == NET_PENDING); until(NET_CLOSED);
    test_connect_and_binary_read();
    test_partial_write();
    test_no_data_and_close();
    test_failures();
    test_cancel_during_connect_and_validation();
    puts("Ultimate async TCP/UCI tests passed");
    return 0;
}
