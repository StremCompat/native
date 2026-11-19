/* Home and the Library. Home: the featured titles at the top (a large hero that changes every
   8 s), then the rows: Continue watching, new episodes, the library and the addons' catalogs.
   Below the top the hero is the compact one and shows the focused title. The Library: the same
   hero over the films and series added to it. Measures are the web interface's (board.css). */
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

#define FEATURED_HIT 1000 /* the pointer's ids of the featured button and its dots */
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
  int wide, ranked, answered, folders; /* folders: a row of Collections folders */
  card *cards;
  int n, col;
  float scroll; /* shown horizontal offset, moving toward the focused card */
} row;

/* the title the hero shows, with what its metadata adds */
typedef struct {
  char *id, *name, *where, *backdrop, *logo, *genres, *info, *rating, *description;
} hero_info;

/* Home and the Library are the same kind of screen (web: board.js): a hero over rows. Each has
   its own state; B is the one being shown or answered. */
typedef struct {
  const char *path;  /* where its rows come from */
  int featured_ok;   /* Home's featured titles at the top; the library has none */
  int labeled;       /* the library names its titles on the cards */
  nav_item section;
  row *rows;
  int nrows, loaded, gen;
  int cur;           /* the focused row */
  int top;           /* the focus is on the featured titles */
  int featured_at;
  double featured_since, last_key, hero_changed, asked_at;
  int hero_asked;
  int waiting; /* a request for the rows is on its way */
  hero_info hero;
  const card *hero_from; /* the card it came from: one title can be on two rows */
  float rows_y;      /* shown vertical offset of the rows */
  backdrop bg;
  double clock_now;
} board;

static board home_board = {.path = "/app/home", .featured_ok = 1, .section = NAV_HOME, .top = 1};
static board library_board = {.path = "/app/library", .labeled = 1, .section = NAV_LIBRARY};
static board *B = &home_board;
static int requests; /* numbers the requests of both boards, to know whose an answer is */

/* the board a request number belongs to, now the one answered */
static int answered_by(int number) {
  if (home_board.gen == number) B = &home_board;
  else if (library_board.gen == number) B = &library_board;
  else return 0;
  return 1;
}

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
  for (int r = 0; r < B->nrows; r++) {
    for (int i = 0; i < B->rows[r].n; i++) card_clear(&B->rows[r].cards[i]);
    free(B->rows[r].cards);
    free(B->rows[r].title);
    free(B->rows[r].catalog);
  }
  free(B->rows);
  B->rows = NULL;
  B->nrows = 0;
}

static void read_cards(row *r, const jval *items) {
  r->n = json_len(items);
  r->cards = calloc((size_t)(r->n ? r->n : 1), sizeof(card));
  for (int i = 0; i < r->n; i++) card_read(&r->cards[i], json_at(items, i));
}

static void catalog_answered(const jval *answer, void *user) {
  intptr_t tag = (intptr_t)user;
  int index = (int)(tag % 1000);
  if (!answered_by((int)(tag / 1000)) || index >= B->nrows) return; /* loaded again meanwhile */
  row *r = &B->rows[index];
  r->answered = 1;
  if (answer) read_cards(r, json_get(answer, "items"));
}

