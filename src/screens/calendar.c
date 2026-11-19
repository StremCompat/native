/* The calendar: the episodes of the followed series, a row per day (Today, Tomorrow, then by
   date), from a week ago to two months ahead (/app/calendar). */
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
#define TOP 56.0f
#define ROWS_Y 170.0f
#define ROW_H (59 + WIDE_H + 26)
#define MAX_DAYS 70

typedef struct {
  char title[48];
  card *items;
  int n, col;
  float scroll;
} day;

static day days[MAX_DAYS];
static int ndays, at, loaded, gen;
static char empty[200];
static float rows_y;

static void clear(void) {
  for (int d = 0; d < ndays; d++) {
    for (int i = 0; i < days[d].n; i++) card_clear(&days[d].items[i]);
    free(days[d].items);
  }
  ndays = 0;
}

static void came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  clear();
  const jval *list = json_get(answer, "days");
  for (int d = 0; d < json_len(list) && ndays < MAX_DAYS; d++) {
    const jval *v = json_at(list, d), *items = json_get(v, "items");
    day *dy = &days[ndays++];
    memset(dy, 0, sizeof *dy);
    snprintf(dy->title, sizeof dy->title, "%s", json_str(json_get(v, "title"), ""));
    dy->n = json_len(items);
    dy->items = calloc((size_t)dy->n + 1, sizeof(card));
    for (int i = 0; i < dy->n; i++) card_read(&dy->items[i], json_at(items, i));
  }
  snprintf(empty, sizeof empty, "%s", json_str(json_get(answer, "empty"), ""));
  loaded = 1;
}

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  day *d = at < ndays ? &days[at] : NULL;
  if (k == K_BACK) app_show(&home_screen);
  else if (k == K_UP && at > 0) at--;
  else if (k == K_DOWN && at < ndays - 1) at++;
  else if (k == K_RIGHT && d && d->col < d->n - 1) d->col++;
  else if (k == K_LEFT && d && d->col > 0) d->col--;
  else if (k == K_LEFT) nav_focus(NAV_CALENDAR);
  else if (k == K_OK && d && d->n) title_open(d->items[d->col].type, d->items[d->col].id);
  else if (k == K_MENU && d && d->n) card_menu_open(&d->items[d->col], 0);
}

static void frame(double t, float dt) {
  (void)t;
  text_draw(tx("navCalendar"), LEFT + 4, TOP, 50, W_EXTRABOLD, C_WHITE);
  if (!loaded) text_draw(tx("searching"), LEFT, ROWS_Y + 20, 26, W_REGULAR, C_TEXT(0.72f));
  else if (!ndays) text_draw_wrap(empty, LEFT, ROWS_Y + 20, 1300, 30, W_BOLD, C_WHITE, 2, 40);
  rows_y = ui_approach(rows_y, at * ROW_H, dt, 70);
  gfx_push_clip(128, ROWS_Y - 20, SC_DESIGN_W - 128, SC_DESIGN_H - ROWS_Y + 20);
  for (int d = 0; d < ndays; d++) {
    float y = ROWS_Y + d * ROW_H - rows_y, step = WIDE_W + CARD_GAP;
    if (y + ROW_H < ROWS_Y - 20 || y > SC_DESIGN_H) continue;
    day *dy = &days[d];
    dy->scroll = ui_approach(dy->scroll, ui_row_offset(dy->col, step, 1744), dt, 70);
    text_draw(dy->title, LEFT + 2, y + 14, 26, W_BOLD, C_TEXT(0.78f));
    for (int i = 0; i < dy->n; i++) {
      float x = LEFT + i * step - dy->scroll;
      if (x + WIDE_W < 128) continue;
      if (x > SC_DESIGN_W) break;
      ui_card(&dy->items[i], x, y + 59, 1, d == at && i == dy->col && !nav_has_focus());
      hit_add(d * 1000 + i, x < 128 ? 128 : x, y + 59, x < 128 ? x + WIDE_W - 128 : WIDE_W, WIDE_H);
    }
  }
  gfx_pop_clip();
  nav_draw(NAV_CALENDAR, dt);
}

static void enter(void) {
  gen++;
  loaded = at = 0;
  api_get("/app/calendar", came, (void *)(intptr_t)gen);
}

/* the pointer: passing over an episode of the day in focus takes the focus (another day would
   scroll the page under it); a click opens it */
static void point(float x, float y, int press) {
  if (nav_point(x, y, press)) return;
  int id = hit_at(x, y);
  if (id < 0 || (!press && id / 1000 != at)) return;
  if (nav_has_focus()) nav_key(K_RIGHT);
  at = id / 1000;
  days[at].col = id % 1000;
  if (press) key(K_OK);
}

const screen calendar_screen = {enter, key, frame, NULL, 0, point};
