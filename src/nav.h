/* The rail of icons on the left of the main screens (search, home, discover, library, addons,
   settings, and leave at the bottom), with their names shown while it has the focus. */
#ifndef SC_NAV_H
#define SC_NAV_H

#include "app.h"

typedef enum { NAV_SEARCH, NAV_HOME, NAV_DISCOVER, NAV_LIBRARY, NAV_ADDONS, NAV_SETTINGS, NAV_LEAVE, NAV_COUNT } nav_item;

/* the rail takes the focus, on the screen it belongs to */
void nav_focus(nav_item current);
int nav_has_focus(void);
/* a key while the rail has the focus: Right or Back give the focus back to the screen; OK
   returns the item chosen, otherwise -1 */
int nav_key(app_key k);
void nav_draw(nav_item current, float dt);

#endif
