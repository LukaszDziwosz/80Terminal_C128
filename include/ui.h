#ifndef T80_UI_H
#define T80_UI_H
#include <stdint.h>
#include "network.h"
#include "telnet.h"

/* Filled by the UI overlay, then used by the resident session. */
struct connect_request {
    char hostname[65];
    uint16_t port;
    enum telnet_profile profile;
};
enum ui_action { UI_QUIT = 0, UI_CONNECT = 1 };

/* Entry points of the uimain overlay (overlay.h): call overlay_use() first. */
/* Returns the chosen adapter, 1 RR-Net, 2 Ultimate, 3 WiC64, or 0 to quit. */
uint8_t ui_launcher(void);
/* Main screen: interface initialization and retry, and the
 * connect form. Returns UI_CONNECT with request filled in, or UI_QUIT. */
uint8_t ui_main(const struct net_backend *backend, struct connect_request *request);
/* Forget the interface state so the next ui_main() initializes it again. */
void ui_reset(void);
#endif
