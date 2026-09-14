#include <conio.h>
#include "program.h"
#include "session.h"
#include "platform.h"
#include "vdc_screen.h"

static void line(uint8_t row, const char *text)
{
    uint8_t col = 0;
    gotoxy(3, row);
    while (*text && col < 73) { putch(*text++); ++col; }
    while (col++ < 73) putch(' ');
}
static void draw_main(const struct net_backend *backend, uint8_t ready)
{
    clrscr();
    line(2, "80Terminal / MAIN");
    line(5, "Network interface:");
    line(6, backend->name);
    line(9, ready ? "Ready for a connection." : "Interface unavailable. F1 retries initialization.");
    line(11, backend->status);
    line(16, "F3  Terminal / connect");
    line(18, "F7  CP437 font preview");
    line(23, "F1 Retry interface     F3 Terminal     F7 Font     F8 Quit");
}
/* The launcher hands ownership to this program once. Sessions return here. */
void program_run(const struct net_backend *backend)
{
    uint8_t key, ready;
    draw_main(backend, 0);
    line(9, "Initializing interface...");
    ready = backend->init() >= 0;
    draw_main(backend, ready);
    for (;;) {
        key = getch();
        if (key == PETSCII_F8 || key == 'q' || key == 'Q') break;
        if (key == PETSCII_F1) {
            backend->close();
            while (backend->state() == NET_CLOSING) backend->poll();
            line(9, "Initializing interface...");
            ready = backend->init() >= 0;
        } else if (key == PETSCII_F3 && ready) {
            session_open(backend);
        } else if (key == PETSCII_F7) {
            screen_font_preview(platform_device());
        } else continue;
        draw_main(backend, ready);
    }
    backend->close();
    while (backend->state() == NET_CLOSING) backend->poll();
}
