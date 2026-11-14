/* The webOS Luna bus, shared by the player and by starting the app's own service. */
#ifndef SC_WEBOS_LUNA_H
#define SC_WEBOS_LUNA_H

#include <luna-service2/lunaservice.h>

/* registered and attached to the main loop; 0 when the bus refused (luna_error says why) */
int luna_ready(void);
const char *luna_error(void);
/* a call; with reply NULL one answer is asked and ignored */
int luna_call(const char *uri, const char *payload, LSFilterFunc reply, LSMessageToken *token);
void luna_cancel(LSMessageToken token);
/* the bus's news, once per frame */
void luna_poll(void);

#endif
