/* The addons of the account, as the web interface shows them (screens.css #aWrap), with three
   buttons on top: the store, adding one by its address, and syncing. OK twice on an installed
   addon removes it. The store (server/addon-store.js) shows the community addons not installed
   yet, most popular first, as cards three to a row with a small picture and the description; one
   that needs setting up shows a QR code for the phone. The changes are made on the account, so
   the person's other devices get them too. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "api.h"
#include "img.h"
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
#define MAX_STORE 300
#define BUTTONS 3
#define KINDS 5
#define STORE_GAP 24.0f
#define STORE_CARD_H 560.0f
#define PIC_H 190.0f

typedef struct {
  char name[96], info[260], url[400];
} addon;

typedef struct {
  char *name, *meta, *desc, *url, *picture, *configure;
  int cover; /* the picture is a background, cut to fill */
} store_card;

static const char *kind_ids[KINDS] = {"all", "streams", "channels", "subtitles", "catalogs"};
static addon mine[MAX_ADDONS];
static int nmine, loaded, in_store, armed = -1;
static int at; /* 0..2 the buttons, then the installed addons */
static float scroll;

static store_card cards[MAX_STORE];
static int ncards, store_more, store_loading, store_gen, kind, card_at, on_chips;
static float store_scroll;
static sc_tex app_logo;
static char *qr;
static int qr_size;
static char qr_title[96], qr_url[400];

