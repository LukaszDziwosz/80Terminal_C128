#ifndef T80_UI_H
#define T80_UI_H
#include <stdint.h>
#include "network.h"
#include "telnet.h"

/* Filled by the UI overlay, then used by the resident session. */
struct connect_request {
    char name[25];      /* phonebook name, or empty for a manual connection */
    char hostname[65];
    uint16_t port;
    enum telnet_profile profile;
};
enum ui_action { UI_QUIT = 0, UI_CONNECT = 1, UI_PHONEBOOK = 2, UI_BACK = 3 };

/* Filled by the resident session; the UI shows it, then clears outcome. */
enum session_outcome {
    SESSION_NONE = 0,       /* cancelled before connecting: nothing to show */
    SESSION_CONNECT_FAILED, /* message: the adapter's reason */
    SESSION_ENDED           /* message: why; detail: adapter error, if any */
};
struct session_result {
    uint8_t outcome;
    uint16_t keys, sent;
    char message[80];
    char detail[80];
};
/* Where the resident session prints adapter status on the UI's
 * "Connecting" screen, inside its panel. */
#define SESSION_STATUS_ROW 10

/* Entry points of the uimain overlay (overlay.h): call overlay_use() first. */
/* Returns the chosen adapter, 1 RR-Net, 2 Ultimate, 3 WiC64, or 0 to quit. */
uint8_t ui_launcher(void);
/* Main screen: the last session's summary if there is one, interface
 * initialization and retry, and the connect form. Returns UI_CONNECT with
 * request filled in and the "Connecting" screen drawn, or UI_QUIT. */
uint8_t ui_main(const struct net_backend *backend, struct connect_request *request,
                struct session_result *result);
/* Forget the interface state so the next ui_main() initializes it again. */
void ui_reset(void);
/* For a request from the phonebook: waits for the adapter and draws the
 * "Connecting" screen. Returns 0 if the user cancelled. */
uint8_t ui_prepare_connect(const struct net_backend *backend, const struct connect_request *request);
#endif
