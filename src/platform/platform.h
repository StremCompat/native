/* What differs from one TV system to another; one file per system under platform/. */
#ifndef SC_PLATFORM_H
#define SC_PLATFORM_H

#include <SDL.h>

#include "app.h"

/* before SDL starts: the system's own settings for the window and the remote */
void platform_init(void);
/* the window: a TV takes the whole screen, a computer a window of its own */
Uint32 platform_window_flags(void);

/* the system's own remote buttons (Back, colours...); K_NONE for the usual keys, which the
   app reads the same way everywhere */
app_key platform_key(const SDL_Event *ev);

/* once per frame: the system's news (webOS bus answers) */
void platform_poll(void);

/* the TV's language as "pt-BR", or "" when the system does not say */
void platform_language(char *out, int size);

/* starts the app's own local server where the system runs it as a service (webOS) */
void platform_start_server(void);
/* starts the local server from the app itself, when the system's service did not answer
   (webOS: a native app cannot always reach its own service) */
void platform_run_server(void);
/* what the service said to that so far, for the start screen's report ("" where there is none) */
const char *platform_server_status(void);

/* hands the downloaded update to the system installer; done(error) with "" when it was taken */
void platform_install_update(void (*done)(const char *error));

/* why the app cannot go on, shown where the person sees it (a TV notification on webOS) */
void platform_fatal(const char *message);

/* offers the system's fonts to the text module, best candidates for CJK first */
void platform_add_fonts(void);

#endif