static void listed(const jval *answer, void *user) {
  (void)user;
  const jval *list = json_get(answer, "addons");
  nmine = 0;
  for (int i = 0; i < json_len(list) && nmine < MAX_ADDONS; i++, nmine++) {
    const jval *a = json_at(list, i);
    snprintf(mine[nmine].name, sizeof mine[0].name, "%s", json_str(json_get(a, "name"), ""));
    snprintf(mine[nmine].url, sizeof mine[0].url, "%s", json_str(json_get(a, "url"), ""));
    snprintf(mine[nmine].info, sizeof mine[0].info, "%s", json_str(json_get(a, "info"), ""));
  }
  loaded = 1;
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

/* ---------- the store ---------- */
static char *copy(const char *s) { return s && *s ? str_dup(s) : NULL; }

static void clear_cards(void) {
  for (int i = 0; i < ncards; i++) {
    store_card *c = &cards[i];
    free(c->name), free(c->meta), free(c->desc), free(c->url), free(c->picture), free(c->configure);
  }
  ncards = card_at = 0;
  store_scroll = 0;
}

static void store_came(const jval *answer, void *user) {
  if ((intptr_t)user != store_gen) return;
  store_loading = 0;
  const jval *list = json_get(answer, "addons");
  for (int i = 0; i < json_len(list) && ncards < MAX_STORE; i++) {
    const jval *a = json_at(list, i);
    store_card *c = &cards[ncards++];
    const char *bg = json_str(json_get(a, "background"), ""), *logo = json_str(json_get(a, "logo"), "");
    sbuf meta = {0};
    const char *version = json_str(json_get(a, "version"), "");
    if (*version) sb_addf(&meta, "v%s   ", version);
    const jval *types = json_get(a, "types");
    for (int t = 0; t < json_len(types); t++) sb_addf(&meta, "%s%s", t ? " \xC2\xB7 " : "", json_str(json_at(types, t), ""));
    c->name = copy(json_str(json_get(a, "name"), ""));
    c->meta = meta.s ? meta.s : NULL;
    c->desc = copy(json_str(json_get(a, "description"), ""));
    c->url = copy(json_str(json_get(a, "url"), ""));
    c->picture = copy(*bg ? bg : logo);
    c->cover = *bg != 0;
    c->configure = copy(json_str(json_get(a, "configure"), ""));
  }
  store_more = json_bool(json_get(answer, "more"), 0);
}

/* the installed addons are left out by the server, which knows the account */
static void load_store(int next) {
  if (!next) clear_cards();
  store_gen++;
  store_loading = 1;
  char path[96];
  snprintf(path, sizeof path, "/addons/store?type=%s&skip=%d", kind_ids[kind], ncards);
  api_get(path, store_came, (void *)(intptr_t)store_gen);
}

static void qr_came(const jval *answer, void *user) {
  (void)user;
  const jval *rows = json_get(answer, "qr");
  qr_size = json_len(rows);
  free(qr);
  qr = qr_size ? malloc((size_t)qr_size * (size_t)qr_size + 1) : NULL;
  for (int y = 0; qr && y < qr_size; y++) {
    const char *row = json_str(json_at(rows, y), "");
    for (int x = 0; x < qr_size; x++) qr[y * qr_size + x] = x < (int)strlen(row) ? row[x] : '0';
  }
}

static void show_qr(const store_card *c) {
  snprintf(qr_title, sizeof qr_title, "%s", c->name ? c->name : "");
  snprintf(qr_url, sizeof qr_url, "%s", c->configure);
  sbuf p = {0};
  sb_add(&p, "/addons/qr?u=");
  sb_add_url(&p, c->configure);
  api_get(p.s, qr_came, NULL);
  sb_free(&p);
}

static void close_qr(void) {
  free(qr);
  qr = NULL;
  qr_size = 0;
}

static void store_key(app_key k) {
  if (qr) return close_qr();
  if (k == K_OK) {
    if (on_chips) {
      on_chips = 0;
      return load_store(0);
    }
    if (card_at >= ncards) return;
    store_card *c = &cards[card_at];
    if (c->configure) return show_qr(c);
    change(c->url, 0);
    return;
  }
  if (k == K_BACK) {
    in_store = 0;
    at = 0;
    return;
  }
  if (on_chips) {
    if (k == K_LEFT && kind > 0) kind--;
    else if (k == K_LEFT) nav_focus(NAV_ADDONS);
    else if (k == K_RIGHT && kind < KINDS - 1) kind++;
    else if (k == K_DOWN && ncards) on_chips = 0;
    return;
  }
  if (k == K_RIGHT && card_at < ncards - 1) card_at++;
  else if (k == K_LEFT && card_at % 3) card_at--;
  else if (k == K_LEFT) nav_focus(NAV_ADDONS);
  else if (k == K_DOWN && card_at + 3 < ncards) card_at += 3;
  else if (k == K_DOWN) card_at = ncards - 1;
  else if (k == K_UP && card_at >= 3) card_at -= 3;
  else if (k == K_UP) on_chips = 1;
  /* the next page comes before the end */
  if (store_more && !store_loading && card_at >= ncards - 6) load_store(1);
}

static void activate(void) {
  if (at == 0) {
    in_store = 1;
    kind = 0;
    on_chips = 0;
    return load_store(0);
  }
  if (at == 1) return addr_open(tx("addAddon"), ADDR_URL, added_by_address);
  if (at == 2) return sync_now(0);
  int i = at - BUTTONS;
  if (i < 0 || i >= nmine) return;
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
  if (in_store) return store_key(k);
  int top = at < BUTTONS, last = BUTTONS + nmine - 1;
  if (k == K_DOWN) at = top ? (nmine ? BUTTONS : at) : (at < last ? at + 1 : at);
  else if (k == K_UP) at = at > BUTTONS ? at - 1 : at == BUTTONS ? 0 : at;
  else if (k == K_RIGHT && top && at < BUTTONS - 1) at++;
  else if (k == K_LEFT && top && at > 0) at--;
  else if (k == K_LEFT) nav_focus(NAV_ADDONS);
  else if (k == K_BACK) app_show(&home_screen);
  else if (k == K_OK) return activate();
  if (armed != at) armed = -1;
}

static void text(const char *utf8) {
  if (addr_active()) addr_text(utf8);
}

/* ---------- drawing ---------- */
static void draw_card(const store_card *c, float x, float y, float w, int focused) {
  gfx_rect(x, y, w, STORE_CARD_H, 16, focused ? C_TEXT(0.10f) : C_TEXT(0.05f));
  gfx_push_clip(x, y, w, PIC_H);
  gfx_rect(x, y, w, PIC_H, 16, SC_RGBA(27, 27, 30, 255));
  const sc_tex *pic = c->picture ? img_get(c->picture, c->cover ? 480 : 240, NULL) : NULL;
  int failed = c->picture && img_failed(c->picture, c->cover ? 480 : 240);
  if (pic && c->cover) gfx_image(pic, x, y, w, PIC_H, 0, 1, COVER);
  else {
    /* a logo, or the app's own when the addon has no picture */
    const sc_tex *logo = pic ? pic : (!c->picture || failed) && app_logo.id ? &app_logo : NULL;
    if (logo) {
      float s = (w * 0.7f) / logo->w < 150.0f / logo->h ? (w * 0.7f) / logo->w : 150.0f / logo->h;
      gfx_image(logo, x + (w - logo->w * s) / 2, y + (PIC_H - logo->h * s) / 2, logo->w * s, logo->h * s, 0, 1, STRETCH);
    }
  }
  gfx_pop_clip();
  float tx0 = x + 22, tw = w - 44, ty = y + PIC_H + 18;
  text_draw_fit(c->name ? c->name : "", tx0, ty, tw, 26, W_EXTRABOLD, C_WHITE);
  if (c->meta) text_draw_fit(c->meta, tx0, ty + 36, tw, 17, W_REGULAR, C_TEXT(0.5f));
  if (c->desc) text_draw_wrap(c->desc, tx0, ty + 66, tw, 18, W_REGULAR, C_TEXT(0.78f), 7, 25);
  const char *label = tx(c->configure ? "storeConfigure" : "installAddon");
  float bw = text_width(label, 19, W_BOLD) + 48;
  gfx_rect(tx0, y + STORE_CARD_H - 62, bw, 44, 22, focused ? C_WHITE : C_TEXT(0.12f));
  ui_line(label, tx0 + 24, y + STORE_CARD_H - 62, 44, 19, W_BOLD, focused ? C_BG : C_WHITE);
  if (focused) gfx_outline(x - 4, y - 4, w + 8, STORE_CARD_H + 8, 19, 4, C_WHITE);
}

static void draw_store(float dt, int nav) {
  float y0 = TOP + 132, x = LEFT;
  for (int i = 0; i < KINDS; i++) {
    char key[32];
    snprintf(key, sizeof key, "store_%s", kind_ids[i]);
    const char *label = tx(key);
    float w = text_width(label, 22, W_SEMIBOLD) + 44;
    int f = on_chips && i == kind && !nav, cur = i == kind;
    gfx_rect(x, y0, w, 50, 25, f ? C_WHITE : C_TEXT(cur ? 0.18f : 0.08f));
    ui_line(label, x + 22, y0, 50, 22, W_SEMIBOLD, f ? C_BG : C_WHITE);
    x += w + 12;
  }
  float gy = y0 + 76, w = (SC_DESIGN_W - LEFT - 48 - 2 * STORE_GAP) / 3;
  int row = card_at / 3;
  store_scroll = ui_approach(store_scroll, row * (STORE_CARD_H + STORE_GAP), dt, 50);
  gfx_push_clip(LEFT - 8, gy - 8, SC_DESIGN_W - LEFT, SC_DESIGN_H - gy + 8);
  for (int i = 0; i < ncards; i++) {
    float cy = gy + (i / 3) * (STORE_CARD_H + STORE_GAP) - store_scroll;
    if (cy + STORE_CARD_H < gy - 8 || cy > SC_DESIGN_H) continue;
    draw_card(&cards[i], LEFT + (i % 3) * (w + STORE_GAP), cy, w, i == card_at && !on_chips && !nav);
  }
  gfx_pop_clip();
  if (!ncards) text_draw(tx(store_loading ? "searching" : "storeUnavailable"), LEFT, gy + 10, 26, W_REGULAR, C_TEXT(0.72f));
}

static void draw_qr(void) {
  if (!qr) return;
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(6, 6, 7, 235));
  float w = 760, x = (SC_DESIGN_W - w) / 2, y = 150;
  gfx_rect(x, y, w, 660, 22, SC_RGBA(21, 21, 23, 255));
  float tw = text_width(qr_title, 32, W_EXTRABOLD);
  text_draw_fit(qr_title, tw < w - 80 ? x + (w - tw) / 2 : x + 40, y + 40, w - 80, 32, W_EXTRABOLD, C_WHITE);
  ui_qr(qr, qr_size, x + (w - 320) / 2, y + 110, 320);
  text_draw_wrap(tx("storeConfigureHint"), x + 50, y + 460, w - 100, 21, W_REGULAR, C_TEXT(0.8f), 3, 30);
  text_draw_fit(qr_url, x + 50, y + 590, w - 100, 16, W_REGULAR, C_TEXT(0.45f));
}

