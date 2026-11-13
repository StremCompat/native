/* Home: the featured titles at the top (a large hero that changes every 8 s), then the rows:
   Continue watching, new episodes, the library and the addons' catalogs. Below the top the hero
   is the compact one and shows the focused title. Measures are the web interface's (board.css). */
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

#define ROWS_TOP 396.0f      /* #rowsClip */
#define ROWS_TOP_FEATURED 836.0f
#define ROW_LEFT 176.0f
#define ROW_TITLE_H 59.0f    /* 14 padding + the title line + 14 margin */
#define ROW_BOTTOM 26.0f
#define CONTENT_W 1744.0f    /* the screen without the rail */
#define FEATURED_MAX 8
#define FEATURED_EVERY_MS 8000.0
#define HERO_WAIT_MS 260.0   /* the hero follows the focus once it rests */
#define RETRY_MS 2000.0      /* Home asked again while the server does not answer */

typedef struct {
  char *title, *catalog; /* catalog: the /app/catalog address, for rows still to fetch */
  int wide, ranked, answered;
  card *cards;
  int n, col;
  float scroll; /* shown horizontal offset, moving toward the focused card */
} row;

/* the title the hero shows, with what its metadata adds */
typedef struct {
  char *id, *name, *where, *backdrop, *logo, *genres, *info, *rating, *description;
} hero_info;

static row *rows;
static int nrows, loaded, gen;
static int cur;            /* the focused row */
static int top = 1;        /* the focus is on the featured titles */
static int featured_at;
static double featured_since, last_key, hero_changed, asked_at;
static int hero_asked;
static hero_info hero;
static const card *hero_from; /* the card it came from: one title can be on two rows */
static float rows_y;       /* shown vertical offset of the rows */
static char *bg_now, *bg_before;
static float bg_mix = 1;   /* 0..1 from the old backdrop to the new one */
static double clock_now;

/* ---------- data ---------- */
static void hero_clear(hero_info *h) {
  char **fields[] = {&h->id, &h->name, &h->where, &h->backdrop, &h->logo, &h->genres, &h->info, &h->rating, &h->description};
  for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
    free(*fields[i]);
    *fields[i] = NULL;
  }
}

static char *copy(const char *s) { return s && *s ? str_dup(s) : NULL; }

static void rows_clear(void) {
  for (int r = 0; r < nrows; r++) {
    for (int i = 0; i < rows[r].n; i++) card_clear(&rows[r].cards[i]);
    free(rows[r].cards);
    free(rows[r].title);
    free(rows[r].catalog);
  }
  free(rows);
  rows = NULL;
  nrows = 0;
}

static void read_cards(row *r, const jval *items) {
  r->n = json_len(items);
  r->cards = calloc((size_t)(r->n ? r->n : 1), sizeof(card));
  for (int i = 0; i < r->n; i++) card_read(&r->cards[i], json_at(items, i));
}

static void catalog_answered(const jval *answer, void *user) {
  intptr_t tag = (intptr_t)user;
  int index = (int)(tag % 1000);
  if (tag / 1000 != gen || index >= nrows) return; /* Home was loaded again meanwhile */
  row *r = &rows[index];
  r->answered = 1;
  if (answer) read_cards(r, json_get(answer, "items"));
}

static void home_answered(const jval *answer, void *user) {
  if ((intptr_t)user != gen || !answer) return;
  const jval *list = json_get(answer, "rows");
  rows_clear();
  nrows = json_len(list);
  rows = calloc((size_t)(nrows ? nrows : 1), sizeof(row));
  for (int i = 0; i < nrows; i++) {
    const jval *v = json_at(list, i), *cat = json_get(v, "catalog");
    row *r = &rows[i];
    r->title = copy(json_str(json_get(v, "title"), ""));
    r->wide = json_bool(json_get(v, "wide"), 0);
    r->ranked = json_bool(json_get(v, "ranked"), 0);
    if (cat->type == J_OBJ) {
      sbuf path = {0};
      sb_add(&path, "/app/catalog?url=");
      sb_add_url(&path, json_str(json_get(cat, "url"), ""));
      sb_add(&path, "&key=");
      sb_add_url(&path, json_str(json_get(cat, "key"), ""));
      sb_add(&path, "&type=");
      sb_add_url(&path, json_str(json_get(cat, "type"), ""));
      r->catalog = path.s;
      api_get(r->catalog, catalog_answered, (void *)(intptr_t)(gen * 1000 + i));
    } else {
      r->answered = 1;
      read_cards(r, json_get(v, "items"));
    }
  }
  loaded = 1;
}

static void load(void) {
  gen++;
  loaded = 0;
  asked_at = clock_now;
  api_get("/app/home", home_answered, (void *)(intptr_t)gen);
}

