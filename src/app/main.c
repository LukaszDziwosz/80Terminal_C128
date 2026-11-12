#include <conio.h>
#include <c64/kernalio.h>
#include "layout.h"
#include "network.h"
#include "overlay.h"
#include "platform.h"
#include "program.h"
#include "ui.h"

/* Adapter overlays load once; no overlay pointers survive a change of adapter. */
static void select_backend(uint8_t choice)
{
    const char *filename = choice == 1 ? p"netrr" :
                           choice == 2 ? p"netult" : p"netwic";
    uint8_t speed = platform_slow();
    krnio_setbnk(0, 0);
    krnio_setnam(filename);
    uint8_t loaded = krnio_load(1, platform_device(), 1);
    platform_restore_speed(speed);
    if (!loaded) {
        program_message("Could not load the selected adapter. Use the complete disk image.");
        return;
    }
    clrscr();
    ui_reset();
    program_run(choice == 1 ? rrnet_backend() :
                choice == 2 ? ultimate_backend() : wic64_backend());
}
int main(void)
{
    uint8_t choice;
    platform_init();
    if (!overlay_use(OVERLAY_UI_MAIN)) {
        program_message("Could not load uimain. Use the complete disk image.");
    } else {
        choice = ui_launcher();
        if (choice) select_backend(choice);
    }
    platform_exit();
    return 0;
}
