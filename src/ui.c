#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "img.h"
#include "text.h"
#include "util.h"

static sc_tex icons;

int ui_init(void) {
  char path[600];
  snprintf(path, sizeof path, "%s", app_asset_dir("icons.png"));
  icons = img_load_file(path, 1);
  return icons.id ? 0 : -1;
}

void ui_icon(icon_id id, float x, float y, float size, uint32_t color) {
  float cx = (float)((int)id % ICON_COLUMNS) * ICON_CELL, cy = (float)((int)id / ICON_COLUMNS) * ICON_CELL;
  gfx_alpha_quad(&icons, x, y, size, size, cx / icons.w, cy / icons.h, (cx + ICON_CELL) / icons.w,
                 (cy + ICON_CELL) / icons.h, color);
}

float ui_approach(float v, float target, float dt, float tau) {
  if (dt <= 0) return v;
  float k = 1.0f - expf(-dt / tau);
  float next = v + (target - v) * k;
  /* close enough: land exactly, so still screens stop changing */
  return fabsf(target - next) < 0.05f ? target : next;
}

void ui_focus_ring(float x, float y, float w, float h, float radius) {
  /* box-shadow: 0 0 0 3px #0a0a0b, 0 0 0 7px #fff */
  gfx_outline(x - 3, y - 3, w + 6, h + 6, radius + 3, 3, C_BG);
  gfx_outline(x - 7, y - 7, w + 14, h + 14, radius + 7, 4, C_WHITE);
}

/* one band of a shade, from strength a at p0 to b at p1 (design pixels) */
static void shade_band(float p0, float p1, float a, float b, int vertical) {
  uint32_t ca = SC_RGBA(10, 10, 11, (int)(a * 255)), cb = SC_RGBA(10, 10, 11, (int)(b * 255));
  if (p1 <= p0) return;
  if (vertical) gfx_gradient(0, p0, SC_DESIGN_W, p1 - p0, 0, ca, cb, 1);
  else gfx_gradient(p0, 0, p1 - p0, SC_DESIGN_H, 0, ca, cb, 0);
}

void ui_shade(const shade_stop *s, int n, int vertical) {
  float len = vertical ? SC_DESIGN_H : SC_DESIGN_W;
  shade_band(0, s[0].at * len, s[0].alpha, s[0].alpha, vertical);
  for (int i = 0; i + 1 < n; i++) shade_band(s[i].at * len, s[i + 1].at * len, s[i].alpha, s[i + 1].alpha, vertical);
  shade_band(s[n - 1].at * len, len, s[n - 1].alpha, s[n - 1].alpha, vertical);
}

float ui_line(const char *s, float x, float y, float box, float size, int weight, uint32_t color) {
  return s && *s ? text_draw(s, x, y + (box - text_line_height(size)) / 2, size, weight, color) : 0;
}

float ui_rating(const char *rating, float x, float y, float box, float size, float badge_size, uint32_t color) {
  if (!rating || !*rating) return 0;
  float w = ui_line(rating, x, y, box, size, W_REGULAR, color) + 8, bh = badge_size + 6;
  float bw = text_width("IMDb", badge_size, W_EXTRABOLD) + 10, by = y + (box - bh) / 2 + 1;
  gfx_rect(x + w, by, bw, bh, 4, SC_RGBA(245, 197, 24, 255));
  ui_line("IMDb", x + w + 5, by, bh, badge_size, W_EXTRABOLD, SC_RGBA(17, 17, 17, 255));
  return w + bw;
}

void backdrop_set(backdrop *b, const char *url) {
  if (!url || !*url || (b->now && !strcmp(b->now, url))) return;
  free(b->before);
  b->before = b->now;
  b->now = str_dup(url);
  b->mix = 0;
}

