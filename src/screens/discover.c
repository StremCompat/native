/* Discover: three filters (type, catalog, genre) over a grid of titles that loads more as the
   focus nears its end. The server knows the catalogs and pages (/app/discover); measures are
   the web interface's (screens.css #discWrap). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "nav.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TOP 48.0f
#define GRID_Y (TOP + 142 + 18)
#define TILE_W 264.0f
#define TILE_H 396.0f
#define STEP_X 288.0f
#define STEP_Y 420.0f
#define COLS 5
#define MAX_ITEMS 500 /* the most Settings offers; the chosen one comes with each page */
#define MAX_OPTIONS 64
#define FILTER_HIT 1000 /* the filters' ids for the pointer, after the titles' */

enum { F_TYPE, F_CATALOG, F_GENRE, F_COUNT };

static char options[F_COUNT][MAX_OPTIONS][96];
static int noptions[F_COUNT], chosen[F_COUNT];
static card items[MAX_ITEMS];
static int nitems, ended, loading, gen, at, in_grid, filter, started;
static menu picker;
static float scroll;

static void clear_items(void) {
  for (int i = 0; i < nitems; i++) card_clear(&items[i]);
  nitems = 0;
}

static void read_options(int f, const jval *list) {
  noptions[f] = 0;
  for (int i = 0; i < json_len(list) && i < MAX_OPTIONS; i++)
    snprintf(options[f][noptions[f]++], sizeof options[0][0], "%s", json_str(json_get(json_at(list, i), "label"), ""));
}

static void page_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  loading = 0;
  if (!answer) return;
  read_options(F_TYPE, json_get(answer, "types"));
  read_options(F_CATALOG, json_get(answer, "catalogs"));
  read_options(F_GENRE, json_get(answer, "genres"));
  const jval *list = json_get(answer, "items");
  int cap = (int)json_num(json_get(answer, "max"), 200);
  if (cap < 1 || cap > MAX_ITEMS) cap = MAX_ITEMS;
  for (int i = 0; i < json_len(list) && nitems < cap; i++) {
    const jval *v = json_at(list, i);
    const char *id = json_str(json_get(v, "id"), "");
    int seen = 0;
    for (int k = 0; k < nitems && !seen; k++) seen = items[k].id && !strcmp(items[k].id, id);
    if (!seen) card_read(&items[nitems++], v);
  }
  ended = json_bool(json_get(answer, "ended"), 0) || nitems >= cap;
}

static void ask(void) {
  if (loading || ended) return;
  loading = 1;
  char path[160];
  snprintf(path, sizeof path, "/app/discover?type=%d&catalog=%d&genre=%d&skip=%d", chosen[F_TYPE], chosen[F_CATALOG],
           chosen[F_GENRE], nitems);
  api_get(path, page_came, (void *)(intptr_t)gen);
}

/* a filter changed: the grid starts again */
static void restart(void) {
  gen++;
  clear_items();
  ended = loading = 0;
  at = 0;
  scroll = 0;
  ask();
}

static void enter(void) {
  if (!started) {
    started = 1;
    restart();
  }
}

/* an option of the open filter chosen */
static void pick(int id) {
  chosen[filter] = id;
  /* a new type has other catalogs, a new catalog other genres */
  if (filter == F_TYPE) chosen[F_CATALOG] = chosen[F_GENRE] = 0;
  if (filter == F_CATALOG) chosen[F_GENRE] = 0;
  picker.open = 0;
  restart();
}

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (picker.open) {
    int id = menu_key(&picker, k);
    if (id >= 0) pick(id);
    return;
  }
  if (k == K_BACK) {
    app_show(&home_screen);
    return;
  }
  if (!in_grid) {
    if (k == K_LEFT && filter > 0) filter--;
    else if (k == K_LEFT) nav_focus(NAV_DISCOVER);
    else if (k == K_RIGHT && filter < F_COUNT - 1 && noptions[filter + 1]) filter++;
    else if (k == K_DOWN && nitems) in_grid = 1;
    else if (k == K_OK && noptions[filter]) {
      menu_begin(&picker, NULL);
      for (int i = 0; i < noptions[filter]; i++) menu_add(&picker, options[filter][i], i == chosen[filter], i);
    }
    return;
  }
  if (k == K_LEFT && at % COLS) at--;
  else if (k == K_LEFT) nav_focus(NAV_DISCOVER);
  else if (k == K_RIGHT && at % COLS < COLS - 1 && at < nitems - 1) at++;
  else if (k == K_DOWN && at + COLS < nitems) at += COLS;
  else if (k == K_UP && at >= COLS) at -= COLS;
  else if (k == K_UP) in_grid = 0;
  else if (k == K_OK && at < nitems) title_open_card(&items[at]);
  else if (k == K_MENU && at < nitems) card_menu_open(&items[at], 0);
  /* two rows from the end, the next page */
  if (nitems - at <= COLS * 2) ask();
}

