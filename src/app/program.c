#ifdef PROGRAM_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#endif
#include "program.h"
#include "overlay.h"
#include "session.h"
#include "ui.h"

#if defined(__OSCAR64C__) && !defined(PROGRAM_TEST)
#pragma bss(lowbss)
#endif
static struct connect_request request;
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
/* The launcher hands ownership to this program once. Sessions return here. */
void program_run(const struct net_backend *backend)
{
    for (;;) {
        /* A session may have used the UI slot for a transfer overlay. */
        if (!overlay_use(OVERLAY_UI_MAIN)) {
            program_message("Could not load uimain. Use the complete disk image.");
            break;
        }
        if (ui_main(backend, &request) != UI_CONNECT) break;
        session_run(backend, &request);
    }
    backend->close();
    while (backend->state() == NET_CLOSING) backend->poll();
}
