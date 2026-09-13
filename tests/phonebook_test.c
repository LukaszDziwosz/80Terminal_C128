#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "phonebook.h"

int main(void)
{
    uint8_t header[6];
    uint8_t record[PHONEBOOK_RECORD_SIZE];
    struct phonebook_entry original = {"Example", "bbs.example", 2323, TELNET_PROFILE_ANSI_80};
    struct phonebook_entry decoded;
    phonebook_header_encode(header, 20);
    assert(header[0] == 0xd5 && header[3] == 0xc2);
    assert(phonebook_header_decode(header, 6) == 20);
    assert(phonebook_header_decode(header, 5) == -1);
    header[5] = 21;
    assert(phonebook_header_decode(header, 6) == -1);
    header[5] = 0;
    header[0] = 'X';
    assert(phonebook_header_decode(header, 6) == -1);
    assert(phonebook_entry_encode(record, &original));
    assert(record[82] == 0x13 && record[83] == 0x09 && record[84] == 2);
    assert(phonebook_entry_decode(&decoded, record, sizeof(record)));
    assert(!strcmp(decoded.name, original.name));
    assert(!strcmp(decoded.hostname, original.hostname));
    assert(decoded.port == 2323 && decoded.profile == TELNET_PROFILE_ANSI_80);
    record[84] = 0;
    assert(phonebook_entry_decode(&decoded, record, sizeof(record)));
    assert(decoded.profile == TELNET_PROFILE_PETSCII_80);
    record[84] = 4;
    assert(!phonebook_entry_decode(&decoded, record, sizeof(record)));
    record[84] = 3;
    assert(!phonebook_entry_decode(&decoded, record, sizeof(record) - 1));
    record[82] = record[83] = 0;
    assert(!phonebook_entry_decode(&decoded, record, sizeof(record)));
    record[82] = 23;
    memset(record + 17, 'a', 65);
    assert(!phonebook_entry_decode(&decoded, record, sizeof(record)));
    puts("phonebook tests passed");
    return 0;
}