/* ---------- what is shown ---------- */
static int shown(const row *r, int index) { return r->n > 0 || (!r->answered && index < 3); }

/* the first catalog with more than two titles gives the featured ones; wide rows never do */
static const row *featured_row(void) {
  for (int i = 0; i < nrows; i++) {
    if (!rows[i].catalog || rows[i].wide) continue;
    if (!rows[i].answered) return NULL;
    if (rows[i].n > 2) return &rows[i];
  }
  return NULL;
}

static int featured_count(void) {
  const row *f = featured_row();
  return f ? (f->n < FEATURED_MAX ? f->n : FEATURED_MAX) : 0;
}

static int at_featured(void) { return top && featured_count() > 2; }

static float row_height(const row *r) { return ROW_TITLE_H + (r->wide ? WIDE_H : CARD_H) + ROW_BOTTOM; }

static float card_step(const row *r) { return (r->wide ? WIDE_W : CARD_W) + CARD_GAP; }

/* the rows scroll so the focused one sits at the top of their area */
static float rows_offset(void) {
  float y = 0;
  for (int i = 0; i < cur && i < nrows; i++)
    if (shown(&rows[i], i)) y += row_height(&rows[i]);
  return y;
}

/* the card the focus is on, if the rows have it */
static const card *focused_card(void) {
  if (cur >= nrows || !rows[cur].n) return NULL;
  return &rows[cur].cards[rows[cur].col];
}

static const card *hero_card(void) {
  if (at_featured()) return &featured_row()->cards[featured_at % featured_count()];
  return focused_card();
}

/* moves the focus to the next row with titles in that direction; 0 when there is none */
static int move_row(int dir) {
  for (int i = cur + dir; i >= 0 && i < nrows; i += dir)
    if (rows[i].n > 0) {
      cur = i;
      return 1;
    }
  return 0;
}

/* the focus to the first row with titles; 0 when none has any yet */
static int first_row(void) {
  cur = -1;
  if (move_row(1)) return 1;
  cur = 0;
  return 0;
}

static void meta_answered(const jval *answer, void *user) {
  const jval *m = json_get(answer, "meta");
  if ((intptr_t)user != gen || !hero.id || strcmp(hero.id, json_str(json_get(m, "id"), ""))) return;
  free(hero.description);
  hero.description = copy(json_str(json_get(m, "description"), NULL));
  /* Action  •  Crime  •  Drama */
  sbuf g = {0};
  const jval *genres = json_get(m, "genres");
  for (int i = 0; i < json_len(genres) && i < 3; i++) {
    if (i) sb_add(&g, "  \xE2\x80\xA2  ");
    sb_add(&g, json_str(json_at(genres, i), ""));
  }
  free(hero.genres);
  hero.genres = g.s;
  /* 109 min   2026 */
  sbuf info = {0};
  const char *runtime = json_str(json_get(m, "runtime"), ""), *year = json_str(json_get(m, "releaseInfo"), "");
  if (!*year) year = json_str(json_get(m, "year"), "");
  sb_addf(&info, "%s%s%s", runtime, *runtime && *year ? "   " : "", year);
  free(hero.info);
  hero.info = info.s;
  free(hero.rating);
  hero.rating = copy(json_str(json_get(m, "imdbRating"), NULL));
}

/* the hero follows its title: the name, picture and line at once, the rest when it rests */
static void follow_hero(void) {
  const card *c = hero_card();
  if (!c || !c->id || (c == hero_from && hero.id && !strcmp(hero.id, c->id))) return;
  hero_from = c;
  hero_clear(&hero);
  hero.id = str_dup(c->id);
  hero.name = copy(c->name);
  hero.where = copy(c->subtitle);
  hero.backdrop = copy(c->backdrop);
  if (!strncmp(c->id, "tt", 2)) {
    char logo[256];
    snprintf(logo, sizeof logo, "https://images.metahub.space/logo/medium/%s/img", c->id);
    hero.logo = str_dup(logo);
  }
  hero_changed = clock_now;
  hero_asked = 0;
  if (hero.backdrop && (!bg_now || strcmp(bg_now, hero.backdrop))) {
    free(bg_before);
    bg_before = bg_now;
    bg_now = str_dup(hero.backdrop);
    bg_mix = 0;
  }
}

static void ask_hero_meta(void) {
  const card *c = hero_card();
  if (hero_asked || !c || !c->type || clock_now - hero_changed < HERO_WAIT_MS) return;
  hero_asked = 1;
  sbuf path = {0};
  sb_add(&path, "/app/meta?type=");
  sb_add_url(&path, c->type);
  sb_add(&path, "&id=");
  sb_add_url(&path, c->id);
  api_get(path.s, meta_answered, (void *)(intptr_t)gen);
  sb_free(&path);
}

