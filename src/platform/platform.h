/* What differs from one TV system to another; one file per system under platform/. */
#ifndef SC_PLATFORM_H
#define SC_PLATFORM_H

#include <SDL.h>

#include "app.h"

/* before SDL starts: the system's own settings for the window and the remote */
void platform_init(void);

/* the system's own remote buttons (Back, colours...); K_NONE for the usual keys, which the
   app reads the same way everywhere */
app_key platform_key(const SDL_Event *ev);

/* starts the app's own local server where the system runs it as a service (webOS) */
void platform_start_server(void);

/* offers the system's fonts to the text module, best candidates for CJK first */
void platform_add_fonts(void);

#endif
