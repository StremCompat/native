/* Search: a keyboard of letters and digits at the left, the last searches or the results at the
   right, and a preview of the focused title. Results come as the viewer types (/app/search).
   Measures are the web interface's (screens.css #sWrap). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "nav.h"
#include "net.h"
#include "qr.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TOP 58.0f
#define KEY_W 78.0f
#define KEY_H 62.0f
#define KEY_GAP 8.0f
#define SIDE_X (LEFT + 576)
#define ROWS_X (LEFT + 564)
#define ROWS_Y (TOP + 156)
#define LIVE_MS 650.0
#define MAX_ROWS 2
#define MAX_RECENT 8

/* 6 x 6 letters and digits, then space, delete and clear two columns wide (web: SEARCH_KEYS) */
static const char KEYS[] = "abcdefghijklmnopqrstuvwxyz1234567890";
enum { K_SPACE = 36, K_DEL, K_CLEAR, KEY_COUNT };
enum { Z_KEYS, Z_RECENT, Z_ROWS };

typedef struct {
  char *title;
  card *items;
  int n, col;
  float scroll;
} result_row;

static char query[128], asked[128], empty[200];
static int zone, key_at, recent_at, row_at, gen, searching;
static result_row rows[MAX_ROWS];
static int nrows;
static char recent[MAX_RECENT][128];
static int nrecent;
static double typed_at, clock_now;
static float rows_y;

static void clear_rows(void) {
  for (int r = 0; r < nrows; r++) {
    for (int i = 0; i < rows[r].n; i++) card_clear(&rows[r].items[i]);
    free(rows[r].items);
    free(rows[r].title);
  }
  nrows = 0;
}

static void answered(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  searching = 0;
  clear_rows();
  const jval *list = json_get(answer, "rows"), *rec = json_get(answer, "recent");
  for (int r = 0; r < json_len(list) && nrows < MAX_ROWS; r++) {
    const jval *v = json_at(list, r), *items = json_get(v, "items");
    result_row *row = &rows[nrows++];
    memset(row, 0, sizeof *row);
    row->title = str_dup(json_str(json_get(v, "title"), ""));
    row->n = json_len(items);
    row->items = calloc((size_t)row->n + 1, sizeof(card));
    for (int i = 0; i < row->n; i++) card_read(&row->items[i], json_at(items, i));
  }
  snprintf(empty, sizeof empty, "%s", json_str(json_get(answer, "empty"), ""));
  nrecent = 0;
  for (int i = 0; i < json_len(rec) && nrecent < MAX_RECENT; i++)
    snprintf(recent[nrecent++], sizeof recent[0], "%s", json_str(json_at(rec, i), ""));
}

/* live: while typing; a submitted search is also kept among the recent ones */
static void run(int live) {
  snprintf(asked, sizeof asked, "%s", query);
  gen++;
  searching = query[0] != 0;
  row_at = 0;
  sbuf p = {0};
  sb_add(&p, "/app/search?q=");
  sb_add_url(&p, query);
  if (live) sb_add(&p, "&live=1");
  api_get(p.s, answered, (void *)(intptr_t)gen);
  sb_free(&p);
}

/* ---------- typing on the phone: the TV's page for a line of text (server/phone-link.js), asked
   every two seconds while Search shows; what comes searches as if typed and submitted ---------- */
#define PHONE_POLL_MS 2000.0
static char phone_qr[QR_MAX_SIZE * QR_MAX_SIZE + 1];
static int phone_size, phone_asking, phone_gen;
static double phone_polled;

static void phone_started(net_reply *r, void *user) {
  if ((intptr_t)user != phone_gen) return;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const jval *rows = json_get(json_root(d), "qr");
  int n = json_len(rows);
  phone_size = 0;
  if (n > 0 && n <= QR_MAX_SIZE) {
    for (int i = 0; i < n; i++) memcpy(phone_qr + i * n, json_str(json_at(rows, i), ""), (size_t)n);
    phone_qr[n * n] = 0;
    phone_size = n;
  }
  json_free(d);
}

static void phone_start(void) {
  phone_size = 0;
  net_request("/app/phone/start", "{\"kind\":\"text\"}", NET_HIGH, NULL, phone_started, (void *)(intptr_t)++phone_gen);
}

static void run(int live);

static void phone_came(const jval *answer, void *user) {
  if ((intptr_t)user != phone_gen) return;
  phone_asking = 0;
  const char *value = json_str(json_get(answer, "value"), NULL);
  if (value && *value) {
    snprintf(query, sizeof query, "%s", value);
    run(0);
    phone_start();
  } else if (json_bool(json_get(answer, "closed"), 0)) {
    phone_size = 0;
  }
}

static void phone_tick(double t) {
  if (!phone_size || phone_asking || t - phone_polled < PHONE_POLL_MS) return;
  phone_polled = t;
  phone_asking = 1;
  api_get("/app/phone/poll", phone_came, (void *)(intptr_t)phone_gen);
}

