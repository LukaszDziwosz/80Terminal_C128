#ifndef T80_SESSION_H
#define T80_SESSION_H
#include "network.h"
#include "ui.h"
/* Resident: opens the connection, runs the terminal and shows the summary.
 * It stays loaded for the whole connection, whatever is in the UI slot. */
void session_run(const struct net_backend *backend, const struct connect_request *request);
#endif