/* ---------- drawing ---------- */
static void draw_backdrop(int featured, float dt) {
  /* #bg: the right part of the screen, or more of it with the featured titles */
  float x = featured ? 346 : 730, h = featured ? 929 : 670, o = featured ? 0.92f : 0.86f;
  bg_mix = ui_approach(bg_mix, 1, dt, 70);
  const sc_tex *before = img_get(bg_before, 1280, NULL), *now = img_get(bg_now, 1280, NULL);
  if (before && bg_mix < 1) gfx_image(before, x, 0, SC_DESIGN_W - x, h, 0, o * (1 - bg_mix), COVER_TOP);
  if (now) gfx_image(now, x, 0, SC_DESIGN_W - x, h, 0, o * (now == before ? 1 : bg_mix), COVER_TOP);
  static const shade_stop down[] = {{0.26f, 0}, {0.38f, 0.25f}, {0.49f, 0.6f}, {0.57f, 0.88f}, {0.61f, 1}};
  static const shade_stop right[] = {{0.38f, 1}, {0.44f, 0.9f}, {0.51f, 0.66f}, {0.59f, 0.36f}, {0.67f, 0.14f}, {0.76f, 0}};
  static const shade_stop down_f[] = {{0.40f, 0}, {0.58f, 0.35f}, {0.76f, 0.85f}, {0.86f, 1}};
  static const shade_stop right_f[] = {{0.18f, 1}, {0.26f, 0.86f}, {0.38f, 0.55f}, {0.52f, 0.2f}, {0.64f, 0}};
  ui_shade(featured ? down_f : down, featured ? 4 : 5, 1);
  ui_shade(featured ? right_f : right, featured ? 5 : 6, 0);
}

/* one line of text centred in a CSS line box of height box */
static float line(const char *s, float x, float y, float box, float size, int weight, uint32_t color) {
  return s ? text_draw(s, x, y + (box - text_line_height(size)) / 2, size, weight, color) : 0;
}

static void draw_hero(int featured) {
  float x = ROW_LEFT, y = featured ? 340 : 70, w = featured ? 998 : 691;
  float logo_w = featured ? 560 : 420, logo_h = featured ? 160 : 122;
  /* the logo when it came, else the name; while the logo is on its way, nothing blinks in */
  const sc_tex *logo = img_get(hero.logo, (int)logo_w, NULL);
  if (logo) {
    float s = logo_w / logo->w < logo_h / logo->h ? logo_w / logo->w : logo_h / logo->h;
    gfx_image(logo, x, y + logo_h - logo->h * s, logo->w * s, logo->h * s, 0, 1, STRETCH);
  } else if (hero.name && (!hero.logo || img_failed(hero.logo, (int)logo_w))) {
    int long_name = strlen(hero.name) > 32;
    float size = featured ? (long_name ? 56 : 76) : (long_name ? 44 : 60), lh = size * 1.04f;
    float used = text_draw_wrap(hero.name, 0, 0, w, size, W_EXTRABOLD, 0, 2, lh);
    text_draw_wrap(hero.name, x, y + logo_h - used, w, size, W_EXTRABOLD, C_WHITE, 2, lh);
  }
  y += logo_h;
  if (featured) {
    y += 14;
    line(hero.genres, x, y, 34, 26, W_SEMIBOLD, C_TEXT(0.62f));
    y += 34;
  }
  /* where the viewer stopped, the length and year, the rating */
  float size = featured ? 28 : 24, box = featured ? 38 : 32, mx = x;
  y += 16;
  if (hero.where) mx += line(hero.where, mx, y, box, size, W_BOLD, C_WHITE) + 24;
  if (hero.info) mx += line(hero.info, mx, y, box, size, W_REGULAR, C_TEXT(0.72f)) + 24;
  if (hero.rating) {
    mx += line(hero.rating, mx, y, box, size, W_REGULAR, C_TEXT(0.72f)) + 8;
    float bw = text_width("IMDb", 16, W_EXTRABOLD) + 10, by = y + (box - 22) / 2 + 1;
    gfx_rect(mx, by, bw, 22, 4, SC_RGBA(245, 197, 24, 255));
    line("IMDb", mx + 5, by, 22, 16, W_EXTRABOLD, SC_RGBA(17, 17, 17, 255));
  }
  y += box + 12;
  float ds = featured ? 30 : 25, lh = ds * 1.45f;
  if (hero.description)
    text_draw_wrap(hero.description, x, y + (lh - text_line_height(ds)) / 2, w, ds, W_REGULAR, C_TEXT(0.78f), 2, lh);
  if (!featured) return;
  /* See details, and the dots saying which featured title this is */
  y += 2 * lh + 28;
  const char *label = tx("seeDetails");
  float bw = text_width(label, 26, W_BOLD) + 72;
  gfx_rect(x, y, bw, 64, 32, nav_has_focus() ? C_TEXT(0.16f) : C_WHITE);
  line(label, x + 36, y, 64, 26, W_BOLD, nav_has_focus() ? C_WHITE : C_BG);
  float dx = 1676, dy = y + 64 - 27 - 10;
  for (int i = 0; i < featured_count(); i++) {
    float dw = i == featured_at ? 30 : 10;
    gfx_rect(dx, dy, dw, 10, 5, i == featured_at ? C_WHITE : C_TEXT(0.35f));
    dx += dw + 8;
  }
}