void backdrop_draw(backdrop *b, float x, float h, float opacity, float dt) {
  b->mix = ui_approach(b->mix, 1, dt, 70);
  const sc_tex *before = img_get(b->before, 1280, NULL), *now = img_get(b->now, 1280, NULL);
  if (before && b->mix < 1) gfx_image(before, x, 0, SC_DESIGN_W - x, h, 0, opacity * (1 - b->mix), COVER_TOP);
  if (now) gfx_image(now, x, 0, SC_DESIGN_W - x, h, 0, opacity * (before ? b->mix : 1), COVER_TOP);
}

float ui_row_offset(int focused, float step, float width) {
  int fits = (int)(width / step);
  if (fits < 2) fits = 2;
  return focused > fits - 2 ? (focused - (fits - 2)) * step : 0;
}

void menu_begin(menu *m, const char *title) {
  snprintf(m->title, sizeof m->title, "%s", title ? title : "");
  m->n = m->at = 0;
  m->open = 1;
}

void menu_add(menu *m, const char *label, int on, int id) {
  if (m->n >= MENU_MAX) return;
  snprintf(m->items[m->n].label, sizeof m->items[m->n].label, "%s", label);
  m->items[m->n].on = on;
  m->items[m->n].id = id;
  if (on) m->at = m->n;
  m->n++;
}

int menu_key(menu *m, int key) {
  if (key == K_UP && m->at > 0) m->at--;
  else if (key == K_DOWN && m->at < m->n - 1) m->at++;
  else if (key == K_OK && m->n) return m->items[m->at].id;
  else if (key == K_BACK) {
    m->open = 0;
    return -2;
  }
  return -1;
}

#define MENU_ROW 62.0f
#define MENU_SHOWN 10

void menu_draw(const menu *m, float x, float bottom) {
  if (!m->open) return;
  float w = 360;
  for (int i = 0; i < m->n; i++) {
    float lw = text_width(m->items[i].label, 25, W_BOLD) + 44;
    if (lw > w) w = lw;
  }
  if (w > 820) w = 820;
  int first = m->at >= MENU_SHOWN ? m->at - MENU_SHOWN + 1 : 0, shown = m->n - first < MENU_SHOWN ? m->n - first : MENU_SHOWN;
  float head = m->title[0] ? 50 : 0, h = 24 + head + shown * MENU_ROW, y = bottom - h;
  gfx_rect(x, y, w + 24, h, 14, SC_RGBA(20, 20, 22, 247));
  if (head) {
    char up[128];
    utf8_upper(up, sizeof up, m->title);
    ui_line(up, x + 34, y + 12, head, 19, W_REGULAR, C_TEXT(0.75f));
  }
  for (int i = 0; i < shown; i++) {
    int at = first + i, f = at == m->at;
    float ry = y + 12 + head + i * MENU_ROW;
    if (f) gfx_rect(x + 12, ry, w, MENU_ROW, 8, C_WHITE);
    text_draw_fit(m->items[at].label, x + 34, ry + (MENU_ROW - text_line_height(25)) / 2, w - 44, 25,
                  m->items[at].on ? W_BOLD : W_REGULAR, f ? C_BG : C_WHITE);
  }
}

void ui_qr(const char *modules, int size, float x, float y, float box) {
  gfx_rect(x, y, box, box, 18, C_WHITE);
  if (!modules || size <= 0) return;
  /* the modules within the white square, with its own quiet margin */
  float cell = (box - 24) / (float)(size + 4), ox = x + 12 + 2 * cell, oy = y + 12 + 2 * cell;
  for (int r = 0; r < size; r++)
    for (int c = 0; c < size; c++)
      if (modules[r * size + c] == '1') gfx_rect(ox + c * cell, oy + r * cell, cell + 0.5f, cell + 0.5f, 0, SC_RGBA(0, 0, 0, 255));
}

/* ---------- toast ---------- */
#define TOAST_MS 3500.0
static char toast_text[256];
static double toast_at, toast_clock;

void ui_toast(const char *text) {
  snprintf(toast_text, sizeof toast_text, "%s", text ? text : "");
  toast_at = toast_clock;
}

