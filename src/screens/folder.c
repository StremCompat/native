/* A Nuvio Collections folder as Nuvio's apps open it, like the web page (folder.js): its cover on
   top, a tab per source (and "All" first, mixing them, when the folder asks for it) over a poster
   grid that keeps loading. A folder set to rows opens on Home's rows instead. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "img.h"
#include "nav.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TILE_W 264.0f
#define TILE_H 396.0f
#define STEP_X 288.0f
#define STEP_Y 420.0f
#define COLS 5
#define MAX_TABS 24
#define MAX_ITEMS 400
#define TAB_HIT 1000 /* the tabs' ids for the pointer, after the titles' */

static char folder_id[128], title[160], cover[512];
static char tab_names[MAX_TABS][160], tab_message[MAX_TABS][200];
static int tab_unavailable[MAX_TABS], ntabs, tab_at, on_tabs, gen;
static card items[MAX_ITEMS];
static int nitems, at, more, loading, loaded;
static float scroll;

static void clear_items(void) {
  for (int i = 0; i < nitems; i++) card_clear(&items[i]);
  nitems = at = 0;
  more = 0;
  scroll = 0;
}

static void page_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  loading = 0;
  const jval *list = json_get(answer, "items");
  for (int i = 0; i < json_len(list) && nitems < MAX_ITEMS; i++) card_read(&items[nitems++], json_at(list, i));
  more = json_bool(json_get(answer, "more"), 0);
}

static void load_page(int next) {
  if (!next) clear_items();
  gen++;
  loading = 1;
  sbuf p = {0};
  sb_add(&p, "/app/folder/page?id=");
  sb_add_url(&p, folder_id);
  sb_addf(&p, "&tab=%d&skip=%d", tab_at, nitems);
  api_get(p.s, page_came, (void *)(intptr_t)gen);
  sb_free(&p);
}

static void folder_came(const jval *answer, void *user) {
  (void)user;
  snprintf(title, sizeof title, "%s", json_str(json_get(answer, "title"), ""));
  snprintf(cover, sizeof cover, "%s", json_str(json_get(answer, "cover"), ""));
  const jval *tabs = json_get(answer, "tabs");
  ntabs = 0;
  for (int i = 0; i < json_len(tabs) && ntabs < MAX_TABS; i++, ntabs++) {
    snprintf(tab_names[ntabs], sizeof tab_names[0], "%s", json_str(json_get(json_at(tabs, i), "title"), ""));
    tab_unavailable[ntabs] = json_bool(json_get(json_at(tabs, i), "unavailable"), 0);
    snprintf(tab_message[ntabs], sizeof tab_message[0], "%s", json_str(json_get(json_at(tabs, i), "message"), ""));
  }
  loaded = 1;
  load_page(0);
}

void folder_open(const char *id) {
  snprintf(folder_id, sizeof folder_id, "%s", id ? id : "");
  app_push(&folder_screen);
}

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (k == K_BACK) return app_back();
  if (on_tabs) {
    int before = tab_at;
    if (k == K_LEFT && tab_at > 0) tab_at--;
    else if (k == K_LEFT) nav_focus(NAV_HOME);
    else if (k == K_RIGHT && tab_at < ntabs - 1) tab_at++;
    else if ((k == K_DOWN || k == K_OK) && nitems) on_tabs = 0;
    if (before != tab_at) load_page(0);
    return;
  }
  if (k == K_OK && at < nitems && items[at].id) title_open(items[at].type, items[at].id);
  else if (k == K_RIGHT && at < nitems - 1) at++;
  else if (k == K_LEFT && at % COLS) at--;
  else if (k == K_LEFT) nav_focus(NAV_HOME);
  else if (k == K_DOWN && at + COLS < nitems) at += COLS;
  else if (k == K_UP && at >= COLS) at -= COLS;
  else if (k == K_UP) on_tabs = 1;
  /* the next page before the end */
  if (more && !loading && at >= nitems - 2 * COLS) load_page(1);
}

