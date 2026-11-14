/* The addons of the account, as the web interface shows them (screens.css #aWrap), with three
   buttons on top: the store (Stremio's list of community addons, OK installs one), adding one
   by its address, and syncing. OK twice on an installed addon removes it. The changes are made
   on the account (server/addon-store.js), so the person's other devices get them too. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "addr.h"
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
#define MAX_ADDONS 80
#define BUTTONS 3

typedef struct {
  char name[96], info[260], url[400];
} addon;

static addon mine[MAX_ADDONS], store[MAX_ADDONS];
static int nmine, nstore, loaded, store_loaded, in_store, armed = -1;
static int at; /* 0..2 the buttons, then the list (in the store, the list alone) */
static float scroll;

static int first(void) { return in_store ? 0 : BUTTONS; }
static int count(void) { return in_store ? nstore : nmine; }
static addon *item(int i) { return in_store ? &store[i] : &mine[i]; }

static void read_list(const jval *list, addon *out, int *n, int from_store) {
  *n = 0;
  for (int i = 0; i < json_len(list) && *n < MAX_ADDONS; i++, (*n)++) {
    const jval *a = json_at(list, i);
    snprintf(out[*n].name, sizeof out[0].name, "%s", json_str(json_get(a, "name"), ""));
    snprintf(out[*n].url, sizeof out[0].url, "%s", json_str(json_get(a, "url"), ""));
    snprintf(out[*n].info, sizeof out[0].info, "%s", json_str(json_get(a, from_store ? "description" : "info"), ""));
  }
}

static void listed(const jval *answer, void *user) {
  (void)user;
  read_list(json_get(answer, "addons"), mine, &nmine, 0);
  loaded = 1;
}

static void store_came(const jval *answer, void *user) {
  (void)user;
  read_list(json_get(answer, "addons"), store, &nstore, 1);
  store_loaded = 1;
}

static int installed(const char *url) {
  for (int i = 0; i < nmine; i++)
    if (!strcmp(mine[i].url, url)) return 1;
  return 0;
}

/* user: non-zero when the sync follows a change, which was already told */
static void synced(net_reply *r, void *user) {
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const char *message = json_str(json_get(json_root(d), "message"), "");
  if (*message && !user) ui_toast(message);
  home_reload();
  json_free(d);
  api_get("/app/addons", listed, NULL);
}

static void sync_now(int quiet) {
  if (!quiet) ui_toast(tx("syncing"));
  net_request("/app/settings/action", "{\"k\":\"__sync\"}", NET_HIGH, NULL, synced, quiet ? (void *)1 : NULL);
}

/* user: non-zero for a removal */
static void changed(net_reply *r, void *user) {
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  int ok = json_bool(json_get(json_root(d), "ok"), 0);
  json_free(d);
  ui_toast(tx(!ok ? "addonNotChanged" : user ? "addonRemoved" : "addonInstalled"));
  if (ok) sync_now(1);
}

/* on the account; the addons are synced again after it */
static void change(const char *url, int remove) {
  sbuf b = {0};
  sb_add(&b, "{\"url\":");
  sb_add_json_str(&b, url);
  sb_addf(&b, ",\"remove\":%s}", remove ? "true" : "false");
  ui_toast(tx("saving"));
  net_request("/addons/change", b.s, NET_HIGH, NULL, changed, remove ? (void *)1 : NULL);
  sb_free(&b);
}

/* typed, or pasted from the phone (addr.c) */
static void added_by_address(const char *url) { change(url, 0); }

static void activate(void) {
  if (!in_store && at == 0) {
    in_store = 1;
    at = 0;
    if (!store_loaded) api_get("/addons/store", store_came, NULL);
    return;
  }
  if (!in_store && at == 1) return addr_open(tx("addAddon"), ADDR_URL, added_by_address);
  if (!in_store && at == 2) return sync_now(0);
  int i = at - first();
  if (i < 0 || i >= count()) return;
  if (in_store) return change(store[i].url, 0);
  /* removing asks for a second OK */
  if (armed != at) {
    armed = at;
    ui_toast(tx("removeAddonConfirm"));
    return;
  }
  change(mine[i].url, 1);
  armed = -1;
}

