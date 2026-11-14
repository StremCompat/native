#include "nav.h"

#include <stdio.h>

#include "api.h"
#include "screens.h"
#include "gfx.h"
#include "text.h"
#include "ui.h"
#include "util.h"

/* nav.css: chips of 60 x 56 at x 34, centred, leave at the bottom */
#define CHIP_X 34.0f
#define CHIP_W 60.0f
#define CHIP_H 56.0f
#define CHIP_STEP 64.0f
#define RAIL_W 128.0f
#define PANEL_W 480.0f

static const struct {
  icon_id icon;
  const char *label;
} items[NAV_COUNT] = {
    {ICON_SEARCH, "navSearch"},     {ICON_HOME, "navHome"},         {ICON_DISCOVER, "navDiscover"},
    {ICON_LIBRARY, "navLibrary"},   {ICON_CALENDAR, "navCalendar"}, {ICON_CHANNELS, "navChannels"},
    {ICON_ADDONS, "navAddons"},     {ICON_SETTINGS, "navSettings"}, {ICON_UPDATE, "updateNav"},
    {ICON_LEAVE, "exitLeave"},
};

static int focused = -1; /* the chip with the focus, or -1 while the screen has it */
static char update_version[32];
static int calendar_shown = 1;

void nav_offer_update(const char *version) { snprintf(update_version, sizeof update_version, "%s", version); }
void nav_show_calendar(int shown) { calendar_shown = shown; }

/* Update only when a newer version is; the Calendar only for Stremio accounts */
static int present(int i) { return (i != NAV_UPDATE || update_version[0]) && (i != NAV_CALENDAR || calendar_shown); }
static float open;      /* 0..1: the panel and the names fading in */

void nav_focus(nav_item current) { focused = (int)current; }

int nav_has_focus(void) { return focused >= 0; }

int nav_key(app_key k) {
  if (k == K_UP && focused > 0) focused -= present(focused - 1) ? 1 : 2;
  else if (k == K_DOWN && focused < NAV_COUNT - 1) focused += present(focused + 1) ? 1 : 2;
  else if (k == K_RIGHT || k == K_BACK) focused = -1;
  else if (k == K_OK) {
    int chosen = focused;
    focused = -1;
    /* the sections are the screens at the bottom of the stack */
    if (chosen == NAV_LEAVE) app_quit();
    else if (chosen == NAV_HOME) app_show(&home_screen);
    else if (chosen == NAV_SEARCH) app_show(&search_screen);
    else if (chosen == NAV_LIBRARY) app_show(&library_screen);
    else if (chosen == NAV_DISCOVER) app_show(&discover_screen);
    else if (chosen == NAV_SETTINGS) app_show(&settings_screen);
    else if (chosen == NAV_ADDONS) app_show(&addons_screen);
    else if (chosen == NAV_CALENDAR) app_show(&calendar_screen);
    else if (chosen == NAV_CHANNELS) app_show(&channels_screen);
    else if (chosen == NAV_UPDATE) update_start();
    return chosen;
  }
  return -1;
}

/* the sections centred, closing up where one is not shown (the Calendar for Nuvio) */
static float chip_y(int i) {
  if (i == NAV_LEAVE) return SC_DESIGN_H - 34 - CHIP_H;
  if (i == NAV_UPDATE) return SC_DESIGN_H - 98 - CHIP_H;
  int shown = 0, at = 0;
  for (int s = 0; s < NAV_UPDATE; s++)
    if (present(s)) {
      if (s < i) at++;
      shown++;
    }
  return (SC_DESIGN_H - (shown * CHIP_STEP)) / 2 + 4 + at * CHIP_STEP;
}

void nav_draw(nav_item current, float dt) {
  open = ui_approach(open, focused >= 0 ? 1.0f : 0.0f, dt, 60);
  /* the rail's own shade, and the darker panel behind the names while it has the focus */
  gfx_gradient(0, 0, RAIL_W * 0.7f, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 235), SC_RGBA(10, 10, 11, 153), 0);
  gfx_gradient(RAIL_W * 0.7f, 0, RAIL_W * 0.3f, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 153), SC_RGBA(10, 10, 11, 0), 0);
  if (open > 0) {
    gfx_push_opacity(open);
    gfx_rect(0, 0, PANEL_W * 0.62f, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 250));
    gfx_gradient(PANEL_W * 0.62f, 0, PANEL_W * 0.38f, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 250), SC_RGBA(10, 10, 11, 0), 0);
    gfx_pop_opacity();
  }
  /* the profile at the top */
  gfx_outline(38, 38, 52, 52, 26, 3, C_TEXT(0.18f));
  for (int i = 0; i < NAV_COUNT; i++) {
    if (!present(i)) continue;
    float y = chip_y(i);
    int is_focus = i == focused, is_current = i == (int)current;
    uint32_t chip = 0, icon = C_TEXT(0.6f);
    if (is_focus) chip = C_WHITE, icon = C_BG;
    else if (is_current && open > 0.5f) chip = C_TEXT(0.14f), icon = C_WHITE;
    else if (is_current) chip = C_WHITE, icon = C_BG;
    if (chip) gfx_rect(CHIP_X, y, CHIP_W, CHIP_H, 14, chip);
    ui_icon(items[i].icon, CHIP_X + (CHIP_W - 28) / 2, y + (CHIP_H - 28) / 2, 28, icon);
    if (open > 0) {
      gfx_push_opacity(open);
      gfx_push_offset(-10 * (1 - open), 0);
      text_draw(tx(items[i].label), CHIP_X + 84, y + CHIP_H / 2 - 17 + 1, 26, W_BOLD,
                is_focus || is_current ? C_WHITE : C_TEXT(0.72f));
      gfx_pop_offset();
      gfx_pop_opacity();
    }
  }
}
