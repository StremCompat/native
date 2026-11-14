/* The addons of the account, as the web interface shows them (screens.css #aWrap): read only,
   since the account decides them, with a button to fetch them again from it. */
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "nav.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TOP 58.0f
#define LIST_Y (TOP + 128)
#define ITEM_H 108.0f
#define MAX_ADDONS 64

static char names[MAX_ADDONS][96], infos[MAX_ADDONS][200];
static int count, loaded, at; /* at 0 is the sync button, then the addons */
static float scroll;

static void listed(const jval *answer, void *user) {
  (void)user;
  const jval *list = json_get(answer, "addons");
  count = 0;
  for (int i = 0; i < json_len(list) && count < MAX_ADDONS; i++, count++) {
    snprintf(names[count], sizeof names[0], "%s", json_str(json_get(json_at(list, i), "name"), ""));
    snprintf(infos[count], sizeof infos[0], "%s", json_str(json_get(json_at(list, i), "info"), ""));
  }
  loaded = 1;
}

static void synced(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const char *message = json_str(json_get(json_root(d), "message"), "");
  if (*message) ui_toast(message);
  if (json_bool(json_get(json_root(d), "changed"), 0)) home_reload();
  json_free(d);
  api_get("/app/addons", listed, NULL);
}

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (k == K_DOWN && at < count) at++;
  else if (k == K_UP && at > 0) at--;
  else if (k == K_LEFT) nav_focus(NAV_ADDONS);
  else if (k == K_BACK) app_show(&home_screen);
  else if (k == K_OK && at == 0) {
    ui_toast(tx("syncing"));
    net_request("/app/settings/action", "{\"k\":\"__sync\"}", NET_HIGH, NULL, synced, NULL);
  }
}

static void frame(double t, float dt) {
  (void)t;
  text_draw(tx("addonsGroup"), LEFT, TOP, 50, W_EXTRABOLD, C_WHITE);
  text_draw_fit(tx("addonsHelp"), LEFT, TOP + 69, SC_DESIGN_W - LEFT - 48 - 260, 24, W_REGULAR, C_TEXT(0.72f));
  const char *sync = tx("syncShort");
  float sw = text_width(sync, 25, W_BOLD) + 60, sx = SC_DESIGN_W - 48 - sw;
  int sf = at == 0 && !nav_has_focus();
  gfx_rect(sx, TOP + 10, sw, 62, 13, sf ? C_WHITE : C_TEXT(0.14f));
  ui_line(sync, sx + 30, TOP + 10, 62, 25, W_BOLD, sf ? SC_RGBA(12, 11, 17, 255) : C_WHITE);
  gfx_rect(LEFT, TOP + 112, SC_DESIGN_W - LEFT - 48, 1, 0, C_TEXT(0.10f));
  float target = at > 4 ? (at - 4) * (ITEM_H + 9) : 0;
  scroll = ui_approach(scroll, target, dt, 50);
  gfx_push_clip(LEFT - 4, LIST_Y - 4, SC_DESIGN_W - LEFT, SC_DESIGN_H - 40 - LIST_Y);
  for (int i = 0; i < count; i++) {
    float y = LIST_Y + i * (ITEM_H + 9) - scroll;
    int f = at == i + 1 && !nav_has_focus();
    gfx_rect(LEFT, y, 1080, ITEM_H, 13, f ? C_TEXT(0.16f) : C_TEXT(0.045f));
    if (f) gfx_outline(LEFT, y, 1080, ITEM_H, 13, 3, C_WHITE);
    text_draw_fit(names[i], LEFT + 25, y + 18, 1030, 29, W_BOLD, C_WHITE);
    text_draw_fit(infos[i], LEFT + 25, y + 18 + 42, 1030, 21, W_REGULAR, C_TEXT(0.6f));
  }
  if (loaded && !count) text_draw(tx("noAddons"), LEFT, LIST_Y, 26, W_REGULAR, C_TEXT(0.72f));
  gfx_pop_clip();
  nav_draw(NAV_ADDONS, dt);
}

static void enter(void) {
  at = 0;
  api_get("/app/addons", listed, NULL);
}

const screen addons_screen = {enter, key, frame, NULL};
