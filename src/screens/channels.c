/* Live channels as IPTV apps show them, like the web screen (channels.js): the categories on the
   left (search, recent, the addons' and the M3U lists' groups, adding a list, managing them) and
   the focused category's channels on the right with what is on now. The server sends the
   categories first and the channels a page at a time (/app/channels), so a list of tens of
   thousands of channels costs the same as a small one; only the rows in view are drawn. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "addr.h"
#include "api.h"
#include "img.h"
#include "nav.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define TOP 56.0f
#define LIST_Y 152.0f
#define CAT_W 420.0f
#define CAT_H 66.0f
#define CAT_STEP 74.0f
#define CH_X (LEFT + CAT_W + 30)
#define CH_H 102.0f
#define CH_STEP 112.0f
#define MAX_CATS 512
#define MAX_LISTS 32
#define PAGE_AHEAD 10

typedef struct {
  char id[96], title[160], q[128];
  int count;
} category;

typedef struct {
  char *key, *name, *logo, *now;
  double start, end;
} channel;

typedef struct {
  char url[400], name[96];
  int count;
  double updated;
} m3u_list;

enum { C_SEARCH, C_RECENT, C_GROUP, C_ADD, C_LISTS };

static category cats[MAX_CATS];
static int kinds[MAX_CATS];
static int ncats, cat_at, cat_top, loaded;
static channel *chans;
static int nchans, cap_chans, ch_at, ch_top, more, loading, on_list, gen, list_gen;
static m3u_list lists[MAX_LISTS];
static int nlists, armed = -1, opening; /* opening: 1 the add panel, 2 the lists, when the screen comes */
static char empty[256];

static char *copy(const jval *v) {
  const char *s = json_str(v, NULL);
  return s && *s ? str_dup(s) : NULL;
}

static void clear_channels(void) {
  for (int i = 0; i < nchans; i++) {
    channel *c = &chans[i];
    free(c->key), free(c->name), free(c->logo), free(c->now);
  }
  nchans = ch_at = ch_top = more = 0;
}

static void add_cat(int kind, const char *id, const char *title, int count) {
  if (ncats >= MAX_CATS) return;
  category *c = &cats[ncats];
  snprintf(c->id, sizeof c->id, "%s", id);
  snprintf(c->title, sizeof c->title, "%s", title);
  c->q[0] = 0;
  c->count = count;
  kinds[ncats++] = kind;
}

/* ---------- a category's channels, a page at a time ---------- */
static void page_came(const jval *answer, void *user) {
  if ((intptr_t)user != list_gen) return;
  loading = 0;
  const jval *list = json_get(answer, "channels");
  for (int i = 0; i < json_len(list); i++) {
    if (nchans == cap_chans) {
      cap_chans = cap_chans ? cap_chans * 2 : 128;
      chans = realloc(chans, (size_t)cap_chans * sizeof *chans);
    }
    const jval *c = json_at(list, i);
    channel *ch = &chans[nchans++];
    ch->key = copy(json_get(c, "key"));
    ch->name = copy(json_get(c, "name"));
    ch->logo = copy(json_get(c, "logo"));
    ch->now = copy(json_get(c, "now"));
    ch->start = json_num(json_get(c, "start"), 0);
    ch->end = json_num(json_get(c, "end"), 0);
  }
  more = json_bool(json_get(answer, "more"), 0);
}

static void load_page(int next) {
  category *c = &cats[cat_at];
  if (!next) clear_channels();
  if (kinds[cat_at] != C_GROUP && kinds[cat_at] != C_RECENT && !(kinds[cat_at] == C_SEARCH && c->q[0])) return;
  list_gen++;
  loading = 1;
  sbuf p = {0};
  sb_add(&p, "/app/channels/group?id=");
  sb_add_url(&p, c->id);
  sb_addf(&p, "&skip=%d", nchans);
  if (c->q[0]) {
    sb_add(&p, "&q=");
    sb_add_url(&p, c->q);
  }
  api_get(p.s, page_came, (void *)(intptr_t)list_gen);
  sb_free(&p);
}