static void frame(double t, float dt) {
  (void)t;
  text_draw(tx("discoverTitle"), LEFT, TOP, 52, W_EXTRABOLD, C_WHITE);
  /* the three filters with what each is set to */
  float x = LEFT, fy = TOP + 83;
  float filter_x[F_COUNT];
  for (int f = 0; f < F_COUNT; f++) {
    filter_x[f] = x;
    if (!noptions[f]) continue;
    const char *label = options[f][chosen[f] < noptions[f] ? chosen[f] : 0];
    int focused = !in_grid && filter == f && !nav_has_focus();
    float w = text_width(label, 26, W_BOLD) + 30 + 24 + 30;
    gfx_rect(x, fy, w, 56, 12, focused ? C_WHITE : C_TEXT(0.1f));
    hit_add(FILTER_HIT + f, x, fy, w, 56);
    ui_line(label, x + 30, fy, 56, 26, W_BOLD, focused ? C_BG : C_TEXT(0.9f));
    /* the small triangle saying it opens */
    float tx0 = x + w - 30 - 12, ty = fy + 25;
    for (int r = 0; r < 6; r++) gfx_rect(tx0 + r, ty + r, 12 - 2 * r, 1.2f, 0, focused ? C_BG : C_TEXT(0.9f));
    x += w + 24;
  }
  /* the grid, a row above the focused one in sight */
  int row = at / COLS;
  scroll = ui_approach(scroll, in_grid && row > 0 ? (row - 1) * STEP_Y : 0, dt, 60);
  gfx_push_clip(LEFT - 14, GRID_Y - 18, SC_DESIGN_W - LEFT, SC_DESIGN_H - GRID_Y + 18);
  for (int i = 0; i < nitems; i++) {
    float cx = LEFT + (i % COLS) * STEP_X, cy = GRID_Y + (i / COLS) * STEP_Y - scroll;
    if (cy + TILE_H < GRID_Y - 18 || cy > SC_DESIGN_H) continue;
    int f = in_grid && i == at && !nav_has_focus();
    ui_tile(&items[i], cx, cy, TILE_W, TILE_H, f);
    hit_add(i, cx, cy, TILE_W, TILE_H);
  }
  if (!nitems && !loading) text_draw(tx("nothingFound"), LEFT, GRID_Y + 20, 30, W_BOLD, C_WHITE);
  else if (!nitems) text_draw(tx("searching"), LEFT, GRID_Y + 20, 26, W_REGULAR, C_TEXT(0.72f));
  gfx_pop_clip();
  menu_draw(&picker, filter_x[filter], fy + 56 + 12 + menu_height(&picker));
  nav_draw(NAV_DISCOVER, dt);
}

/* the pointer: the open list of a filter first; a click on a filter opens its list; passing over a
   title of the same row takes the focus (another row would scroll the grid under it), a click opens
   it */
static void point(float x, float y, int press) {
  if (nav_point(x, y, press)) return;
  if (picker.open) {
    int id = menu_point(&picker, x, y, press);
    if (id >= 0) pick(id);
    return;
  }
  int id = hit_at(x, y);
  if (id < 0) return;
  if (nav_has_focus()) nav_key(K_RIGHT);
  if (id >= FILTER_HIT) {
    in_grid = 0;
    filter = id - FILTER_HIT;
    if (press) key(K_OK);
    return;
  }
  if (!press && (!in_grid || id / COLS != at / COLS)) return;
  in_grid = 1;
  at = id;
  if (press) key(K_OK);
}

const screen discover_screen = {enter, key, frame, NULL, 0, point};