static void home_answered(const jval *answer, void *user) {
  if (!answered_by((int)(intptr_t)user)) return;
  /* asked again only once this one is over: on a slow TV the answer can take longer than the
     retry pause, and a new request each time threw the coming answer away and kept the server busy */
  B->waiting = 0;
  B->asked_at = B->clock_now;
  if (!answer) return;
  const jval *list = json_get(answer, "rows");
  rows_clear();
  B->nrows = json_len(list);
  B->rows = calloc((size_t)(B->nrows ? B->nrows : 1), sizeof(row));
  for (int i = 0; i < B->nrows; i++) {
    const jval *v = json_at(list, i), *cat = json_get(v, "catalog");
    row *r = &B->rows[i];
    r->title = copy(json_str(json_get(v, "title"), ""));
    r->wide = json_bool(json_get(v, "wide"), 0);
    r->ranked = json_bool(json_get(v, "ranked"), 0);
    r->folders = json_bool(json_get(v, "collection"), 0);
    if (cat->type == J_OBJ) {
      sbuf path = {0};
      sb_add(&path, "/app/catalog?url=");
      sb_add_url(&path, json_str(json_get(cat, "url"), ""));
      sb_add(&path, "&key=");
      sb_add_url(&path, json_str(json_get(cat, "key"), ""));
      sb_add(&path, "&type=");
      sb_add_url(&path, json_str(json_get(cat, "type"), ""));
      r->catalog = path.s;
      api_get(r->catalog, catalog_answered, (void *)(intptr_t)(B->gen * 1000 + i));
    } else {
      r->answered = 1;
      read_cards(r, json_get(v, "items"));
    }
  }
  B->loaded = 1;
}

void home_reload(void) {
  B = &home_board;
  B->loaded = 0;
  B->asked_at = -RETRY_MS;
  B->waiting = 0;
  rows_clear();
  B->top = 1;
  B->cur = 0;
}

static void load(void) {
  B->gen = ++requests;
  B->loaded = 0;
  B->waiting = 1;
  B->asked_at = B->clock_now;
  api_get(B->path, home_answered, (void *)(intptr_t)B->gen);
}

/* ---------- what is shown ---------- */
static int shown(const row *r, int index) { return r->n > 0 || (!r->answered && index < 3); }

/* the first catalog with more than two titles gives the featured ones; wide rows never do */
static const row *featured_row(void) {
  for (int i = 0; i < B->nrows; i++) {
    if (!B->rows[i].catalog || B->rows[i].wide) continue;
    if (!B->rows[i].answered) return NULL;
    if (B->rows[i].n > 2) return &B->rows[i];
  }
  return NULL;
}

static int featured_count(void) {
  const row *f = featured_row();
  return f ? (f->n < FEATURED_MAX ? f->n : FEATURED_MAX) : 0;
}

static int at_featured(void) { return B->featured_ok && B->top && featured_count() > 2; }

static float row_height(const row *r) { return ROW_TITLE_H + (r->wide ? WIDE_H : CARD_H) + ROW_BOTTOM; }

/* a row of folders steps by its first tile's shape */
static float card_step(const row *r) {
  if (r->folders && r->n) return ui_folder_width(&r->cards[0]) + CARD_GAP;
  return (r->wide ? WIDE_W : CARD_W) + CARD_GAP;
}

/* the rows scroll so the focused one sits at the top of their area */
static float rows_offset(void) {
  float y = 0;
  for (int i = 0; i < B->cur && i < B->nrows; i++)
    if (shown(&B->rows[i], i)) y += row_height(&B->rows[i]);
  return y;
}

/* the card the focus is on, if the rows have it */
static const card *focused_card(void) {
  if (B->cur >= B->nrows || !B->rows[B->cur].n) return NULL;
  return &B->rows[B->cur].cards[B->rows[B->cur].col];
}

static const card *hero_card(void) {
  if (at_featured()) return &featured_row()->cards[B->featured_at % featured_count()];
  return focused_card();
}

/* moves the focus to the next row with titles in that direction; 0 when there is none */
static int move_row(int dir) {
  for (int i = B->cur + dir; i >= 0 && i < B->nrows; i += dir)
    if (B->rows[i].n > 0) {
      B->cur = i;
      return 1;
    }
  return 0;
}

/* the focus to the first row with titles; 0 when none has any yet */
static int first_row(void) {
  B->cur = -1;
  if (move_row(1)) return 1;
  B->cur = 0;
  return 0;
}