/* ---------- the categories and the lists ---------- */
static void came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  ncats = 0;
  add_cat(C_SEARCH, "search", tx("channelSearch"), 0);
  const jval *recent = json_get(answer, "recent");
  if (json_len(recent)) add_cat(C_RECENT, "recent", tx("channelRecent"), json_len(recent));
  const jval *groups = json_get(answer, "groups");
  for (int g = 0; g < json_len(groups); g++) {
    const jval *v = json_at(groups, g);
    add_cat(C_GROUP, json_str(json_get(v, "id"), ""), json_str(json_get(v, "title"), ""), (int)json_num(json_get(v, "count"), -1));
  }
  add_cat(C_ADD, "add", tx("m3uAdd"), 0);
  const jval *ls = json_get(answer, "lists");
  nlists = 0;
  for (int i = 0; i < json_len(ls) && nlists < MAX_LISTS; i++, nlists++) {
    const jval *l = json_at(ls, i);
    snprintf(lists[nlists].url, sizeof lists[0].url, "%s", json_str(json_get(l, "url"), ""));
    snprintf(lists[nlists].name, sizeof lists[0].name, "%s", json_str(json_get(l, "name"), ""));
    lists[nlists].count = (int)json_num(json_get(l, "count"), 0);
    lists[nlists].updated = json_num(json_get(l, "updated"), 0);
  }
  if (nlists) add_cat(C_LISTS, "lists", tx("m3uManage"), nlists);
  snprintf(empty, sizeof empty, "%s", json_str(json_get(answer, "empty"), ""));
  loaded = 1;
  /* the first category after Search, or the one the screen was opened for */
  cat_at = ncats > 2 + (nlists > 0) ? 1 : 0;
  for (int i = 0; i < ncats; i++)
    if ((opening == 2 && kinds[i] == C_LISTS) || (opening == 1 && kinds[i] == C_ADD)) cat_at = i;
  opening = 0;
  load_page(0);
}

