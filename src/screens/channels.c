/* Live channels: a row per group (an addon's channel catalog, or a group of an M3U list), each
   channel with its logo and what is on now when the list has a guide. At the top, adding an M3U
   list (typed with the on-screen keyboard) or removing them (/app/channels). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "img.h"
#include "keyboard.h"
#include "nav.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TOP 56.0f
#define ROWS_Y 260.0f
#define TILE 220.0f
#define TILE_STEP 244.0f
#define ROW_H (59 + TILE + 70)
#define MAX_GROUPS 64
#define MAX_CHANNELS 400

typedef struct {
  char *id, *type, *name, *logo, *now, *url;
  int m3u;
} channel;

typedef struct {
  char title[128];
  channel *items;
  int n, col;
  float scroll;
} group;

enum { B_ADD, B_REMOVE };

static group groups[MAX_GROUPS];
static int ngroups, nlists, row, button, loaded, gen, typing; /* row -1: the buttons */
static char empty[256];
static keyboard kb;
static float rows_y;

static char *copy(const jval *v) {
  const char *s = json_str(v, NULL);
  return s && *s ? str_dup(s) : NULL;
}

static void clear(void) {
  for (int g = 0; g < ngroups; g++) {
    for (int i = 0; i < groups[g].n; i++) {
      channel *c = &groups[g].items[i];
      free(c->id), free(c->type), free(c->name), free(c->logo), free(c->now), free(c->url);
    }
    free(groups[g].items);
  }
  ngroups = 0;
}

static void came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  clear();
  const jval *list = json_get(answer, "groups");
  for (int g = 0; g < json_len(list) && ngroups < MAX_GROUPS; g++) {
    const jval *v = json_at(list, g), *items = json_get(v, "items");
    group *gr = &groups[ngroups++];
    memset(gr, 0, sizeof *gr);
    snprintf(gr->title, sizeof gr->title, "%s", json_str(json_get(v, "title"), ""));
    gr->n = json_len(items) < MAX_CHANNELS ? json_len(items) : MAX_CHANNELS;
    gr->items = calloc((size_t)gr->n + 1, sizeof(channel));
    for (int i = 0; i < gr->n; i++) {
      const jval *c = json_at(items, i);
      channel *ch = &gr->items[i];
      ch->id = copy(json_get(c, "id"));
      ch->type = copy(json_get(c, "type"));
      ch->name = copy(json_get(c, "name"));
      ch->logo = copy(json_get(c, "logo"));
      ch->now = copy(json_get(c, "now"));
      ch->url = copy(json_get(c, "url"));
      ch->m3u = !strcmp(json_str(json_get(c, "kind"), ""), "m3u");
    }
  }
  nlists = (int)json_num(json_get(answer, "lists"), 0);
  snprintf(empty, sizeof empty, "%s", json_str(json_get(answer, "empty"), ""));
  loaded = 1;
  if (row >= ngroups) row = ngroups ? 0 : -1;
}

static void load(void) {
  gen++;
  api_get("/app/channels", came, (void *)(intptr_t)gen);
}

static void lists_changed(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const char *message = json_str(json_get(json_root(d), "message"), "");
  if (*message) ui_toast(message);
  json_free(d);
  load();
}

static void change_lists(const char *url) {
  sbuf b = {0};
  if (url) {
    sb_add(&b, "{\"url\":");
    sb_add_json_str(&b, url);
    sb_add(&b, "}");
  } else {
    sb_add(&b, "{\"remove\":true}");
  }
  net_request("/app/channels/lists", b.s, NET_HIGH, NULL, lists_changed, NULL);
  sb_free(&b);
}

static void open_channel(const channel *c) {
  if (c->m3u && c->url) player_play_address(c->url, c->name, 1);
  else if (c->id && c->type) sources_open(c->type, c->id, c->id, c->name);
}

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (typing) {
    if (k == K_BACK) typing = 0;
    else if (kb_key(&kb, k) == KB_DONE) {
      typing = 0;
      if (kb.text[0]) change_lists(kb.text);
    }
    return;
  }
  group *g = row >= 0 && row < ngroups ? &groups[row] : NULL;
  if (k == K_BACK) app_show(&home_screen);
  else if (row < 0) {
    if (k == K_RIGHT && button == B_ADD && nlists) button = B_REMOVE;
    else if (k == K_LEFT && button == B_REMOVE) button = B_ADD;
    else if (k == K_LEFT) nav_focus(NAV_CHANNELS);
    else if (k == K_DOWN && ngroups) row = 0;
    else if (k == K_OK && button == B_ADD) kb_reset(&kb, "https://", 0), typing = 1;
    else if (k == K_OK) change_lists(NULL);
  } else if (k == K_UP) row--;
  else if (k == K_DOWN && row < ngroups - 1) row++;
  else if (k == K_RIGHT && g && g->col < g->n - 1) g->col++;
  else if (k == K_LEFT && g && g->col > 0) g->col--;
  else if (k == K_LEFT) nav_focus(NAV_CHANNELS);
  else if (k == K_OK && g && g->n) open_channel(&g->items[g->col]);
}

