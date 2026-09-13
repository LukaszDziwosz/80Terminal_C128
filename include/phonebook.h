#ifndef T80_PHONEBOOK_H
#define T80_PHONEBOOK_H
#include <stdint.h>
#include "telnet.h"
#define PHONEBOOK_MAX 20
#define PHONEBOOK_NAME_SIZE 16
#define PHONEBOOK_HOST_SIZE 64
#define PHONEBOOK_HEADER_SIZE 6
#define PHONEBOOK_RECORD_SIZE 85

/* Serialize fields explicitly: never persist compiler struct/enum layout. */
struct phonebook_entry {
    char name[PHONEBOOK_NAME_SIZE + 1];
    char hostname[PHONEBOOK_HOST_SIZE + 1];
    uint16_t port;
    enum telnet_profile profile;
};
void phonebook_header_encode(uint8_t *data, uint8_t count);
int phonebook_header_decode(const uint8_t *data, uint16_t size);
uint8_t phonebook_entry_encode(uint8_t *data, const struct phonebook_entry *entry);
uint8_t phonebook_entry_decode(struct phonebook_entry *entry,
                               const uint8_t *data, uint16_t size);
#endif
