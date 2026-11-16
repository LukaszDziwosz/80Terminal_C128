#ifndef T80_IRCUI_H
#define T80_IRCUI_H
#include <stdint.h>
#include "network.h"

/* Entry point of the uiirc overlay (overlay.h): connects to the server in
 * irc_config (ircconfig.h, filled in by ui_irc_form) and runs the chat
 * screen until the user disconnects. The adapter is initialized and closed
 * on entry, and closed again on return. */
void irc_run(const struct net_backend *backend);
#endif
