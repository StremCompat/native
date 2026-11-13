/* The local server's /app API: JSON answers read on a worker thread and handed to the screens
   on the main thread, and the interface's texts in the viewer's language. */
#ifndef SC_API_H
#define SC_API_H

#include "json.h"

/* answer is NULL when the server did not answer or what came back was not JSON */
typedef void (*api_done)(const jval *answer, void *user);

void api_get(const char *path, api_done done, void *user);

/* the texts, asked once at the start (waiting for the server, which is on this TV) */
int api_load_strings(void);
/* the text for a key, in the interface's language, else in English, else the key itself */
const char *tx(const char *key);

#endif
