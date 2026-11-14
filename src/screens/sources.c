/* The sources of a film or episode, in a panel over its page: Best source, the language filter,
   the quality chips and the list, filled as the addons answer. The server orders and labels
   them (/app/streams); measures are the web interface's (streams.css). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define PANEL_X 1120.0f
#define PANEL_Y 100.0f
#define PAD_L 34.0f
#define PAD_R 54.0f
#define LIST_Y 324.0f
#define CONTENT_X (PANEL_X + PAD_L)
#define CONTENT_W (800.0f - PAD_L - PAD_R)

typedef struct {
  char *text, *kind;
} tag;

typedef struct {
  int seq;
  char *heading, *text, *compat;
  tag *tags;
  int ntags;
} source;

typedef struct {
  char *key, *label;
} chip;

enum { ZONE_BEST, ZONE_LANG, ZONE_CHIPS, ZONE_LIST };

static char *type, *id, *vid, *name;
static int gen, version, done, asking, recommended;
static source *items;
static int nitems;
static chip *chips, *langs;
static int nchips, nlangs;
static char *failed;
static int zone = ZONE_LIST, chip_at, lang_at, item_at, moved;
static int next_episode; /* opened by the player for the next episode: a matching source plays */
static float scroll;

static char *copy(const jval *v) {
  const char *s = json_str(v, NULL);
  return s ? str_dup(s) : NULL;
}

static void free_chips(chip **list, int *n) {
  for (int i = 0; i < *n; i++) {
    free((*list)[i].key);
    free((*list)[i].label);
  }
  free(*list);
  *list = NULL;
  *n = 0;
}

static void clear(void) {
  for (int i = 0; i < nitems; i++) {
    free(items[i].heading);
    free(items[i].text);
    free(items[i].compat);
    for (int t = 0; t < items[i].ntags; t++) {
      free(items[i].tags[t].text);
      free(items[i].tags[t].kind);
    }
    free(items[i].tags);
  }
  free(items);
  items = NULL;
  nitems = 0;
  free_chips(&chips, &nchips);
  free_chips(&langs, &nlangs);
  free(failed);
  failed = NULL;
}

static void read_chips(const jval *list, chip **out, int *n) {
  *n = json_len(list);
  *out = calloc((size_t)*n + 1, sizeof(chip));
  for (int i = 0; i < *n; i++) {
    (*out)[i].key = copy(json_get(json_at(list, i), "key"));
    (*out)[i].label = copy(json_get(json_at(list, i), "label"));
  }
}

static const char *current(chip *list, int n, int at) { return at < n ? list[at].key : "All"; }

static void ask(void);

static void answered(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  asking = 0;
  if (!answer) return; /* asked again on the next frame */
  /* the focus stays on top until the viewer moves, then follows the source it is on */
  int keep = moved && item_at < nitems ? items[item_at].seq : -1;
  char *keep_chip = str_dup(current(chips, nchips, chip_at)), *keep_lang = str_dup(current(langs, nlangs, lang_at));
  clear();
  version = (int)json_num(json_get(answer, "version"), 0);
  done = json_bool(json_get(answer, "done"), 0);
  recommended = (int)json_num(json_get(answer, "recommended"), -1);
  int match = (int)json_num(json_get(answer, "match"), 0);
  if (next_episode && match > 0) {
    next_episode = 0;
    app_back();
    player_start(type, id, vid, match);
    return;
  }
  failed = copy(json_get(answer, "failed"));
  read_chips(json_get(answer, "chips"), &chips, &nchips);
  read_chips(json_get(answer, "langs"), &langs, &nlangs);
  chip_at = lang_at = 0;
  for (int i = 0; i < nchips; i++)
    if (!strcmp(chips[i].key, keep_chip)) chip_at = i;
  for (int i = 0; i < nlangs; i++)
    if (!strcmp(langs[i].key, keep_lang)) lang_at = i;
  free(keep_chip);
  free(keep_lang);
  const jval *list = json_get(answer, "items");
  nitems = json_len(list);
  items = calloc((size_t)nitems + 1, sizeof(source));
  for (int i = 0; i < nitems; i++) {
    const jval *v = json_at(list, i), *tags = json_get(v, "tags");
    source *s = &items[i];
    s->seq = (int)json_num(json_get(v, "seq"), 0);
    s->heading = copy(json_get(v, "heading"));
    s->text = copy(json_get(v, "text"));
    s->compat = copy(json_get(v, "compat"));
    s->ntags = json_len(tags);
    s->tags = calloc((size_t)s->ntags + 1, sizeof(tag));
    for (int t = 0; t < s->ntags; t++) {
      s->tags[t].text = copy(json_get(json_at(tags, t), "t"));
      s->tags[t].kind = copy(json_get(json_at(tags, t), "c"));
    }
    if (s->seq == keep) item_at = i;
  }
  if (keep < 0 || item_at >= nitems) item_at = 0;
}

