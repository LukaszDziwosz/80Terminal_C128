#include <string.h>
#include "phonebook.h"

void phonebook_header_encode(uint8_t *data, uint8_t count)
{
    /* cc65 C128 char literals use PETSCII: 'U','T','P','B' are D5 D4 D0 C2.
     * Explicit bytes also make Oscar64/host builds produce the same file. */
    data[0] = 0xd5; data[1] = 0xd4; data[2] = 0xd0; data[3] = 0xc2;
    data[4] = 1; data[5] = count;
}
int phonebook_header_decode(const uint8_t *data, uint16_t size)
{
    if (size != PHONEBOOK_HEADER_SIZE || data[4] != 1 || data[5] > PHONEBOOK_MAX)
        return -1;
    /* Accept ASCII UTPB too, for tools that exported the documented format. */
    if (!((data[0] == 0xd5 && data[1] == 0xd4 && data[2] == 0xd0 && data[3] == 0xc2) ||
          (data[0] == 0x55 && data[1] == 0x54 && data[2] == 0x50 && data[3] == 0x42)))
        return -1;
    return data[5];
}
static uint8_t valid_string(const char *s, uint8_t limit)
{
    uint8_t i;
    if (!s[0]) return 0;
    for (i = 0; i <= limit; ++i) {
        if (!s[i]) return 1;
        if ((uint8_t)s[i] < 32 || (uint8_t)s[i] > 126) return 0;
    }
    return 0;
}
static uint8_t valid_entry(const struct phonebook_entry *entry)
{
    return valid_string(entry->name, PHONEBOOK_NAME_SIZE) &&
           valid_string(entry->hostname, PHONEBOOK_HOST_SIZE) && entry->port &&
           entry->profile >= TELNET_PROFILE_PETSCII_80 &&
           entry->profile <= TELNET_PROFILE_VT100_80;
}
uint8_t phonebook_entry_encode(uint8_t *data, const struct phonebook_entry *entry)
{
    if (!valid_entry(entry)) return 0;
    memset(data, 0, PHONEBOOK_RECORD_SIZE);
    memcpy(data, entry->name, strlen(entry->name));
    memcpy(data + 17, entry->hostname, strlen(entry->hostname));
    data[82] = (uint8_t)entry->port;
    data[83] = (uint8_t)(entry->port >> 8);
    data[84] = (uint8_t)entry->profile;
    return 1;
}
uint8_t phonebook_entry_decode(struct phonebook_entry *entry,
                               const uint8_t *data, uint16_t size)
{
    if (size != PHONEBOOK_RECORD_SIZE) return 0;
    memcpy(entry->name, data, 17);
    memcpy(entry->hostname, data + 17, 65);
    entry->port = data[82] | ((uint16_t)data[83] << 8);
    /* Legacy PETSCII 40 entries become PETSCII 80. No 40-column runtime. */
    entry->profile = data[84] == 0 ? TELNET_PROFILE_PETSCII_80 :
                                   (enum telnet_profile)data[84];
    return valid_entry(entry);
}