static void meta_answered(const jval *answer, void *user) {
  const jval *m = json_get(answer, "meta");
  if (!answered_by((int)(intptr_t)user) || !B->hero.id || strcmp(B->hero.id, json_str(json_get(m, "id"), ""))) return;
  free(B->hero.description);
  B->hero.description = copy(json_str(json_get(m, "description"), NULL));
  /* Action  •  Crime  •  Drama */
  sbuf g = {0};
  const jval *genres = json_get(m, "genres");
  for (int i = 0; i < json_len(genres) && i < 3; i++) {
    if (i) sb_add(&g, "  \xE2\x80\xA2  ");
    sb_add(&g, json_str(json_at(genres, i), ""));
  }
  free(B->hero.genres);
  B->hero.genres = g.s;
  /* 109 min   2026 */
  sbuf info = {0};
  const char *runtime = json_str(json_get(m, "runtime"), ""), *year = json_str(json_get(m, "releaseInfo"), "");
  if (!*year) year = json_str(json_get(m, "year"), "");
  sb_addf(&info, "%s%s%s", runtime, *runtime && *year ? "   " : "", year);
  free(B->hero.info);
  B->hero.info = info.s;
  free(B->hero.rating);
  B->hero.rating = copy(json_str(json_get(m, "imdbRating"), NULL));
}

/* the hero follows its title: the name, picture and line at once, the rest when it rests */
static void follow_hero(void) {
  const card *c = hero_card();
  if (!c || !c->id || (c == B->hero_from && B->hero.id && !strcmp(B->hero.id, c->id))) return;
  B->hero_from = c;
  hero_clear(&B->hero);
  B->hero.id = str_dup(c->id);
  B->hero.name = copy(c->name);
  B->hero.where = copy(c->subtitle);
  B->hero.backdrop = copy(c->backdrop);
  /* a folder: its logo, and how many lists it holds in place of a description */
  if (c->logo) B->hero.logo = str_dup(c->logo);
  if (c->type && !strcmp(c->type, "folder")) B->hero.description = copy(c->description);
  if (!strncmp(c->id, "tt", 2)) {
    char logo[256];
    snprintf(logo, sizeof logo, "https://images.metahub.space/logo/medium/%s/img", c->id);
    B->hero.logo = str_dup(logo);
  }
  B->hero_changed = B->clock_now;
  B->hero_asked = 0;
  backdrop_set(&B->bg, B->hero.backdrop);
}

static void ask_hero_meta(void) {
  const card *c = hero_card();
  if (B->hero_asked || !c || !c->type || !strcmp(c->type, "folder") || B->clock_now - B->hero_changed < HERO_WAIT_MS) return;
  B->hero_asked = 1;
  sbuf path = {0};
  sb_add(&path, "/app/meta?type=");
  sb_add_url(&path, c->type);
  sb_add(&path, "&id=");
  sb_add_url(&path, c->id);
  api_get(path.s, meta_answered, (void *)(intptr_t)B->gen);
  sb_free(&path);
}

/* ---------- drawing ---------- */
static void draw_backdrop(int featured, float dt) {
  /* #bg: the right part of the screen, or more of it with the featured titles */
  backdrop_draw(&B->bg, featured ? 346 : 730, featured ? 929 : 670, featured ? 0.92f : 0.86f, dt);
  static const shade_stop down[] = {{0.26f, 0}, {0.38f, 0.25f}, {0.49f, 0.6f}, {0.57f, 0.88f}, {0.61f, 1}};
  static const shade_stop right[] = {{0.38f, 1}, {0.44f, 0.9f}, {0.51f, 0.66f}, {0.59f, 0.36f}, {0.67f, 0.14f}, {0.76f, 0}};
  static const shade_stop down_f[] = {{0.40f, 0}, {0.58f, 0.35f}, {0.76f, 0.85f}, {0.86f, 1}};
  static const shade_stop right_f[] = {{0.18f, 1}, {0.26f, 0.86f}, {0.38f, 0.55f}, {0.52f, 0.2f}, {0.64f, 0}};
  ui_shade(featured ? down_f : down, featured ? 4 : 5, 1);
  ui_shade(featured ? right_f : right, featured ? 5 : 6, 0);
}

