#include <conio.h>
#include <c64/kernalio.h>
#include "layout.h"
#include "network.h"
#include "platform.h"
#include "vdc_screen.h"
#include "session.h"

static void line(uint8_t row, const char *text)
{
    gotoxy(3, row);
    while (*text) putch(*text++);
}
static void menu(void)
{
    clrscr();
    line(2, "80Terminal / Oscar64 foundation");
    line(4, "Commodore 128 - 80 columns");
    line(7, "1 / F1   RR-Net");
    line(9, "2 / F3   1541 Ultimate II+");
    line(11, "3 / F5   WiC64");
    line(15, "F7   CP437 font preview");
    line(17, "F8   Exit to BASIC");
    line(21, "WiC64 and Ultimate: direct TCP. RR-Net: work in progress.");
}
static void select_backend(uint8_t choice)
{
    const struct net_backend *backend;
    const char *filename = choice == 1 ? p"netrr" :
                           choice == 2 ? p"netult" : p"netwic";
    uint8_t speed = platform_slow();
    krnio_setbnk(0, 0);
    krnio_setnam(filename);
    uint8_t loaded = krnio_load(1, platform_device(), 1);
    platform_restore_speed(speed);
    clrscr();
    if (!loaded) {
        line(5, "Could not load the selected adapter. Use the complete disk image.");
    } else {
        backend = choice == 1 ? rrnet_backend() :
                  choice == 2 ? ultimate_backend() : wic64_backend();
        line(2, backend->name);
        int result = backend->init();
        if (result == NET_NOT_IMPLEMENTED || result < 0) line(5, backend->status);
        else session_open(backend);
    }
    line(18, "Press any key to return to the launcher.");
    getch();
    /* No overlay pointers survive a change of adapter. */
}
int main(void)
{
    uint8_t key;
    platform_init();
    for (;;) {
        menu();
        key = getch();
        if (key == '1' || key == PETSCII_F1) select_backend(1);
        else if (key == '2' || key == PETSCII_F3) select_backend(2);
        else if (key == '3' || key == PETSCII_F5) select_backend(3);
        else if (key == PETSCII_F7) screen_font_preview(platform_device());
        else if (key == PETSCII_F8 || key == 'q') break;
    }
    platform_exit();
    return 0;
}
