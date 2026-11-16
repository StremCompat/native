/* The webOS Luna bus, shared by the player and by starting the app's own service. */
#ifndef SC_WEBOS_LUNA_H
#define SC_WEBOS_LUNA_H

#include <luna-service2/lunaservice.h>

/* the app's ID, given by build.sh (the native app sits next to the web one, with its own) */
#ifndef SC_APP_ID
#define SC_APP_ID "io.github.alexesmm.stremcompat.native"
#endif
#define SC_SERVICE_ID SC_APP_ID ".server"

/* registered and attached to the main loop; 0 when the bus refused (luna_error says why) */
int luna_ready(void);
const char *luna_error(void);
/* the name the app took on the bus, for the reports */
const char *luna_name(void);
/* a call; with reply NULL one answer is asked and ignored */
int luna_call(const char *uri, const char *payload, LSFilterFunc reply, LSMessageToken *token);
void luna_cancel(LSMessageToken token);
/* the bus's news, once per frame */
void luna_poll(void);

#endif