/* asked again with the version in hand: the server answers when more sources came */
static void ask(void) {
  if (asking || (done && version)) return;
  asking = 1;
  sbuf p = {0};
  sb_addf(&p, "/app/streams?have=%d&type=", version);
  sb_add_url(&p, type);
  sb_add(&p, "&vid=");
  sb_add_url(&p, vid);
  sb_add(&p, "&quality=");
  sb_add_url(&p, current(chips, nchips, chip_at));
  sb_add(&p, "&lang=");
  sb_add_url(&p, current(langs, nlangs, lang_at));
  if (next_episode) {
    sb_add(&p, "&next=1&id=");
    sb_add_url(&p, id);
  }
  api_get(p.s, answered, (void *)(intptr_t)gen);
  sb_free(&p);
}

/* the same search seen through another chip or language */
static void refilter(void) {
  gen++;
  asking = 0;
  version = 0;
  done = 0;
  item_at = 0;
  moved = 0;
  ask();
}

void sources_open_next(const char *t, const char *title_id, const char *video) {
  sources_open(t, title_id, video, NULL);
  next_episode = 1;
  refilter();
}

void sources_open(const char *t, const char *title_id, const char *video, const char *title_name) {
  next_episode = 0;
  free(type);
  free(id);
  free(vid);
  free(name);
  type = str_dup(t);
  id = str_dup(title_id);
  vid = str_dup(video);
  name = str_dup(title_name ? title_name : "");
  clear();
  gen++;
  asking = version = done = 0;
  zone = ZONE_LIST;
  chip_at = lang_at = item_at = moved = 0;
  scroll = 0;
  ask();
  app_push(&sources_screen);
}

static void key(app_key k) {
  if (k == K_BACK) {
    app_back();
    return;
  }
  if (zone == ZONE_LIST) {
    if (k == K_DOWN && item_at < nitems - 1) item_at++, moved = 1;
    else if (k == K_UP && item_at > 0) item_at--, moved = 1;
    else if (k == K_UP) zone = ZONE_CHIPS;
    else if (k == K_OK && item_at < nitems) player_start(type, id, vid, items[item_at].seq);
  } else if (zone == ZONE_CHIPS) {
    if (k == K_RIGHT && chip_at < nchips - 1) chip_at++, refilter();
    else if (k == K_LEFT && chip_at > 0) chip_at--, refilter();
    else if (k == K_DOWN && nitems) zone = ZONE_LIST;
    else if (k == K_UP) zone = ZONE_BEST;
  } else if (zone == ZONE_BEST) {
    if (k == K_RIGHT && nlangs) zone = ZONE_LANG;
    else if (k == K_DOWN) zone = ZONE_CHIPS;
  } else if (zone == ZONE_LANG) {
    if (k == K_LEFT) zone = ZONE_BEST;
    else if (k == K_DOWN) zone = ZONE_CHIPS;
    else if (k == K_OK) lang_at = (lang_at + 1) % nlangs, refilter();
  }
}