static void draw_hero(int featured) {
  float x = ROW_LEFT, y = featured ? 340 : 70, w = featured ? 998 : 691;
  float logo_w = featured ? 560 : 420, logo_h = featured ? 160 : 122;
  /* the logo when it came, else the name; while the logo is on its way, nothing blinks in */
  const sc_tex *logo = img_get(B->hero.logo, (int)logo_w, NULL);
  if (logo) {
    float s = logo_w / logo->w < logo_h / logo->h ? logo_w / logo->w : logo_h / logo->h;
    gfx_image(logo, x, y + logo_h - logo->h * s, logo->w * s, logo->h * s, 0, 1, STRETCH);
  } else if (B->hero.name && (!B->hero.logo || img_failed(B->hero.logo, (int)logo_w))) {
    int long_name = strlen(B->hero.name) > 32;
    float size = featured ? (long_name ? 56 : 76) : (long_name ? 44 : 60), lh = size * 1.04f;
    float used = text_draw_wrap(B->hero.name, 0, 0, w, size, W_EXTRABOLD, 0, 2, lh);
    text_draw_wrap(B->hero.name, x, y + logo_h - used, w, size, W_EXTRABOLD, C_WHITE, 2, lh);
  }
  y += logo_h;
  if (featured) {
    y += 14;
    ui_line(B->hero.genres, x, y, 34, 26, W_SEMIBOLD, C_TEXT(0.62f));
    y += 34;
  }
  /* where the viewer stopped, the length and year, the rating */
  float size = featured ? 28 : 24, box = featured ? 38 : 32, mx = x;
  y += 16;
  if (B->hero.where) mx += ui_line(B->hero.where, mx, y, box, size, W_BOLD, C_WHITE) + 24;
  if (B->hero.info) mx += ui_line(B->hero.info, mx, y, box, size, W_REGULAR, C_TEXT(0.72f)) + 24;
  ui_rating(B->hero.rating, mx, y, box, size, 16, C_TEXT(0.72f));
  y += box + 12;
  float ds = featured ? 30 : 25, lh = ds * 1.45f;
  if (B->hero.description)
    text_draw_wrap(B->hero.description, x, y + (lh - text_line_height(ds)) / 2, w, ds, W_REGULAR, C_TEXT(0.78f), 2, lh);
  if (!featured) return;
  /* See details, and the dots saying which featured title this is */
  y += 2 * lh + 28;
  const char *label = tx("seeDetails");
  float bw = text_width(label, 26, W_BOLD) + 72;
  gfx_rect(x, y, bw, 64, 32, nav_has_focus() ? C_TEXT(0.16f) : C_WHITE);
  hit_add(FEATURED_HIT, x, y, bw, 64);
  ui_line(label, x + 36, y, 64, 26, W_BOLD, nav_has_focus() ? C_WHITE : C_BG);
  float dx = 1676, dy = y + 64 - 27 - 10;
  for (int i = 0; i < featured_count(); i++) {
    float dw = i == B->featured_at ? 30 : 10;
    gfx_rect(dx, dy, dw, 10, 5, i == B->featured_at ? C_WHITE : C_TEXT(0.35f));
    hit_add(FEATURED_HIT + 1 + i, dx - 4, dy - 12, dw + 8, 34);
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
  B->rows_y = ui_approach(B->rows_y, featured ? 0 : rows_offset(), dt, 70);
  gfx_push_clip(128, area - 20, SC_DESIGN_W - 128, SC_DESIGN_H - area + 20);
  float y = area - B->rows_y;
  for (int i = 0; i < B->nrows; i++) {
    row *r = &B->rows[i];
    if (!shown(r, i)) continue;
    float h = row_height(r), step = card_step(r);
    if (y + h > area - 20 && y < SC_DESIGN_H) {
      int is_cur = i == B->cur && !featured && !nav_has_focus();
      r->scroll = ui_approach(r->scroll, ui_row_offset(r->col, step, CONTENT_W), dt, 70);
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
        if (r->folders) {
          ui_folder_tile(&r->cards[c], cx, cy, is_cur && c == r->col);
          continue;
        }
        ui_card(&r->cards[c], cx, cy, r->wide, is_cur && c == r->col);
        if (B->labeled && !r->wide) ui_card_name(&r->cards[c], cx, cy, is_cur && c == r->col);
        if (r->ranked && c < 10) draw_rank(c + 1, cx, cy - (is_cur && c == r->col ? 6 : 0));
      }
    }
    y += h;
  }
  gfx_pop_clip();
}