static void draw_phone(void) {
  if (!phone_size) return;
  float box = 170, x = SC_DESIGN_W - 48 - box, y = SC_DESIGN_H - 60 - box;
  ui_qr(phone_qr, phone_size, x, y, box);
  const char *hint = tx("phoneSearchHint");
  text_draw_wrap(hint, x - 380, y + box / 2 - 30, 350, 22, W_REGULAR, C_TEXT(0.72f), 2, 30);
}

static void press(void) {
  size_t n = strlen(query);
  if (key_at < K_SPACE && n + 1 < sizeof query) query[n] = KEYS[key_at], query[n + 1] = 0;
  else if (key_at == K_SPACE && n && n + 1 < sizeof query) query[n] = ' ', query[n + 1] = 0;
  else if (key_at == K_DEL && n) query[n - 1] = 0;
  else if (key_at == K_CLEAR) query[0] = 0;
  typed_at = clock_now;
}

/* the key grid: row and column of a key, and the key at a place */
static int key_row(int i) { return i < 36 ? i / 6 : 6; }
static int key_col(int i) { return i < 36 ? i % 6 : (i - 36) * 2; }
static int key_at_place(int r, int c) { return r < 6 ? r * 6 + c : 36 + (c / 2 < 2 ? c / 2 : 2); }

static result_row *current_row(void) { return row_at < nrows ? &rows[row_at] : NULL; }

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (k == K_BACK) {
    app_show(&home_screen);
    return;
  }
  if (zone == Z_KEYS) {
    int r = key_row(key_at), c = key_col(key_at);
    if (k == K_UP && r > 0) key_at = key_at_place(r - 1, c);
    else if (k == K_DOWN && r < 6) key_at = key_at_place(r + 1, c);
    else if (k == K_LEFT && c > 0) key_at = key_at_place(r, c - (r == 6 ? 2 : 1));
    else if (k == K_LEFT) nav_focus(NAV_SEARCH);
    else if (k == K_RIGHT && c < 5 - (r == 6)) key_at = key_at_place(r, c + (r == 6 ? 2 : 1));
    else if (k == K_RIGHT && nrows) zone = Z_ROWS;
    else if (k == K_RIGHT && !query[0] && nrecent) zone = Z_RECENT;
    else if (k == K_OK) press();
  } else if (zone == Z_RECENT) {
    if (k == K_UP && recent_at > 0) recent_at--;
    else if (k == K_DOWN && recent_at < nrecent - 1) recent_at++;
    else if (k == K_LEFT) zone = Z_KEYS;
    else if (k == K_OK) {
      snprintf(query, sizeof query, "%s", recent[recent_at]);
      run(0);
    }
  } else {
    result_row *row = current_row();
    if (k == K_LEFT && row && row->col > 0) row->col--;
    else if (k == K_LEFT) zone = Z_KEYS;
    else if (k == K_RIGHT && row && row->col < row->n - 1) row->col++;
    else if (k == K_DOWN && row_at < nrows - 1) row_at++;
    else if (k == K_UP && row_at > 0) row_at--;
    else if (k == K_MENU && row && row->n) card_menu_open(&row->items[row->col], 0);
    else if (k == K_OK && row && row->n) {
      run(0); /* remembered once a result is opened */
      title_open_card(&row->items[row->col]);
    }
  }
}

static void text(const char *utf8) {
  size_t n = strlen(query), add = strlen(utf8);
  if (n + add < sizeof query) memcpy(query + n, utf8, add + 1);
  typed_at = clock_now;
}

/* ---------- drawing ---------- */
static void draw_keys(void) {
  /* the box with what was typed */
  gfx_rect(LEFT, TOP, 528, 86, 14, C_TEXT(0.10f));
  const char *shown = query[0] ? query : tx("searchPlaceholder");
  text_draw_fit(shown, LEFT + 32, TOP + (86 - text_line_height(34)) / 2, 464, 34, W_REGULAR,
                query[0] ? C_WHITE : C_TEXT(0.45f));
  float ky = TOP + 110;
  for (int i = 0; i < KEY_COUNT; i++) {
    int r = key_row(i), c = key_col(i), focused = zone == Z_KEYS && key_at == i && !nav_has_focus();
    float w = i >= K_SPACE ? KEY_W * 2 + KEY_GAP : KEY_W, x = LEFT + c * (KEY_W + KEY_GAP), y = ky + r * (KEY_H + KEY_GAP);
    gfx_rect(x, y, w, KEY_H, 10, focused ? C_WHITE : C_TEXT(0.08f));
    hit_add(Z_KEYS * 1000 + i, x, y, w, KEY_H);
    char letter[2] = {KEYS[i < 36 ? i : 0], 0};
    const char *label = i < 36 ? letter : i == K_SPACE ? tx("keySpace") : i == K_DEL ? tx("keyDelete") : tx("keyClear");
    float size = i < 36 ? 28 : 22, lw = text_width(label, size, W_SEMIBOLD);
    ui_line(label, x + (w - lw) / 2, y, KEY_H, size, W_SEMIBOLD, focused ? C_BG : C_TEXT(0.85f));
  }
}

