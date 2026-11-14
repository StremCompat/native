/* The rail of icons on the left of the main screens (search, home, discover, library, calendar,
   channels, addons, settings, and leave at the bottom), with their names shown while it has the focus. */
#ifndef SC_NAV_H
#define SC_NAV_H

#include "app.h"

typedef enum {
  NAV_SEARCH,
  NAV_HOME,
  NAV_DISCOVER,
  NAV_LIBRARY,
  NAV_CALENDAR,
  NAV_CHANNELS,
  NAV_ADDONS,
  NAV_SETTINGS,
  NAV_UPDATE, /* only while a newer version exists */
  NAV_LEAVE,
  NAV_COUNT
} nav_item;

/* the rail takes the focus, on the screen it belongs to */
void nav_focus(nav_item current);
int nav_has_focus(void);
/* a key while the rail has the focus: Right or Back give the focus back to the screen; OK
   opens the section chosen (or leaves the app) and returns it, otherwise -1 */
int nav_key(app_key k);
void nav_draw(nav_item current, float dt);
/* a newer version was found: the update item shows above leaving */
void nav_offer_update(const char *version);
/* the Calendar is for Stremio accounts; a Nuvio one does not show it */
void nav_show_calendar(int shown);

#endif