/* ---------- the screen ---------- */
static void enter(void) {
  if (!B->loaded) load();
}

/* the library changes from anywhere (a title added, a film finished): built again each time */
static void library_enter(void) {
  B = &library_board;
  rows_clear();
  B->cur = 0;
  load();
}

static void key(app_key k) {
  B->last_key = B->clock_now;
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (at_featured()) {
    int n = featured_count();
    if (k == K_RIGHT) B->featured_at = (B->featured_at + 1) % n;
    else if (k == K_LEFT && B->featured_at > 0) B->featured_at--;
    else if (k == K_LEFT) nav_focus(B->section);
    else if (k == K_DOWN) B->top = !first_row();
    else if (k == K_OK) title_open(hero_card()->type, hero_card()->id);
    B->featured_since = B->clock_now;
    return;
  }
  row *r = B->cur < B->nrows ? &B->rows[B->cur] : NULL;
  if (k == K_RIGHT && r && r->col < r->n - 1) r->col++;
  else if (k == K_LEFT && r && r->col > 0) r->col--;
  else if (k == K_LEFT) nav_focus(B->section);
  else if (k == K_DOWN) move_row(1);
  else if (k == K_UP && !move_row(-1)) B->top = 1;
  else if (k == K_OK && focused_card() && focused_card()->type && !strcmp(focused_card()->type, "folder"))
    folder_open(focused_card()->id);
  else if (k == K_OK && focused_card() && focused_card()->id && focused_card()->type)
    title_open(focused_card()->type, focused_card()->id);
  else if (k == K_MENU && focused_card() && focused_card()->id && strcmp(focused_card()->type ? focused_card()->type : "", "folder"))
    card_menu_open(focused_card(), focused_card()->cw);
  else if (k == K_BACK) {
    B->top = 1;
    first_row();
  }
}

static void frame(double t, float dt) {
  B->clock_now = t;
  if (!B->loaded && !B->waiting && t - B->asked_at > RETRY_MS) load();
  int featured = at_featured();
  /* the rows below the featured titles start with the first one that has titles */
  if (!featured && B->cur < B->nrows && !B->rows[B->cur].n) move_row(1);
  if (featured && t - B->featured_since > FEATURED_EVERY_MS && t - B->last_key > FEATURED_EVERY_MS) {
    B->featured_at = (B->featured_at + 1) % featured_count();
    B->featured_since = t;
  }
  follow_hero();
  ask_hero_meta();
  draw_backdrop(featured, dt);
  draw_hero(featured);
  draw_rows(featured, dt);
  nav_draw(B->section, dt);
}

/* Home and the Library share everything but their state */
static void home_enter(void) {
  B = &home_board;
  enter();
}
static void home_key(app_key k) {
  B = &home_board;
  key(k);
}
/* The first time, Home appears whole: a splash over it until its first rows and their pictures
   came (cards no longer pop in one by one), at most SPLASH_MAX_MS */
#define SPLASH_MAX_MS 2500.0
#define SPLASH_FADE_MS 300.0
static double splash_from = -1, splash_done = -1;
static sc_tex splash_logo;

