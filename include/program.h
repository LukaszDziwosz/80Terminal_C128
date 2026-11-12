#ifndef T80_PROGRAM_H
#define T80_PROGRAM_H
#include "network.h"
/* Resident application loop: the uimain overlay's main screen alternates
 * with resident sessions until the user quits. */
void program_run(const struct net_backend *backend);
/* Full-screen error, then waits for a key before exiting to BASIC. */
void program_message(const char *text);
#endif