static void load(void) {
  gen++;
  loaded = 0;
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

static void post_lists(const char *field, const char *url) {
  sbuf b = {0};
  sb_addf(&b, "{\"%s\":", field);
  sb_add_json_str(&b, url);
  sb_add(&b, "}");
  net_request("/app/channels/lists", b.s, NET_HIGH, NULL, lists_changed, NULL);
  sb_free(&b);
}

static void list_added(const char *url) {
  ui_toast(tx("saving"));
  post_lists("url", url);
}

static void searched(const char *q) {
  category *c = &cats[0];
  snprintf(c->q, sizeof c->q, "%s", q);
  snprintf(c->title, sizeof c->title, "%s: %s", tx("channelSearch"), q);
  cat_at = 0;
  on_list = 1;
  load_page(0);
}

/* ---------- playing: an M3U channel at once, an addon's through its sources ---------- */
static void play_came(const jval *answer, void *user) {
  (void)user;
  const char *url = json_str(json_get(answer, "url"), NULL), *id = json_str(json_get(answer, "id"), NULL);
  const char *name = json_str(json_get(answer, "name"), "");
  if (url) player_play_address(url, name, 1);
  else if (id) sources_open(json_str(json_get(answer, "type"), "tv"), id, id, name);
  else ui_toast(tx("sourceFailed"));
}

static void play(const channel *c) {
  sbuf p = {0};
  sb_add(&p, "/app/channels/play?key=");
  sb_add_url(&p, c->key ? c->key : "");
  sb_add(&p, "&name=");
  sb_add_url(&p, c->name ? c->name : "");
  sb_add(&p, "&logo=");
  sb_add_url(&p, c->logo ? c->logo : "");
  api_get(p.s, play_came, NULL);
  sb_free(&p);
}

/* ---------- keys ---------- */
static int rows_in(float step) { return (int)((SC_DESIGN_H - LIST_Y - 30) / step); }
static int rows_count(void) { return kinds[cat_at] == C_LISTS ? nlists + 1 : nchans; }

static void activate(void) {
  int kind = kinds[cat_at];
  if (!on_list) {
    if (kind == C_SEARCH && !nchans) return addr_open(tx("channelSearch"), ADDR_TEXT, searched);
    if (kind == C_ADD) return addr_open(tx("m3uAdd"), ADDR_M3U, list_added);
    if (rows_count()) on_list = 1, ch_at = 0, armed = -1;
    return;
  }
  if (kind == C_LISTS) {
    if (ch_at == nlists) {
      for (int i = 0; i < nlists; i++) post_lists("refresh", lists[i].url);
      return ui_toast(tx("syncing"));
    }
    /* removing asks for a second OK */
    if (armed != ch_at) {
      armed = ch_at;
      return ui_toast(tx("removeListConfirm"));
    }
    armed = -1;
    on_list = 0;
    return post_lists("remove", lists[ch_at].url);
  }
  if (ch_at < nchans) play(&chans[ch_at]);
}

static void key(app_key k) {
  if (addr_active()) return addr_key(k);
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (k == K_OK) return activate();
  if (!on_list) {
    int before = cat_at;
    if (k == K_DOWN && cat_at < ncats - 1) cat_at++;
    else if (k == K_UP && cat_at > 0) cat_at--;
    else if (k == K_RIGHT && rows_count()) on_list = 1, ch_at = 0;
    else if (k == K_LEFT) nav_focus(NAV_CHANNELS);
    else if (k == K_BACK) app_show(&home_screen);
    if (before != cat_at) load_page(0);
    return;
  }
  if (k == K_DOWN && ch_at < rows_count() - 1) ch_at++;
  else if (k == K_UP && ch_at > 0) ch_at--;
  else if (k == K_LEFT || k == K_BACK) on_list = 0;
  if (armed != ch_at) armed = -1;
  /* the next page comes before the end is reached */
  if (more && !loading && ch_at >= nchans - PAGE_AHEAD) load_page(1);
}

static void text(const char *utf8) {
  if (addr_active()) addr_text(utf8);
}

/* ---------- drawing ---------- */
static void draw_channel(const channel *c, float y, int focused) {
  float w = SC_DESIGN_W - 48 - CH_X;
  gfx_rect(CH_X, y, w, CH_H, 14, focused ? C_TEXT(0.16f) : C_TEXT(0.04f));
  if (focused) gfx_outline(CH_X, y, w, CH_H, 14, 3, C_WHITE);
  gfx_rect(CH_X + 20, y + 12, 120, 78, 10, C_TEXT(0.08f));
  const sc_tex *logo = img_get(c->logo, 120, NULL);
  if (logo) {
    float s = 110.0f / logo->w < 70.0f / logo->h ? 110.0f / logo->w : 70.0f / logo->h;
    gfx_image(logo, CH_X + 20 + (120 - logo->w * s) / 2, y + 12 + (78 - logo->h * s) / 2, logo->w * s, logo->h * s, 0, 1, STRETCH);
  }
  float x = CH_X + 162, tw = w - 182, ty = y + 14;
  text_draw_fit(c->name ? c->name : "", x, ty, tw, 26, W_BOLD, C_WHITE);
  ty += 34;
  if (c->now) text_draw_fit(c->now, x, ty, tw, 20, W_REGULAR, C_TEXT(0.75f)), ty += 26;
  if (c->start > 0 && c->end > c->start) {
    double now = (double)time(NULL) * 1000.0, p = (now - c->start) / (c->end - c->start);
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    gfx_rect(x, ty, 260, 4, 2, C_TEXT(0.15f));
    gfx_rect(x, ty, (float)(260 * p), 4, 2, C_WHITE);
  }
}

static void draw_list_row(int i, float y, int focused) {
  float w = SC_DESIGN_W - 48 - CH_X;
  gfx_rect(CH_X, y, w, CH_H, 14, focused ? C_TEXT(0.16f) : C_TEXT(0.04f));
  if (focused) gfx_outline(CH_X, y, w, CH_H, 14, 3, i == armed ? SC_RGBA(34, 211, 238, 255) : C_WHITE);
  if (i == nlists) {
    ui_line(tx("m3uRefreshAll"), CH_X + 26, y, CH_H, 25, W_BOLD, C_WHITE);
    return;
  }
  text_draw_fit(lists[i].name, CH_X + 26, y + 18, w - 52, 26, W_BOLD, C_WHITE);
  char count[16], day[16] = "-";
  snprintf(count, sizeof count, "%d", lists[i].count);
  if (lists[i].updated > 0) {
    time_t at = (time_t)(lists[i].updated / 1000);
    struct tm *tm = localtime(&at);
    if (tm) snprintf(day, sizeof day, "%d/%d", tm->tm_mday, tm->tm_mon + 1);
  }
  text_draw_fit(txf("m3uListInfo", count, day), CH_X + 26, y + 58, w - 52, 20, W_REGULAR, C_TEXT(0.6f));
}

static void frame(double t, float dt) {
  (void)t;
  int nav = nav_has_focus() || addr_active();
  text_draw(tx("navChannels"), LEFT + 4, TOP, 50, W_EXTRABOLD, C_WHITE);
  if (!loaded) text_draw(tx("searching"), LEFT, LIST_Y + 10, 26, W_REGULAR, C_TEXT(0.72f));
  /* the categories, only those in view */
  int crows = rows_in(CAT_STEP);
  if (cat_at < cat_top) cat_top = cat_at;
  if (cat_at >= cat_top + crows) cat_top = cat_at - crows + 1;
  for (int i = cat_top; i < ncats && i < cat_top + crows; i++) {
    float y = LIST_Y + (i - cat_top) * CAT_STEP;
    int cur = i == cat_at, f = cur && !on_list && !nav;
    if (cur) gfx_rect(LEFT, y, CAT_W, CAT_H, 12, f ? C_WHITE : C_TEXT(0.10f));
    uint32_t c = f ? C_BG : kinds[i] == C_GROUP ? C_TEXT(0.78f) : C_WHITE;
    float tw = CAT_W - 44 - (cats[i].count > 0 ? 70 : 0);
    text_draw_fit(cats[i].title, LEFT + 22, y + (CAT_H - text_line_height(25)) / 2, tw, 25, W_SEMIBOLD, c);
    if (cats[i].count > 0) {
      char n[16];
      snprintf(n, sizeof n, "%d", cats[i].count);
      text_draw(n, LEFT + CAT_W - 20 - text_width(n, 19, W_SEMIBOLD), y + (CAT_H - text_line_height(19)) / 2, 19, W_SEMIBOLD,
                f ? SC_RGBA(60, 60, 64, 255) : C_TEXT(0.45f));
    }
  }
  /* the channels (or the lists), only those in view */
  int kind = ncats ? kinds[cat_at] : C_SEARCH, rows = rows_in(CH_STEP), count = ncats ? rows_count() : 0;
  if (ch_at < ch_top) ch_top = ch_at;
  if (ch_at >= ch_top + rows) ch_top = ch_at - rows + 1;
  for (int i = ch_top; i < count && i < ch_top + rows; i++) {
    float y = LIST_Y + (i - ch_top) * CH_STEP;
    int f = i == ch_at && on_list && !nav;
    if (kind == C_LISTS) draw_list_row(i, y, f);
    else draw_channel(&chans[i], y, f);
  }
  if (loaded && !count && kind != C_ADD && kind != C_SEARCH)
    text_draw_wrap(loading ? tx("searching") : ncats <= 2 ? empty : tx("noChannels"), CH_X, LIST_Y + 14, 1100, 25, W_REGULAR,
                   C_TEXT(0.7f), 3, 36);
  nav_draw(NAV_CHANNELS, dt);
  addr_draw();
}

/* what = 1 opens the add panel, 2 the lists (Settings > Channels) */
void channels_open(int what) {
  opening = what;
  app_show(&channels_screen);
}

static void enter(void) {
  on_list = 0;
  armed = -1;
  clear_channels();
  if (opening == 1) addr_open(tx("m3uAdd"), ADDR_M3U, list_added);
  load();
}

const screen channels_screen = {enter, key, frame, text, 0};