static int first_rows_ready(void) {
  if (!B->loaded) return 0;
  for (int i = 0, shown_rows = 0; i < B->nrows && shown_rows < 2; i++) {
    if (!B->rows[i].answered) return 0;
    if (B->rows[i].n) shown_rows++;
  }
  return 1;
}

static void draw_splash(double t) {
  if (splash_from < 0) splash_from = t;
  if (splash_done < 0 && ((first_rows_ready() && !img_waiting()) || t - splash_from > SPLASH_MAX_MS)) splash_done = t;
  float a = splash_done >= 0 ? 1 - (float)((t - splash_done) / SPLASH_FADE_MS) : 1;
  if (a <= 0) return;
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, (int)(255 * a)));
  if (!splash_logo.id) splash_logo = img_load_file(app_asset_dir("logo.png"), 0);
  if (splash_logo.id) gfx_image(&splash_logo, (SC_DESIGN_W - 120) / 2.0f, (SC_DESIGN_H - 120) / 2.0f, 120, 120, 0, a, 0);
}

static void home_frame(double t, float dt) {
  B = &home_board;
  frame(t, dt);
  draw_splash(t);
}
static void library_key(app_key k) {
  B = &library_board;
  key(k);
}
static void library_frame(double t, float dt) {
  B = &library_board;
  frame(t, dt);
  if (B->loaded && !B->nrows) {
    const char *title = tx("emptyLibraryTitle"), *hint = tx("emptyLibraryHint");
    text_draw(title, 176, 420, 40, W_BOLD, C_WHITE);
    text_draw_wrap(hint, 176, 480, 1200, 26, W_REGULAR, C_TEXT(0.72f), 3, 36);
  }
}

static void point(float x, float y, int press) {
  /* nothing under the splash is clickable yet */
  if (B == &home_board && splash_done < 0) return;
  if (nav_point(x, y, press) || x < ROW_LEFT) return;
  /* the featured title's button opens it, a dot shows that title (only clicks: the focus moving up
     to it would move Home under the pointer) */
  int id = hit_at(x, y);
  if (id >= FEATURED_HIT) {
    if (!press) return;
    if (nav_has_focus()) nav_key(K_RIGHT);
    B->top = 1;
    B->featured_since = B->clock_now;
    if (id > FEATURED_HIT) B->featured_at = id - FEATURED_HIT - 1;
    else key(K_OK);
    return;
  }
  float area = at_featured() ? ROWS_TOP_FEATURED : ROWS_TOP;
  if (y < area - 20 || y >= SC_DESIGN_H) return;
  float top = area - B->rows_y;
  for (int i = 0; i < B->nrows; i++) {
    row *r = &B->rows[i];
    if (!shown(r, i)) continue;
    float dy = y - top - ROW_TITLE_H;
    top += row_height(r);
    if (!r->n || dy < 0 || dy >= (r->wide ? WIDE_H : CARD_H)) continue;
    float step = card_step(r), dx = x - ROW_LEFT + r->scroll;
    int col = (int)(dx / step);
    if (col >= r->n) return;
    float width = r->folders ? ui_folder_width(&r->cards[col]) : r->wide ? WIDE_W : CARD_W,
          height = r->folders ? ui_folder_height(&r->cards[col]) : r->wide ? WIDE_H : CARD_H;
    if (dx - col * step >= width || dy >= height) return;
    /* passing over another row (or from the featured title) would move Home under the pointer:
       only a click goes there */
    if (!press && (at_featured() || i != B->cur || nav_has_focus())) return;
    if (nav_has_focus()) nav_key(K_RIGHT);
    B->top = 0;
    B->cur = i;
    r->col = col;
    if (press) key(K_OK);
    return;
  }
}

static void home_point(float x, float y, int press) { B = &home_board; point(x, y, press); }
static void library_point(float x, float y, int press) { B = &library_board; point(x, y, press); }

const screen home_screen = {home_enter, home_key, home_frame, NULL, 0, home_point};
const screen library_screen = {library_enter, library_key, library_frame, NULL, 0, library_point};