void ui_draw_toast(double t) {
  toast_clock = t;
  if (!toast_text[0] || t - toast_at > TOAST_MS) return;
  float w = text_width(toast_text, 24, W_SEMIBOLD) + 52, y = SC_DESIGN_H * 0.08f;
  gfx_rect((SC_DESIGN_W - w) / 2, y, w, 54, 27, SC_RGBA(20, 20, 22, 219));
  ui_line(toast_text, (SC_DESIGN_W - w) / 2 + 26, y, 54, 24, W_SEMIBOLD, C_WHITE);
}

/* ---------- cards ---------- */
static char *dup_or_null(const char *s) { return s && *s ? str_dup(s) : NULL; }

void card_read(card *c, const jval *v) {
  memset(c, 0, sizeof *c);
  c->id = dup_or_null(json_str(json_get(v, "id"), NULL));
  c->type = dup_or_null(json_str(json_get(v, "type"), NULL));
  c->name = dup_or_null(json_str(json_get(v, "name"), NULL));
  c->art = dup_or_null(json_str(json_get(v, "art"), NULL));
  c->backdrop = dup_or_null(json_str(json_get(v, "backdrop"), NULL));
  c->poster = dup_or_null(json_str(json_get(v, "poster"), NULL));
  c->subtitle = dup_or_null(json_str(json_get(v, "subtitle"), NULL));
  c->video_id = dup_or_null(json_str(json_get(v, "videoId"), NULL));
  c->info = dup_or_null(json_str(json_get(v, "info"), NULL));
  c->description = dup_or_null(json_str(json_get(v, "description"), NULL));
  c->shape = dup_or_null(json_str(json_get(v, "shape"), NULL));
  c->glyph = dup_or_null(json_str(json_get(v, "glyph"), NULL));
  c->progress = (float)json_num(json_get(v, "progress"), 0);
  c->watched = json_bool(json_get(v, "watched"), 0);
  c->up_next = json_bool(json_get(v, "upNext"), 0);
  c->in_library = json_bool(json_get(v, "inLibrary"), 0);
  c->fresh = json_bool(json_get(v, "fresh"), 0);
  c->cw = json_bool(json_get(v, "cw"), 0);
}

void card_clear(card *c) {
  free(c->id);
  free(c->type);
  free(c->name);
  free(c->art);
  free(c->backdrop);
  free(c->poster);
  free(c->subtitle);
  free(c->video_id);
  free(c->info);
  free(c->description);
  free(c->shape);
  free(c->glyph);
  memset(c, 0, sizeof *c);
}

#define CARD_RADIUS 12.0f

/* no picture: the title on a dark gradient (.card.noart) */
static void no_art(const card *c, float x, float y, float w, float h, int wide) {
  gfx_gradient(x, y, w, h, CARD_RADIUS, SC_RGBA(34, 34, 37, 255), SC_RGBA(17, 22, 29, 255), 1);
  if (!wide && c->name) {
    float line = 27 * 1.25f, used = text_draw_wrap(c->name, 0, 0, w - 36, 27, W_BOLD, 0, 3, line);
    text_draw_wrap(c->name, x + 18, y + h - 20 - used, w - 36, 27, W_BOLD, C_WHITE, 3, line);
  }
}

/* the title and the line under it on wide cards (.cwlabel), over a shade, and the progress */
static void wide_label(const card *c, float x, float y, float w, float h) {
  /* the shade starts clear, so its own rounded top corners never show */
  float shade = 120;
  gfx_gradient(x, y + h - shade, w, shade, CARD_RADIUS, SC_RGBA(0, 0, 0, 0), SC_RGBA(0, 0, 0, 219), 1);
  float bottom = y + h - 28, sub_h = c->subtitle ? 30 : 0;
  if (c->name) text_draw_fit(c->name, x + 16, bottom - sub_h - 31, w - 32, 25, W_SEMIBOLD, C_WHITE);
  if (c->subtitle)
    text_draw_fit(c->subtitle, x + 16, bottom - 27, w - 32, 21, c->up_next ? W_SEMIBOLD : W_REGULAR,
                  c->up_next ? C_WHITE : C_TEXT(0.76f));
  if (c->progress > 0) {
    float bw = w - 28;
    gfx_rect(x + 14, y + h - 18, bw, 6, 3, C_TEXT(0.28f));
    gfx_rect(x + 14, y + h - 18, bw * clampf(c->progress, 0, 1), 6, 3, C_WHITE);
  }
}

