#ifndef T80_TRANSFER_H
#define T80_TRANSFER_H
#include "network.h"
#include "ui.h"
/* Entry point of the uixfer overlay (overlay.h): the transfer screens,
 * while the session is suspended and the connection stays open. Returns
 * when the user goes back to the terminal. */
void transfer_run(const struct net_backend *backend, const struct connect_request *request);
#endif
