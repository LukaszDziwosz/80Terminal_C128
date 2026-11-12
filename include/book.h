#ifndef T80_BOOK_H
#define T80_BOOK_H
#include <stdint.h>
#include "network.h"
#include "ui.h"

/* Resident: how many entries the uibook overlay keeps in bank 1
 * (bank1.h). The main screen reads the recent list from there too. */
struct book_state {
    uint8_t count;         /* saved boards at BANK1_PHONEBOOK */
    uint8_t recent_count;  /* at BANK1_RECENT, newest first */
};
extern struct book_state book;

/* Entry points of the uibook overlay (overlay.h): call overlay_use() first. */
/* Reads the phonebook and recent files from the boot disk into bank 1.
 * Without a phonebook file it starts with the LukasSoft BBS entry. */
void book_startup(void);
/* The phonebook screen. Returns UI_CONNECT with request filled in, or UI_BACK. */
uint8_t book_run(const struct net_backend *backend, struct connect_request *request);
/* After a successful connection: moves it to the top of the recent list
 * and saves the recent file. */
void book_note_recent(const struct connect_request *request);
#endif
