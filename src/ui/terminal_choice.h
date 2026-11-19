#ifndef T80_UI_TERMINAL_CHOICE_H
#define T80_UI_TERMINAL_CHOICE_H
/* The terminal profiles a connection can use, for the connect form and the
 * phonebook editor. Header-only, like draw.h: include it after draw.h. */
#include "telnet.h"

#define TERMINAL_CHOICES 6
static const char *terminal_label(enum telnet_profile profile)
{
    return profile == TELNET_PROFILE_PETSCII_80 ? "PETSCII" :
           profile == TELNET_PROFILE_VT100_80 ? "VT100" :
           profile == TELNET_PROFILE_ANSI_MONO_80 ? "ANSI mono" :
           profile == TELNET_PROFILE_VT100_MONO_80 ? "VT100 mono" :
           profile == TELNET_PROFILE_AMIGA_80 ? "Amiga" : "ANSI";
}
/* Keys '1' to '6'; 0 for any other key. */
static uint8_t terminal_for_key(uint8_t key)
{
    return key == '1' ? TELNET_PROFILE_ANSI_80 :
           key == '2' ? TELNET_PROFILE_PETSCII_80 :
           key == '3' ? TELNET_PROFILE_VT100_80 :
           key == '4' ? TELNET_PROFILE_ANSI_MONO_80 :
           key == '5' ? TELNET_PROFILE_VT100_MONO_80 :
           key == '6' ? TELNET_PROFILE_AMIGA_80 : 0;
}
/* One row: "1 ANSI 2 PETSCII 3 VT100 4 ANSI mono 5 VT100 mono 6 Amiga", 57
 * columns, inside the panel from column 16 (its right rim is column 77). */
static void terminal_choices(uint8_t col, uint8_t row)
{
    char digit[2];
    digit[1] = 0;
    for (digit[0] = '1'; digit[0] < '1' + TERMINAL_CHOICES; ++digit[0]) {
        col = ui_text(col, row, digit, UI_ACTION);
        col = ui_text((uint8_t)(col + 1), row, terminal_label((enum telnet_profile)terminal_for_key((uint8_t)digit[0])), UI_TEXT);
        col = (uint8_t)(col + 1);
    }
}
#endif