static void text(const char *utf8) {
  if (typing) kb_type(&kb, utf8);
}

static void draw_channel(const channel *c, float x, float y, int focused) {
  if (focused) y -= 6;
  gfx_rect(x, y, TILE, TILE, 16, C_TEXT(0.08f));
  const sc_tex *logo = img_get(c->logo, (int)TILE, NULL);
  if (logo) {
    /* the whole logo, inside the square with a margin */
    float box = TILE - 48, s = box / logo->w < box / logo->h ? box / logo->w : box / logo->h;
    gfx_image(logo, x + (TILE - logo->w * s) / 2, y + (TILE - logo->h * s) / 2, logo->w * s, logo->h * s, 0, 1, STRETCH);
  } else if (c->name) {
    text_draw_wrap(c->name, x + 18, y + 18, TILE - 36, 26, W_BOLD, C_WHITE, 4, 32);
  }
  if (focused) ui_focus_ring(x, y, TILE, TILE, 16);
  text_draw_fit(c->name ? c->name : "", x + 4, y + TILE + 12, TILE - 8, 22, W_SEMIBOLD, focused ? C_WHITE : C_TEXT(0.86f));
  if (c->now) text_draw_fit(c->now, x + 4, y + TILE + 42, TILE - 8, 19, W_REGULAR, C_TEXT(0.6f));
}

static void frame(double t, float dt) {
  (void)t;
  text_draw(tx("navChannels"), LEFT + 4, TOP, 50, W_EXTRABOLD, C_WHITE);
  /* the list buttons */
  const char *labels[2] = {tx("m3uAdd"), tx("m3uRemove")};
  float bx = LEFT;
  for (int b = 0; b < (nlists ? 2 : 1); b++) {
    int f = row < 0 && button == b && !nav_has_focus() && !typing;
    float w = text_width(labels[b], 25, W_BOLD) + 60;
    gfx_rect(bx, TOP + 92, w, 62, 13, f ? C_WHITE : C_TEXT(0.14f));
    ui_line(labels[b], bx + 30, TOP + 92, 62, 25, W_BOLD, f ? SC_RGBA(12, 11, 17, 255) : C_WHITE);
    bx += w + 16;
  }
  if (!loaded) text_draw(tx("searching"), LEFT, ROWS_Y + 20, 26, W_REGULAR, C_TEXT(0.72f));
  else if (!ngroups) text_draw_wrap(empty, LEFT, ROWS_Y + 20, 1300, 28, W_REGULAR, C_TEXT(0.78f), 3, 38);
  rows_y = ui_approach(rows_y, row > 0 ? row * ROW_H : 0, dt, 70);
  gfx_push_clip(128, ROWS_Y - 20, SC_DESIGN_W - 128, SC_DESIGN_H - ROWS_Y + 20);
  for (int g = 0; g < ngroups; g++) {
    float y = ROWS_Y + g * ROW_H - rows_y;
    if (y + ROW_H < ROWS_Y - 20 || y > SC_DESIGN_H) continue;
    group *gr = &groups[g];
    gr->scroll = ui_approach(gr->scroll, ui_row_offset(gr->col, TILE_STEP, 1744), dt, 70);
    text_draw_fit(gr->title, LEFT + 2, y + 14, 1400, 26, W_BOLD, C_TEXT(0.78f));
    for (int i = 0; i < gr->n; i++) {
      float x = LEFT + i * TILE_STEP - gr->scroll;
      if (x + TILE < 128) continue;
      if (x > SC_DESIGN_W) break;
      draw_channel(&gr->items[i], x, y + 59, g == row && i == gr->col && !nav_has_focus() && !typing);
    }
  }
  gfx_pop_clip();
  nav_draw(NAV_CHANNELS, dt);
  if (typing) {
    float kw = 13 * 72 - 8, kx = (SC_DESIGN_W - kw) / 2, ky = SC_DESIGN_H - 360;
    gfx_rect(kx - 30, ky - 130, kw + 60, 460, 18, SC_RGBA(20, 20, 22, 250));
    text_draw(tx("m3uAddress"), kx, ky - 110, 22, W_BOLD, C_TEXT(0.7f));
    gfx_rect(kx, ky - 72, kw, 56, 10, C_TEXT(0.1f));
    text_draw_fit(kb.text, kx + 18, ky - 72 + (56 - text_line_height(26)) / 2, kw - 36, 26, W_REGULAR, C_WHITE);
    kb_draw(&kb, kx, ky);
  }
}

static void enter(void) {
  row = -1;
  button = B_ADD;
  typing = 0;
  load();
}

const screen channels_screen = {enter, key, frame, text, 0};
