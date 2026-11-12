#ifndef T80_SESSION_H
#define T80_SESSION_H
#include "network.h"
#include "ui.h"
/* The uiterm overlay's entry point (overlay.h): opens the connection and
 * runs the terminal, then fills result for the UI's summary. */
void session_run(const struct net_backend *backend, const struct connect_request *request,
                 struct session_result *result);
#endif