static void draw_rank(int n, float x, float y) {
  char s[12];
  snprintf(s, sizeof s, "%d", n);
  text_draw(s, x + 12, y + 7, 72, W_EXTRABOLD, SC_RGBA(0, 0, 0, 200));
  text_draw(s, x + 10, y + 4, 72, W_EXTRABOLD, C_WHITE);
}

static void draw_rows(int featured, float dt) {
  float area = featured ? ROWS_TOP_FEATURED : ROWS_TOP;
  rows_y = ui_approach(rows_y, featured ? 0 : rows_offset(), dt, 70);
  gfx_push_clip(128, area - 20, SC_DESIGN_W - 128, SC_DESIGN_H - area + 20);
  float y = area - rows_y;
  for (int i = 0; i < nrows; i++) {
    row *r = &rows[i];
    if (!shown(r, i)) continue;
    float h = row_height(r), step = card_step(r);
    if (y + h > area - 20 && y < SC_DESIGN_H) {
      int is_cur = i == cur && !featured && !nav_has_focus();
      int vis = (int)(CONTENT_W / step);
      if (vis < 2) vis = 2;
      float target = r->col > vis - 2 ? (r->col - (vis - 2)) * step : 0;
      r->scroll = ui_approach(r->scroll, target, dt, 70);
      text_draw_fit(r->title, ROW_LEFT + 2, y + 14, CONTENT_W * 0.8f, 26, W_BOLD, C_TEXT(0.78f));
      float cy = y + ROW_TITLE_H;
      if (!r->n) {
        /* placeholders while the first rows load */
        for (float cx = ROW_LEFT; cx < SC_DESIGN_W; cx += step)
          gfx_rect(cx, cy, r->wide ? WIDE_W : CARD_W, r->wide ? WIDE_H : CARD_H, 12, C_TEXT(0.055f));
      }
      int first = (int)(r->scroll / step);
      for (int c = first > 0 ? first - 1 : 0; c < r->n; c++) {
        float cx = ROW_LEFT + c * step - r->scroll;
        if (cx > SC_DESIGN_W) break;
        ui_card(&r->cards[c], cx, cy, r->wide, is_cur && c == r->col);
        if (r->ranked && c < 10) draw_rank(c + 1, cx, cy - (is_cur && c == r->col ? 6 : 0));
      }
    }
    y += h;
  }
  gfx_pop_clip();
}

/* ---------- the screen ---------- */
static void enter(void) {
  if (!loaded) load();
}

static void key(app_key k) {
  last_key = clock_now;
  if (nav_has_focus()) {
    if (nav_key(k) == NAV_LEAVE) app_quit();
    return;
  }
  if (at_featured()) {
    int n = featured_count();
    if (k == K_RIGHT) featured_at = (featured_at + 1) % n;
    else if (k == K_LEFT && featured_at > 0) featured_at--;
    else if (k == K_LEFT) nav_focus(NAV_HOME);
    else if (k == K_DOWN) top = !first_row();
    featured_since = clock_now;
    return;
  }
  row *r = cur < nrows ? &rows[cur] : NULL;
  if (k == K_RIGHT && r && r->col < r->n - 1) r->col++;
  else if (k == K_LEFT && r && r->col > 0) r->col--;
  else if (k == K_LEFT) nav_focus(NAV_HOME);
  else if (k == K_DOWN) move_row(1);
  else if (k == K_UP && !move_row(-1)) top = 1;
  else if (k == K_BACK) {
    top = 1;
    first_row();
  }
}

static void frame(double t, float dt) {
  clock_now = t;
  if (!loaded && t - asked_at > RETRY_MS) load();
  int featured = at_featured();
  /* the rows below the featured titles start with the first one that has titles */
  if (!featured && cur < nrows && !rows[cur].n) move_row(1);
  if (featured && t - featured_since > FEATURED_EVERY_MS && t - last_key > FEATURED_EVERY_MS) {
    featured_at = (featured_at + 1) % featured_count();
    featured_since = t;
  }
  follow_hero();
  ask_hero_meta();
  draw_backdrop(featured, dt);
  draw_hero(featured);
  draw_rows(featured, dt);
  nav_draw(NAV_HOME, dt);
}

const screen home_screen = {enter, key, frame};
