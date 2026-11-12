#ifdef PROGRAM_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#endif
#include "program.h"
#include "overlay.h"
#include "session.h"
#include "ui.h"
#include "book.h"

#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma bss(lowbss)
#endif
static struct connect_request request;
static struct session_result result;
struct book_state book;
#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma bss(bss)
#endif

void program_message(const char *text)
{
    clrscr();
    gotoxy(3, 5);
    while (*text) putch(*text++);
    gotoxy(3, 18);
    for (text = "Press any key to exit to BASIC."; *text; ) putch(*text++);
    getch();
}
static uint8_t use(uint8_t id)
{
    if (overlay_use(id)) return 1;
    (void)id;
    program_message("Could not load a program file. Use the complete disk image.");
    return 0;
}
/* The launcher hands ownership to this program once. Sessions return here.
 * The main screen and the phonebook are separate overlays that take turns
 * in the UI slot; only this loop, which is resident, moves between them. */
void program_run(const struct net_backend *backend)
{
    uint8_t action;
    result.outcome = SESSION_NONE;
    book.count = book.recent_count = 0;
    if (!use(OVERLAY_UI_BOOK)) goto done;
    book_startup();
    for (;;) {
        if (!use(OVERLAY_UI_MAIN)) break;
        action = ui_main(backend, &request, &result);
        if (action == UI_QUIT) break;
        if (action == UI_PHONEBOOK) {
            if (!use(OVERLAY_UI_BOOK)) break;
            if (book_run(backend, &request) != UI_CONNECT) continue;
            if (!use(OVERLAY_UI_MAIN)) break;
            if (!ui_prepare_connect(backend, &request)) continue;
        }
        if (!use(OVERLAY_UI_TERM)) break;
        session_run(backend, &request, &result);
        if (result.outcome == SESSION_ENDED) {
            if (!use(OVERLAY_UI_BOOK)) break;
            book_note_recent(&request);
        }
    }
done:
    backend->close();
    while (backend->state() == NET_CLOSING) backend->poll();
}