static void draw_recent(void) {
  float y = TOP + 22 + 50;
  if (!nrecent) return;
  char label[96];
  utf8_upper(label, sizeof label, tx("recentSearches"));
  text_draw(label, SIDE_X + 2, y, 21, W_BOLD, C_TEXT(0.7f));
  y += 40;
  for (int i = 0; i < nrecent; i++) {
    int f = zone == Z_RECENT && i == recent_at;
    float w = text_width(recent[i], 22, f ? W_BOLD : W_REGULAR) + 48;
    gfx_rect(SIDE_X, y, w, 46, 23, f ? C_WHITE : C_TEXT(0.07f));
    hit_add(Z_RECENT * 1000 + i, SIDE_X, y, w, 46);
    ui_line(recent[i], SIDE_X + 24, y, 46, 22, f ? W_BOLD : W_REGULAR, f ? SC_RGBA(12, 11, 17, 255) : C_TEXT(0.72f));
    y += 46 + 14;
  }
}

/* the focused result's name, year, length and synopsis, at the top right */
static void draw_preview(void) {
  result_row *row = current_row();
  if (zone != Z_ROWS || !row || !row->n) return;
  const card *c = &row->items[row->col];
  float x = SC_DESIGN_W - 48 - 720 + 32, y = TOP + 8;
  gfx_rect(x - 32, TOP, 1, 145, 0, C_TEXT(0.10f));
  text_draw_fit(c->name ? c->name : "", x, y, 670, 34, W_BOLD, C_WHITE);
  if (c->info) text_draw_fit(c->info, x, y + 46, 670, 21, W_BOLD, C_TEXT(0.8f));
  if (c->description) text_draw_wrap(c->description, x, y + 78, 670, 20, W_REGULAR, C_TEXT(0.72f), 2, 27);
}

static void draw_rows(float dt) {
  float target = 0;
  for (int r = 0; r < row_at; r++) target += 59 + CARD_H + 26;
  rows_y = ui_approach(rows_y, zone == Z_ROWS ? target : 0, dt, 70);
  gfx_push_clip(ROWS_X - 20, ROWS_Y - 14, SC_DESIGN_W - ROWS_X + 20, SC_DESIGN_H - ROWS_Y + 14);
  float y = ROWS_Y - rows_y;
  for (int r = 0; r < nrows; r++) {
    result_row *row = &rows[r];
    float step = CARD_W + CARD_GAP;
    row->scroll = ui_approach(row->scroll, ui_row_offset(row->col, step, SC_DESIGN_W - ROWS_X), dt, 70);
    text_draw_fit(row->title, ROWS_X + 2, y + 14, 600, 26, W_BOLD, C_TEXT(0.78f));
    for (int i = 0; i < row->n; i++) {
      float x = ROWS_X + i * step - row->scroll;
      if (x + CARD_W < ROWS_X - 20) continue;
      if (x > SC_DESIGN_W) break;
      int f = zone == Z_ROWS && r == row_at && i == row->col;
      ui_card(&row->items[i], x, y + 59, 0, f);
      ui_card_name(&row->items[i], x, y + 59, f);
      hit_add(Z_ROWS * 1000 + r * 100 + i, x < ROWS_X ? ROWS_X : x, y + 59, CARD_W, CARD_H);
    }
    y += 59 + CARD_H + 26;
  }
  gfx_pop_clip();
}

static void frame(double t, float dt) {
  clock_now = t;
  /* what is typed is searched once the typing rests */
  if (typed_at && t - typed_at > LIVE_MS) {
    typed_at = 0;
    if (strcmp(query, asked)) run(1);
  }
  draw_keys();
  phone_tick(t);
  if (!query[0]) {
    text_draw(tx("searchHint"), SIDE_X, TOP + 22, 25, W_REGULAR, C_TEXT(0.72f));
    draw_recent();
    draw_phone();
  } else if (searching) {
    text_draw(tx("searching"), SIDE_X, TOP + 22, 25, W_REGULAR, C_TEXT(0.72f));
  } else if (!nrows && empty[0]) {
    text_draw_wrap(empty, SIDE_X, TOP + 22, 1000, 25, W_REGULAR, C_TEXT(0.72f), 2, 34);
  }
  if (query[0]) draw_rows(dt);
  draw_preview();
  nav_draw(NAV_SEARCH, dt);
}

static void enter(void) {
  if (!asked[0] && !nrecent) run(1); /* the recent searches */
  phone_start();
}

/* The pointer: passing over a key or a recent search takes the focus, a click presses it; over a
   result of the row in focus it takes the focus (another row would scroll the page under it), and a
   click opens it. */
static void point(float x, float y, int press) {
  if (nav_point(x, y, press)) return;
  int id = hit_at(x, y);
  if (id < 0) return;
  if (nav_has_focus()) nav_key(K_RIGHT);
  int z = id / 1000, at = id % 1000;
  if (z == Z_ROWS) {
    if (at / 100 >= nrows || (!press && (zone != Z_ROWS || at / 100 != row_at))) return;
    row_at = at / 100;
    rows[row_at].col = at % 100;
  } else if (z == Z_RECENT) recent_at = at;
  else key_at = at;
  zone = z;
  if (press) key(K_OK);
}

const screen search_screen = {enter, key, frame, text, 0, point};