static void key(app_key k) {
  if (addr_active()) return addr_key(k);
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  int top = !in_store && at < BUTTONS, last = first() + count() - 1;
  if (k == K_DOWN) at = top ? (count() ? BUTTONS : at) : (at < last ? at + 1 : at);
  else if (k == K_UP) at = at > first() ? (at == BUTTONS && !in_store ? 0 : at - 1) : at;
  else if (k == K_RIGHT && top && at < BUTTONS - 1) at++;
  else if (k == K_LEFT && top && at > 0) at--;
  else if (k == K_LEFT) nav_focus(NAV_ADDONS);
  else if (k == K_BACK && in_store) in_store = 0, at = 0;
  else if (k == K_BACK) app_show(&home_screen);
  else if (k == K_OK) return activate();
  if (armed != at) armed = -1;
}

static void text(const char *utf8) {
  if (addr_active()) addr_text(utf8);
}

static void frame(double t, float dt) {
  (void)t;
  int nav = nav_has_focus() || addr_active();
  text_draw(tx(in_store ? "addonStore" : "addonsGroup"), LEFT, TOP, 50, W_EXTRABOLD, C_WHITE);
  text_draw_fit(tx("addonsHint"), LEFT, TOP + 69, SC_DESIGN_W - LEFT - 48 - 760, 24, W_REGULAR, C_TEXT(0.72f));
  if (!in_store) {
    const char *labels[BUTTONS] = {tx("addonStore"), tx("addAddon"), tx("syncShort")};
    float x = SC_DESIGN_W - 48;
    for (int b = BUTTONS - 1; b >= 0; b--) {
      float w = text_width(labels[b], 25, W_BOLD) + 60;
      x -= w;
      int f = at == b && !nav;
      gfx_rect(x, TOP + 10, w, 62, 13, f ? C_WHITE : C_TEXT(0.14f));
      ui_line(labels[b], x + 30, TOP + 10, 62, 25, W_BOLD, f ? SC_RGBA(12, 11, 17, 255) : C_WHITE);
      x -= 14;
    }
  }
  gfx_rect(LEFT, TOP + 112, SC_DESIGN_W - LEFT - 48, 1, 0, C_TEXT(0.10f));
  int row = at - first();
  float target = row > 4 ? (row - 4) * (ITEM_H + 9) : 0;
  scroll = ui_approach(scroll, target, dt, 50);
  gfx_push_clip(LEFT - 4, LIST_Y - 4, SC_DESIGN_W - LEFT, SC_DESIGN_H - 40 - LIST_Y);
  for (int i = 0; i < count(); i++) {
    float y = LIST_Y + i * (ITEM_H + 9) - scroll;
    if (y + ITEM_H < LIST_Y - 4 || y > SC_DESIGN_H) continue;
    addon *a = item(i);
    int f = at == i + first() && !nav, have = in_store && installed(a->url);
    gfx_rect(LEFT, y, 1080, ITEM_H, 13, f ? C_TEXT(0.16f) : C_TEXT(0.045f));
    if (f) gfx_outline(LEFT, y, 1080, ITEM_H, 13, 3, C_WHITE);
    text_draw_fit(a->name, LEFT + 25, y + 18, 1030, 29, W_BOLD, C_WHITE);
    char line[320];
    snprintf(line, sizeof line, "%s%s%s", have ? tx("addonIsInstalled") : "", have ? "  \xC2\xB7  " : "", a->info);
    text_draw_fit(line, LEFT + 25, y + 18 + 42, 1030, 21, W_REGULAR, C_TEXT(0.6f));
  }
  if (in_store && store_loaded && !nstore) text_draw(tx("storeUnavailable"), LEFT, LIST_Y, 26, W_REGULAR, C_TEXT(0.72f));
  else if (in_store && !store_loaded) text_draw(tx("searching"), LEFT, LIST_Y, 26, W_REGULAR, C_TEXT(0.72f));
  else if (!in_store && loaded && !nmine) text_draw(tx("noAddons"), LEFT, LIST_Y, 26, W_REGULAR, C_TEXT(0.72f));
  gfx_pop_clip();
  nav_draw(NAV_ADDONS, dt);
  addr_draw();
}

static void enter(void) {
  at = 0;
  in_store = 0;
  armed = -1;
  api_get("/app/addons", listed, NULL);
}

const screen addons_screen = {enter, key, frame, text, 0};