/* ---------- drawing ---------- */
/* the tags, wrapped over lines; returns where they end. draw 0 only measures. */
static float tag_row(const source *s, float x, float y, float w, int focused, int draw) {
  float tx0 = x, line_h = 32 + 6;
  for (int i = 0; i < s->ntags; i++) {
    const tag *t = &s->tags[i];
    const char *kind = t->kind ? t->kind : "";
    int fx = !strcmp(kind, "fx"), outline = fx || !strcmp(kind, "size") || !strcmp(kind, "seed") || !strcmp(kind, "sublang");
    float size = fx ? 17 : 20, tw = text_width(t->text ? t->text : "", size, W_BOLD) + 22;
    if (x + tw > tx0 + w) x = tx0, y += line_h;
    if (!draw) {
      x += tw + 8;
      continue;
    }
    uint32_t back = C_TEXT(0.1f), fore = C_TEXT(0.9f), ring = 0;
    if (!strcmp(kind, "q")) back = focused ? C_WHITE : C_TEXT(0.2f), fore = focused ? C_BG : C_WHITE;
    else if (!strcmp(kind, "lang")) back = focused ? C_TEXT(0.24f) : SC_RGBA(142, 227, 173, 41), fore = focused ? C_WHITE : SC_RGBA(191, 240, 208, 255);
    else if (fx) back = focused ? C_BG : 0, fore = C_WHITE, ring = focused ? C_WHITE : C_TEXT(0.7f);
    else if (outline) back = 0, fore = focused ? C_WHITE : !strcmp(kind, "sublang") ? C_TEXT(0.75f) : C_TEXT(0.9f), ring = focused && !strcmp(kind, "sublang") ? C_TEXT(0.5f) : C_TEXT(0.18f);
    else if (focused) back = C_TEXT(0.24f), fore = C_WHITE;
    if (back) gfx_rect(x, y, tw, 32, 6, back);
    if (ring) gfx_outline(x, y, tw, 32, 6, 2, ring);
    ui_line(t->text, x + 11, y, 32, size, W_BOLD, fore);
    x += tw + 8;
  }
  return s->ntags ? y + line_h - 6 : y;
}

/* one source; returns its height. draw 0 only measures it. */
static float draw_source(int i, float y, int draw) {
  const source *s = &items[i];
  int focused = draw && zone == ZONE_LIST && i == item_at;
  float x = CONTENT_X + 26, w = CONTENT_W - 52, top = y;
  y += 24;
  if (i == recommended) {
    char rec[128];
    utf8_upper(rec, sizeof rec, tx("recommendedSource"));
    if (draw) ui_line(rec, x, y, 26, 20, W_EXTRABOLD, focused ? C_WHITE : SC_RGBA(142, 227, 173, 255));
    y += 26 + 8;
  }
  if (draw) ui_line(s->heading, x, y, 36, 29, W_BOLD, C_WHITE);
  y += 36;
  if (s->ntags) y = tag_row(s, x, y + 9, w, focused, draw);
  if (s->compat) {
    if (draw) ui_line(s->compat, x, y + 7, 28, 21, W_BOLD, SC_RGBA(255, 189, 122, 255));
    y += 7 + 28;
  }
  float lh = 26 * 1.4f;
  int open = zone == ZONE_LIST && i == item_at; /* the focused one shows two lines */
  if (open) y += 9 + text_draw_wrap(s->text ? s->text : "", x, y + 9 + (lh - text_line_height(26)) / 2, w, 26, W_REGULAR, draw ? C_WHITE : 0, 2, lh);
  else {
    if (draw) text_draw_fit(s->text ? s->text : "", x, y + 9 + (lh - text_line_height(26)) / 2, w, 26, W_REGULAR, C_TEXT(0.72f));
    y += 9 + lh;
  }
  return y + 24 - top;
}