static void frame(double t, float dt) {
  (void)t;
  int nav = nav_has_focus() || addr_active();
  text_draw(tx(in_store ? "addonStore" : "addonsGroup"), LEFT, TOP, 50, W_EXTRABOLD, C_WHITE);
  text_draw_fit(tx("addonsHint"), LEFT, TOP + 69, SC_DESIGN_W - LEFT - 48 - 760, 24, W_REGULAR, C_TEXT(0.72f));
  if (in_store) {
    draw_store(dt, nav);
    nav_draw(NAV_ADDONS, dt);
    draw_qr();
    return;
  }
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
  gfx_rect(LEFT, TOP + 112, SC_DESIGN_W - LEFT - 48, 1, 0, C_TEXT(0.10f));
  int row = at - BUTTONS;
  float target = row > 4 ? (row - 4) * (ITEM_H + 9) : 0;
  scroll = ui_approach(scroll, target, dt, 50);
  gfx_push_clip(LEFT - 4, LIST_Y - 4, SC_DESIGN_W - LEFT, SC_DESIGN_H - 40 - LIST_Y);
  for (int i = 0; i < nmine; i++) {
    float y = LIST_Y + i * (ITEM_H + 9) - scroll;
    if (y + ITEM_H < LIST_Y - 4 || y > SC_DESIGN_H) continue;
    int f = at == i + BUTTONS && !nav;
    gfx_rect(LEFT, y, 1080, ITEM_H, 13, f ? C_TEXT(0.16f) : C_TEXT(0.045f));
    if (f) gfx_outline(LEFT, y, 1080, ITEM_H, 13, 3, C_WHITE);
    text_draw_fit(mine[i].name, LEFT + 25, y + 18, 1030, 29, W_BOLD, C_WHITE);
    text_draw_fit(mine[i].info, LEFT + 25, y + 18 + 42, 1030, 21, W_REGULAR, C_TEXT(0.6f));
  }
  if (loaded && !nmine) text_draw(tx("noAddons"), LEFT, LIST_Y, 26, W_REGULAR, C_TEXT(0.72f));
  gfx_pop_clip();
  nav_draw(NAV_ADDONS, dt);
  addr_draw();
}

static void enter(void) {
  at = 0;
  in_store = 0;
  armed = -1;
  close_qr();
  if (!app_logo.id) app_logo = img_load_file(app_asset_dir("logo.png"), 0);
  api_get("/app/addons", listed, NULL);
}

const screen addons_screen = {enter, key, frame, text, 0};
