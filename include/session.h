#ifndef T80_SESSION_H
#define T80_SESSION_H
#include "network.h"
#include "ui.h"
/* The uiterm overlay's entry point (overlay.h): opens the connection and
 * runs the terminal, then fills result for the UI's summary. */
/* With resuming set, returns to the terminal after a transfer (request is
 * not used). Ends with result->outcome SESSION_TRANSFER when the user
 * pressed HELP: the connection stays open for the transfer overlay. */
void session_run(const struct net_backend *backend, const struct connect_request *request,
                 struct session_result *result, uint8_t resuming);
/* Resident, 1 KB: the session's transmit queue, and the block buffer of
 * a transfer (the queue is empty then). */
extern uint8_t session_buffer[1024];
#endif