static void frame(double t, float dt) {
  (void)t;
  int nav = nav_has_focus();
  float top = 58;
  if (cover[0]) {
    const sc_tex *pic = img_get(cover, 1280, NULL);
    if (pic) {
      gfx_push_opacity(0.55f);
      gfx_image(pic, 128, 0, SC_DESIGN_W - 128, 300, 0, 1, COVER);
      gfx_pop_opacity();
      gfx_gradient(128, 150, SC_DESIGN_W - 128, 150, 0, SC_RGBA(10, 10, 11, 0), C_BG, 1);
    }
    top = 150;
  }
  text_draw_fit(title, LEFT, top, SC_DESIGN_W - LEFT - 48, 50, W_EXTRABOLD, C_WHITE);
  float x = LEFT, ty = top + 84;
  for (int i = 0; i < ntabs && x < SC_DESIGN_W - 100; i++) {
    float w = text_width(tab_names[i], 22, W_SEMIBOLD) + 44;
    int f = on_tabs && i == tab_at && !nav, cur = i == tab_at;
    gfx_rect(x, ty, w, 50, 25, f ? C_WHITE : C_TEXT(cur ? 0.18f : 0.08f));
    hit_add(TAB_HIT + i, x, ty, w, 50);
    ui_line(tab_names[i], x + 22, ty, 50, 22, W_SEMIBOLD, f ? C_BG : C_WHITE);
    x += w + 12;
  }
  float gy = ty + 90;
  int row = at / COLS;
  scroll = ui_approach(scroll, !on_tabs && row > 0 ? (row - 1) * STEP_Y : 0, dt, 60);
  gfx_push_clip(LEFT - 14, gy - 18, SC_DESIGN_W - LEFT, SC_DESIGN_H - gy + 18);
  for (int i = 0; i < nitems; i++) {
    float cx = LEFT + (i % COLS) * STEP_X, cy = gy + (i / COLS) * STEP_Y - scroll;
    if (cy + TILE_H < gy - 18 || cy > SC_DESIGN_H) continue;
    ui_tile(&items[i], cx, cy, TILE_W, TILE_H, !on_tabs && i == at && !nav);
    hit_add(i, cx, cy, TILE_W, TILE_H);
  }
  gfx_pop_clip();
  if (loaded && !nitems)
    text_draw(!loading && tab_message[tab_at][0] ? tab_message[tab_at]
              : tx(loading ? "searching" : tab_unavailable[tab_at] ? "folderSourceUnavailable" : "nothingFound"),
              LEFT, gy + 10, 26,
              W_REGULAR, C_TEXT(0.72f));
  nav_draw(NAV_HOME, dt);
}

static void enter(void) {
  clear_items();
  loaded = ntabs = tab_at = on_tabs = 0;
  title[0] = cover[0] = 0;
  sbuf p = {0};
  sb_add(&p, "/app/folder?id=");
  sb_add_url(&p, folder_id);
  api_get(p.s, folder_came, NULL);
  sb_free(&p);
}

/* the pointer: a click on a tab shows its lists; passing over a title of the same row takes the
   focus (another row would scroll the grid under it), a click opens it */
static void point(float x, float y, int press) {
  if (nav_point(x, y, press)) return;
  int id = hit_at(x, y);
  if (id < 0) return;
  if (nav_has_focus()) nav_key(K_RIGHT);
  if (id >= TAB_HIT) {
    if (!press) return;
    on_tabs = 1;
    if (tab_at != id - TAB_HIT) tab_at = id - TAB_HIT, load_page(0);
    return;
  }
  if (!press && (on_tabs || id / COLS != at / COLS)) return;
  on_tabs = 0;
  at = id;
  if (press) key(K_OK);
}

const screen folder_screen = {enter, key, frame, NULL, 0, point};
