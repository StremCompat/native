/* Similar titles: the top ones of the title's first genre, in a grid (web: similar.js). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TOP 48.0f
#define GRID_Y (TOP + 100)
#define TILE_W 264.0f
#define TILE_H 396.0f
#define STEP_X 288.0f
#define STEP_Y 420.0f
#define COLS 5
#define MAX_ITEMS 30

static card items[MAX_ITEMS];
static int count, at, gen, loaded;
static char title[200], empty[120];
static float scroll;

static void came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  const jval *list = json_get(answer, "items");
  for (int i = 0; i < count; i++) card_clear(&items[i]);
  count = 0;
  for (int i = 0; i < json_len(list) && count < MAX_ITEMS; i++) card_read(&items[count++], json_at(list, i));
  snprintf(title, sizeof title, "%s", json_str(json_get(answer, "title"), tx("similar")));
  snprintf(empty, sizeof empty, "%s", json_str(json_get(answer, "empty"), ""));
  loaded = 1;
}

void similar_open(const char *type, const char *id) {
  gen++;
  loaded = at = 0;
  scroll = 0;
  snprintf(title, sizeof title, "%s", tx("similar"));
  sbuf p = {0};
  sb_add(&p, "/app/similar?type=");
  sb_add_url(&p, type);
  sb_add(&p, "&id=");
  sb_add_url(&p, id);
  api_get(p.s, came, (void *)(intptr_t)gen);
  sb_free(&p);
  app_push(&similar_screen);
}

static void key(app_key k) {
  if (k == K_BACK) app_back();
  else if (k == K_LEFT && at % COLS) at--;
  else if (k == K_RIGHT && at % COLS < COLS - 1 && at < count - 1) at++;
  else if (k == K_DOWN && at + COLS < count) at += COLS;
  else if (k == K_UP && at >= COLS) at -= COLS;
  else if (k == K_OK && at < count) title_open(items[at].type, items[at].id);
  else if (k == K_MENU && at < count) card_menu_open(&items[at], 0);
}

static void frame(double t, float dt) {
  (void)t;
  text_draw_fit(title, LEFT, TOP, SC_DESIGN_W - LEFT - 48, 52, W_EXTRABOLD, C_WHITE);
  if (!loaded) text_draw(tx("findingSimilar"), LEFT, GRID_Y + 20, 27, W_REGULAR, C_TEXT(0.55f));
  else if (!count) text_draw(empty, LEFT, GRID_Y + 20, 30, W_BOLD, C_WHITE);
  int row = at / COLS;
  scroll = ui_approach(scroll, row > 0 ? (row - 1) * STEP_Y : 0, dt, 60);
  gfx_push_clip(LEFT - 14, GRID_Y - 18, SC_DESIGN_W - LEFT, SC_DESIGN_H - GRID_Y + 18);
  for (int i = 0; i < count; i++) {
    float x = LEFT + (i % COLS) * STEP_X, y = GRID_Y + (i / COLS) * STEP_Y - scroll;
    if (y + TILE_H < GRID_Y - 18 || y > SC_DESIGN_H) continue;
    ui_tile(&items[i], x, y, TILE_W, TILE_H, i == at);
    hit_add(i, x, y, TILE_W, TILE_H);
  }
  gfx_pop_clip();
}

static void enter(void) {}

/* the pointer: passing over a title of the same row takes the focus (another row would scroll the
   grid under it); a click opens it */
static void point(float x, float y, int press) {
  int id = hit_at(x, y);
  if (id < 0 || (!press && id / COLS != at / COLS)) return;
  at = id;
  if (press) key(K_OK);
}

const screen similar_screen = {enter, key, frame, NULL, 0, point};