static void draw_list(float dt) {
  gfx_push_clip(CONTENT_X, LIST_Y, CONTENT_W, SC_DESIGN_H - 30 - LIST_Y);
  /* the focused source stays within 260 px of the top of the list */
  float at = 0;
  for (int i = 0; i < item_at && i < nitems; i++) at += draw_source(i, 0, 0) + 12;
  scroll = ui_approach(scroll, at > 260 ? at - 260 : 0, dt, 50);
  float y = LIST_Y - scroll;
  for (int i = 0; i < nitems && y < SC_DESIGN_H; i++) {
    float h = draw_source(i, 0, 0);
    if (y + h > LIST_Y) {
      if (zone == ZONE_LIST && i == item_at) {
        gfx_rect(CONTENT_X, y, CONTENT_W, h, 12, SC_RGBA(42, 48, 58, 255));
        gfx_outline(CONTENT_X, y, CONTENT_W, h, 12, 3, C_WHITE);
      }
      draw_source(i, y, 1);
    }
    y += h + 12;
  }
  const char *note = !nitems && done ? tx(version ? "noSourcesQuality" : "noSources") : failed;
  if (note && *note) text_draw_wrap(note, CONTENT_X + 26, y + 20, CONTENT_W - 52, 24, W_REGULAR, C_TEXT(0.72f), 3, 34);
  if (!nitems && !done) text_draw(tx("searching"), CONTENT_X + 26, LIST_Y + 20, 24, W_REGULAR, C_TEXT(0.72f));
  gfx_pop_clip();
}

static void frame(double t, float dt) {
  title_screen.frame(t, dt);
  ask();
  gfx_rect(PANEL_X, PANEL_Y, 800, SC_DESIGN_H - PANEL_Y, 0, SC_RGBA(17, 17, 19, 250));
  float y = PANEL_Y + 30;
  float hw = text_draw(tx("sources"), CONTENT_X, y, 38, W_BOLD, C_WHITE);
  /* Best source */
  int best_f = zone == ZONE_BEST;
  const char *best = tx("playBest");
  float bx = CONTENT_X + hw + 20, bw = 12 + 20 + 6 + text_width(best, 20, W_SEMIBOLD) + 18;
  gfx_rect(bx, y + 6, bw, 38, 19, best_f ? C_WHITE : C_TEXT(0.1f));
  ui_icon(ICON_BOLT, bx + 12, y + 15, 20, best_f ? C_BG : C_TEXT(0.9f));
  ui_line(best, bx + 38, y + 6, 38, 20, W_SEMIBOLD, best_f ? C_BG : C_TEXT(0.9f));
  /* the language filter, at the right, when the sources come in more than one */
  if (nlangs) {
    const char *label = langs[lang_at].label;
    float lw = text_width(label, 20, W_SEMIBOLD) + 36, lx = PANEL_X + 800 - PAD_R - lw;
    int f = zone == ZONE_LANG, on = lang_at > 0;
    if (f) gfx_rect(lx, PANEL_Y + 36, lw, 38, 19, C_WHITE);
    else gfx_outline(lx, PANEL_Y + 36, lw, 38, 19, 2, on ? C_TEXT(0.8f) : C_TEXT(0.16f));
    ui_line(label, lx + 18, PANEL_Y + 36, 38, 20, W_SEMIBOLD, f ? C_BG : on ? C_WHITE : C_TEXT(0.72f));
  }
  y += 54;
  text_draw_fit(name, CONTENT_X, y, CONTENT_W, 26, W_REGULAR, C_TEXT(0.72f));
  y += text_line_height(26) + 16;
  float x = CONTENT_X;
  for (int i = 0; i < nchips; i++) {
    int f = zone == ZONE_CHIPS && i == chip_at, cur = i == chip_at;
    float w = text_width(chips[i].label, 25, W_SEMIBOLD) + 56;
    if (x + w > CONTENT_X + CONTENT_W) break;
    gfx_rect(x, y, w, 54, 27, f ? C_WHITE : cur ? C_TEXT(0.22f) : C_TEXT(0.1f));
    ui_line(chips[i].label, x + 28, y, 54, 25, W_SEMIBOLD, f ? C_BG : cur ? C_WHITE : C_TEXT(0.8f));
    x += w + 12;
  }
  draw_list(dt);
}

static void enter(void) {}

const screen sources_screen = {enter, key, frame, NULL};
