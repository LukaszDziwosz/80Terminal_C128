#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ansi_terminal.h"

static uint8_t vdc[16384], sent[32], sent_length, restored;
static uint16_t cursor_address;
static uint8_t cursor_visible;

uint8_t screen_load_cp437(uint8_t device) { return device == 8; }
void screen_restore_font(void) { ++restored; }
uint8_t screen_reg_read(uint8_t reg)
{
    if (reg == 20) return 8;
    return 0;
}
void screen_reg_write(uint8_t reg, uint8_t value) { (void)reg; (void)value; }
void screen_write_run(uint16_t at, const uint8_t *data, uint16_t length)
{ memcpy(vdc + at, data, length); }
void screen_fill_run(uint16_t at, uint8_t value, uint16_t length)
{ memset(vdc + at, value, length); }
void screen_copy_run(uint16_t destination, uint16_t source, uint16_t length)
{ memmove(vdc + destination, vdc + source, length); }
void screen_set_cursor(uint16_t at, uint8_t visible)
{ cursor_address = at; cursor_visible = visible; }
static void send(const uint8_t *data, uint8_t length)
{ memcpy(sent + sent_length, data, length); sent_length += length; }
static void feed(const char *text)
{ while (*text) ansi_terminal_receive((uint8_t)*text++); }

int main(void)
{
    uint8_t key[5];
    assert(ansi_terminal_init(send, 8));
    assert(vdc[0] == ' ' && vdc[0x800] == 0x8f);
    feed("A\033[3;4HB");
    assert(vdc[0] == 'A' && vdc[163] == 'B');
    assert(cursor_address == 164 && cursor_visible);
    feed("\033[31mC");
    assert(vdc[164] == 'C' && vdc[0x800 + 164] == 0x88);
    sent_length = 0;
    feed("\033[6n\033[5n\033[c");
    assert(!memcmp(sent, "\033[3;6R\033[0n\033[?1;0c", 18));
    assert(ansi_terminal_key(0x91, key) == 3);
    assert(!memcmp(key, "\033[A", 3));
    assert(ansi_terminal_key('Q', key) == 1 && key[0] == 'q');
    assert(ansi_terminal_key(13, key) == 1 && key[0] == 13);
    ansi_terminal_receive_buffer((const uint8_t *)"\033[2J", 4);
    assert(vdc[0] == ' ' && vdc[163] == ' ');
    ansi_terminal_shutdown();
    assert(restored == 1 && !cursor_visible);
    puts("ANSI terminal parser tests passed");
    return 0;
}