static void name_label(const card *c, float x, float y, float w, float h) {
  if (!c->name) return;
  gfx_gradient(x, y + h - 90, w, 90, CARD_RADIUS, SC_RGBA(0, 0, 0, 0), SC_RGBA(0, 0, 0, 235), 1);
  text_draw_fit(c->name, x + 15, y + h - 13 - text_line_height(24), w - 30, 24, W_SEMIBOLD, C_WHITE);
}

void ui_card_name(const card *c, float x, float y, int focused) { name_label(c, x, focused ? y - 6 : y, CARD_W, CARD_H); }

static void draw_card(const card *c, float x, float y, float w, float h, int wide, int focused) {
  if (focused) y -= 6;
  float fade;
  const sc_tex *pic = img_get(c->art, (int)w, &fade);
  if (pic) {
    if (fade < 1) gfx_rect(x, y, w, h, CARD_RADIUS, C_CARD);
    gfx_image(pic, x, y, w, h, CARD_RADIUS, fade, COVER);
  } else if (!c->art || img_failed(c->art, (int)w)) {
    no_art(c, x, y, w, h, wide);
  } else {
    gfx_rect(x, y, w, h, CARD_RADIUS, C_CARD);
  }
  if (wide) {
    wide_label(c, x, y, w, h);
  } else if (c->watched) {
    /* .wbadge: a check in a dark circle at the top right */
    gfx_rect(x + w - 54, y + 12, 42, 42, 21, SC_RGBA(0, 0, 0, 184));
    ui_icon(ICON_CHECK, x + w - 54 + 8, y + 20, 26, C_WHITE);
  }
  if (focused) ui_focus_ring(x, y, w, h, CARD_RADIUS);
}

void ui_card(const card *c, float x, float y, int wide, int focused) {
  draw_card(c, x, y, wide ? WIDE_W : CARD_W, wide ? WIDE_H : CARD_H, wide, focused);
}

float ui_folder_width(const card *c) {
  if (c->shape && !strcmp(c->shape, "wide")) return 540;
  if (c->shape && !strcmp(c->shape, "square")) return CARD_H - 44;
  return CARD_W;
}

void ui_folder_tile(const card *c, float x, float y, int focused) {
  float w = ui_folder_width(c), h = CARD_H - 44;
  gfx_rect(x, y, w, h, CARD_RADIUS, C_TEXT(0.07f));
  const sc_tex *pic = c->art ? img_get(c->art, (int)w, NULL) : NULL;
  if (pic) gfx_image(pic, x, y, w, h, CARD_RADIUS, 1, COVER);
  else if (c->glyph) {
    float gw = text_width(c->glyph, 58, W_EXTRABOLD);
    ui_line(c->glyph, x + (w - gw) / 2, y, h, 58, W_EXTRABOLD, C_TEXT(0.85f));
  }
  if (focused) gfx_outline(x - 4, y - 4, w + 8, h + 8, CARD_RADIUS + 4, 4, C_WHITE);
  if (c->name) text_draw_fit(c->name, x + 2, y + h + 10, w - 4, 21, W_SEMIBOLD, focused ? C_WHITE : C_TEXT(0.85f));
}

void ui_tile(const card *c, float x, float y, float w, float h, int focused) {
  draw_card(c, x, y, w, h, 0, focused);
  name_label(c, x, focused ? y - 6 : y, w, h);
}

float menu_height(const menu *m) {
  int shown = m->n < MENU_SHOWN ? m->n : MENU_SHOWN;
  return 24 + (m->title[0] ? 50 : 0) + shown * MENU_ROW;
}
