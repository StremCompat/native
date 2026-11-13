/* A title's page: its name or logo, lines and synopsis, the buttons, and for a series the
   seasons and episodes. While the focus is on the episodes the top shows the focused episode.
   The server decides what is on it (/app/title); measures are the web interface's (detail.css). */
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

#define LEFT 176.0f
#define TOP 60.0f
#define WIDTH 1680.0f /* the page without the rail and the right margin */
#define EP_W 430.0f
#define EP_THUMB_H 242.0f
#define EP_H 286.0f
#define EP_STEP 454.0f
#define RANGE 50 /* episodes per range in the continuous order */

typedef struct {
  char *kind, *label, *icon, *video;
} action;

typedef struct {
  char *id, *title, *title_overall, *line, *description, *thumbnail, *badge, *date;
  int absolute, own_thumbnail, watched;
  float progress;
} episode;

typedef struct {
  char *label;
  episode *eps;
  int n, number;
} season;

enum { ZONE_BUTTONS, ZONE_SEASONS, ZONE_EPISODES };
/* the controls above the episodes */
enum { CTRL_ORDER, CTRL_PREV, CTRL_LIST, CTRL_NEXT, CTRL_JUMP, CTRL_COUNT };

static char *want_type, *want_id; /* what was opened */
static int gen, loaded;
static char *name, *logo, *runtime, *year, *rating, *genres, *people, *facts, *awards, *description;
static action *actions;
static int nactions;
static season *seasons;
static int nseasons;
static episode **linear; /* the numbered episodes in order, for the continuous order */
static int nlinear;
static int zone, button, control, season_at, range_at, continuous, ep_at;
static float ep_scroll;
static backdrop bg;
static char *background;

static char *copy(const jval *v) {
  const char *s = json_str(v, NULL);
  return s && *s ? str_dup(s) : NULL;
}

static void clear(void) {
  char **fields[] = {&name, &logo, &runtime, &year, &rating, &genres, &people, &facts, &awards, &description, &background};
  for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
    free(*fields[i]);
    *fields[i] = NULL;
  }
  for (int i = 0; i < nactions; i++) {
    free(actions[i].kind);
    free(actions[i].label);
    free(actions[i].icon);
    free(actions[i].video);
  }
  free(actions);
  actions = NULL;
  nactions = 0;
  for (int s = 0; s < nseasons; s++) {
    for (int e = 0; e < seasons[s].n; e++) {
      episode *ep = &seasons[s].eps[e];
      char **ef[] = {&ep->id, &ep->title, &ep->title_overall, &ep->line, &ep->description, &ep->thumbnail, &ep->badge, &ep->date};
      for (size_t i = 0; i < sizeof ef / sizeof ef[0]; i++) free(*ef[i]);
    }
    free(seasons[s].eps);
    free(seasons[s].label);
  }
  free(seasons);
  seasons = NULL;
  nseasons = 0;
  free(linear);
  linear = NULL;
  nlinear = 0;
}

/* ---------- the episodes on show: one season, or a range of the continuous order ---------- */
static int shown_count(void) {
  if (continuous) {
    int left = nlinear - range_at * RANGE;
    return left < RANGE ? left : RANGE;
  }
  return season_at < nseasons ? seasons[season_at].n : 0;
}

static episode *shown(int i) {
  if (continuous) return linear[range_at * RANGE + i];
  return &seasons[season_at].eps[i];
}

/* shows the list holding this episode, with the focus on it */
static void select_episode(const char *id) {
  for (int s = 0; id && s < nseasons; s++)
    for (int e = 0; e < seasons[s].n; e++)
      if (!strcmp(seasons[s].eps[e].id, id)) {
        if (continuous) {
          for (int i = 0; i < nlinear; i++)
            if (linear[i] == &seasons[s].eps[e]) range_at = i / RANGE, ep_at = i % RANGE;
        } else {
          season_at = s;
          ep_at = e;
        }
        return;
      }
}

static void read_page(const jval *t) {
  name = copy(json_get(t, "name"));
  logo = copy(json_get(t, "logo"));
  runtime = copy(json_get(t, "runtime"));
  year = copy(json_get(t, "year"));
  rating = copy(json_get(t, "rating"));
  genres = copy(json_get(t, "genres"));
  people = copy(json_get(t, "people"));
  facts = copy(json_get(t, "facts"));
  awards = copy(json_get(t, "awards"));
  description = copy(json_get(t, "description"));
  background = copy(json_get(t, "background"));
  const jval *acts = json_get(t, "actions");
  nactions = json_len(acts);
  actions = calloc((size_t)nactions + 1, sizeof(action));
  for (int i = 0; i < nactions; i++) {
    const jval *a = json_at(acts, i);
    actions[i].kind = copy(json_get(a, "kind"));
    actions[i].label = copy(json_get(a, "label"));
    actions[i].icon = copy(json_get(a, "icon"));
    actions[i].video = copy(json_get(a, "video"));
  }
  const jval *list = json_get(json_get(t, "series"), "seasons");
  nseasons = json_len(list);
  seasons = calloc((size_t)nseasons + 1, sizeof(season));
  for (int s = 0; s < nseasons; s++) {
    const jval *sv = json_at(list, s), *eps = json_get(sv, "episodes");
    season *se = &seasons[s];
    se->label = copy(json_get(sv, "label"));
    se->number = (int)json_num(json_get(sv, "n"), 0);
    se->n = json_len(eps);
    se->eps = calloc((size_t)se->n + 1, sizeof(episode));
    if (se->number) nlinear += se->n;
    for (int e = 0; e < se->n; e++) {
      const jval *ev = json_at(eps, e);
      episode *ep = &se->eps[e];
      ep->id = copy(json_get(ev, "id"));
      ep->title = copy(json_get(ev, "title"));
      ep->title_overall = copy(json_get(ev, "titleOverall"));
      ep->line = copy(json_get(ev, "line"));
      ep->description = copy(json_get(ev, "description"));
      ep->thumbnail = copy(json_get(ev, "thumbnail"));
      ep->badge = copy(json_get(ev, "badge"));
      ep->date = copy(json_get(ev, "date"));
      ep->absolute = (int)json_num(json_get(ev, "absolute"), 0);
      ep->own_thumbnail = json_bool(json_get(ev, "ownThumbnail"), 0);
      ep->watched = json_bool(json_get(ev, "watched"), 0);
      ep->progress = (float)json_num(json_get(ev, "progress"), 0);
    }
  }
  linear = calloc((size_t)nlinear + 1, sizeof(episode *));
  for (int s = 0, i = 0; s < nseasons; s++)
    for (int e = 0; seasons[s].number && e < seasons[s].n; e++) linear[i++] = &seasons[s].eps[e];
  select_episode(json_str(json_get(json_get(t, "series"), "landing"), NULL));
}

static void answered(const jval *answer, void *user) {
  const jval *t = json_get(answer, "title");
  if ((intptr_t)user != gen || t->type != J_OBJ) return;
  clear();
  read_page(t);
  loaded = 1;
  backdrop_set(&bg, background);
  /* a new film opens its sources at once, as on the web */
  if (json_bool(json_get(t, "openSources"), 0)) sources_open(want_type, want_id, want_id, name);
}

void title_open(const char *type, const char *id) {
  free(want_type);
  free(want_id);
  want_type = str_dup(type);
  want_id = str_dup(id);
  clear();
  gen++;
  loaded = 0;
  zone = ZONE_BUTTONS;
  button = control = season_at = range_at = continuous = ep_at = 0;
  ep_scroll = 0;
  sbuf path = {0};
  sb_add(&path, "/app/title?type=");
  sb_add_url(&path, type);
  sb_add(&path, "&id=");
  sb_add_url(&path, id);
  api_get(path.s, answered, (void *)(intptr_t)gen);
  sb_free(&path);
  app_push(&title_screen);
}

/* ---------- keys ---------- */
static void run_action(const action *a) {
  const char *kind = a->kind ? a->kind : "";
  if (!strcmp(kind, "resume")) player_start(want_type, want_id, a->video ? a->video : want_id, -1);
  else if (!strcmp(kind, "episode") || !strcmp(kind, "sources") || !strcmp(kind, "best"))
    sources_open(want_type, want_id, a->video ? a->video : want_id, name);
}

static void move_list(int dir) {
  ep_at = 0;
  if (continuous) {
    if (dir < 0 && range_at > 0) range_at--;
    else if (dir > 0 && (range_at + 1) * RANGE < nlinear) range_at++;
  } else {
    if (dir < 0 && season_at > 0) season_at--;
    else if (dir > 0 && season_at < nseasons - 1) season_at++;
  }
}

static void control_key(void) {
  if (control == CTRL_ORDER) {
    const char *keep = shown_count() ? shown(ep_at)->id : NULL;
    continuous = !continuous;
    range_at = season_at = ep_at = 0;
    select_episode(keep);
  } else if (control == CTRL_PREV) {
    move_list(-1);
  } else if (control == CTRL_NEXT) {
    move_list(1);
  }
}

static void key(app_key k) {
  if (nav_has_focus()) {
    if (nav_key(k) == NAV_LEAVE) app_quit();
    return;
  }
  if (k == K_BACK) {
    app_back();
    return;
  }
  if (!loaded) return;
  int *at = zone == ZONE_BUTTONS ? &button : zone == ZONE_SEASONS ? &control : &ep_at;
  int count = zone == ZONE_BUTTONS ? nactions : zone == ZONE_SEASONS ? CTRL_COUNT : shown_count();
  if (k == K_RIGHT && *at < count - 1) (*at)++;
  else if (k == K_LEFT && *at > 0) (*at)--;
  else if (k == K_LEFT) nav_focus(NAV_HOME);
  else if (k == K_DOWN && zone == ZONE_BUTTONS && nseasons) zone = ZONE_SEASONS;
  else if (k == K_DOWN && zone == ZONE_SEASONS && shown_count()) zone = ZONE_EPISODES;
  else if (k == K_UP && zone > ZONE_BUTTONS) zone--;
  else if (k == K_OK && zone == ZONE_BUTTONS && button < nactions) run_action(&actions[button]);
  else if (k == K_OK && zone == ZONE_SEASONS) control_key();
  else if (k == K_OK && zone == ZONE_EPISODES && shown_count()) sources_open(want_type, want_id, shown(ep_at)->id, name);
}

/* ---------- drawing ---------- */
static float draw_name(float y, float logo_w, float logo_h, float size) {
  const sc_tex *pic = img_get(logo, (int)logo_w, NULL);
  if (pic) {
    float s = logo_w / pic->w < logo_h / pic->h ? logo_w / pic->w : logo_h / pic->h;
    gfx_image(pic, LEFT, y + logo_h - pic->h * s, pic->w * s, pic->h * s, 0, 1, STRETCH);
    return logo_h;
  }
  if (logo && !img_failed(logo, (int)logo_w)) return logo_h; /* its place is kept while it comes */
  return text_draw_wrap(name ? name : "", LEFT, y, WIDTH * 0.62f, size, W_EXTRABOLD, C_WHITE, 2, size * 1.05f);
}

static float draw_lines(float y) {
  float x = LEFT;
  x += ui_line(runtime, x, y, 32, 26, W_REGULAR, C_TEXT(0.76f)) + (runtime ? 24 : 0);
  x += ui_line(year, x, y, 32, 26, W_REGULAR, C_TEXT(0.76f)) + (year ? 24 : 0);
  if (rating) x += ui_rating(rating, x, y, 32, 26, 17, C_TEXT(0.76f)) + 24;
  ui_line(genres, x, y, 32, 26, W_REGULAR, C_TEXT(0.76f));
  y += 32 + 12;
  struct {
    const char *text;
    float size;
    uint32_t color;
  } info[] = {{people, 24, C_TEXT(0.72f)}, {facts, 22, C_TEXT(0.6f)}, {awards, 23, SC_RGBA(245, 197, 24, 219)}};
  for (int i = 0; i < 3; i++) {
    if (!info[i].text) continue;
    text_draw_fit(info[i].text, LEFT, y, WIDTH * 0.62f, info[i].size, W_REGULAR, info[i].color);
    y += text_line_height(info[i].size) + 5;
  }
  return y;
}

static void draw_buttons(float y) {
  float x = LEFT;
  for (int i = 0; i < nactions; i++) {
    const action *a = &actions[i];
    int focused = zone == ZONE_BUTTONS && i == button && !nav_has_focus();
    uint32_t back = focused ? C_WHITE : C_TEXT(0.1f), fore = focused ? SC_RGBA(12, 11, 17, 255) : C_TEXT(0.9f);
    static const struct {
      const char *name;
      icon_id id;
    } icons[] = {{"bolt", ICON_BOLT}, {"similar", ICON_SIMILAR}, {"trailer", ICON_TRAILER},
                 {"grid", ICON_GRID}, {"check", ICON_CHECK},     {"plus", ICON_PLUS}};
    int icon = -1;
    for (size_t n = 0; a->icon && n < sizeof icons / sizeof icons[0]; n++)
      if (!strcmp(a->icon, icons[n].name)) icon = (int)icons[n].id;
    float label_w = text_width(a->label ? a->label : "", 26, W_BOLD), w;
    /* round with its icon, showing its label only while focused; the main ones play and always
       show their label */
    int main_button = icon < 0, with_label = main_button || focused;
    if (main_button) w = 26 + 30 + 10 + label_w + 36;
    else w = focused ? 20 + 30 + 10 + label_w + 28 : 64;
    gfx_rect(x, y, w, 64, 32, back);
    float ix = main_button ? x + 26 : focused ? x + 20 : x + 17;
    ui_icon(main_button ? ICON_PLAY : (icon_id)icon, ix, y + 17, 30, fore);
    if (with_label) ui_line(a->label, ix + 40, y, 64, 26, W_BOLD, fore);
    x += w + 12;
  }
}

static void draw_controls(float y) {
  const char *labels[CTRL_COUNT];
  char range[64];
  labels[CTRL_ORDER] = tx(continuous ? "continuousOrder" : "seasonOrder");
  labels[CTRL_PREV] = "\xE2\x80\xB9";
  labels[CTRL_NEXT] = "\xE2\x80\xBA";
  labels[CTRL_JUMP] = tx("goToEpisode");
  if (continuous && shown_count()) {
    char a[16], b[16];
    snprintf(a, sizeof a, "%d", shown(0)->absolute);
    snprintf(b, sizeof b, "%d", shown(shown_count() - 1)->absolute);
    snprintf(range, sizeof range, "%s", txf("episodeRange", a, b));
    labels[CTRL_LIST] = range;
  } else {
    labels[CTRL_LIST] = season_at < nseasons ? seasons[season_at].label : "";
  }
  float x = LEFT;
  for (int i = 0; i < CTRL_COUNT; i++) {
    int focused = zone == ZONE_SEASONS && i == control && !nav_has_focus();
    float tw = text_width(labels[i], 22, focused ? W_BOLD : W_REGULAR);
    float w = tw + 48;
    if (i == CTRL_PREV || i == CTRL_NEXT) w = tw + 30 < 54 ? 54 : tw + 30;
    if (i == CTRL_LIST && w < 238) w = 238;
    uint32_t back = focused ? C_WHITE : i == CTRL_LIST ? C_TEXT(0.14f) : C_TEXT(0.07f);
    uint32_t fore = focused ? SC_RGBA(12, 11, 17, 255) : i == CTRL_LIST ? C_WHITE : C_TEXT(0.72f);
    gfx_rect(x, y, w, 46, 23, back);
    ui_line(labels[i], x + (w - tw) / 2, y, 46, 22, focused ? W_BOLD : W_REGULAR, fore);
    x += w + 10;
  }
}

static void draw_episode(const episode *ep, float x, float y, int focused) {
  if (focused) {
    y -= 4;
    gfx_rect(x, y, EP_W, EP_H, 12, C_TEXT(0.1f));
  }
  gfx_rect(x, y, EP_W, EP_THUMB_H, 12, SC_RGBA(13, 13, 14, 255));
  float fade;
  const sc_tex *pic = img_get(ep->thumbnail, (int)EP_W, &fade);
  if (pic) gfx_image(pic, x, y, EP_W, EP_THUMB_H, 12, fade * (ep->own_thumbnail ? 1 : 0.52f), COVER);
  gfx_gradient(x, y + EP_THUMB_H * 0.6f, EP_W, EP_THUMB_H * 0.4f, 12, SC_RGBA(0, 0, 0, 0), SC_RGBA(0, 0, 0, 128), 1);
  if (ep->badge) {
    float bw = text_width(ep->badge, 20, W_BOLD) + 22;
    gfx_rect(x + 10, y + 10, bw, 34, 7, SC_RGBA(0, 0, 0, 184));
    ui_line(ep->badge, x + 21, y + 10, 34, 20, W_BOLD, C_WHITE);
  }
  if (ep->date) {
    float dw = text_width(ep->date, 18, W_REGULAR) + 22;
    gfx_rect(x + 10, y + EP_THUMB_H - 26 - 32, dw, 32, 7, SC_RGBA(0, 0, 0, 153));
    ui_line(ep->date, x + 21, y + EP_THUMB_H - 26 - 32, 32, 18, W_REGULAR, C_WHITE);
  }
  if (ep->progress > 0) {
    gfx_rect(x + 14, y + EP_THUMB_H - 18, EP_W - 28, 6, 3, C_TEXT(0.28f));
    gfx_rect(x + 14, y + EP_THUMB_H - 18, (EP_W - 28) * clampf(ep->progress, 0, 1), 6, 3, C_WHITE);
  }
  if (ep->watched) {
    gfx_rect(x + EP_W - 54, y + 12, 42, 42, 21, SC_RGBA(0, 0, 0, 184));
    ui_icon(ICON_CHECK, x + EP_W - 46, y + 20, 26, C_WHITE);
  }
  const char *t = continuous ? ep->title_overall : ep->title;
  text_draw_fit(t ? t : "", x + 8, y + EP_THUMB_H + 10, EP_W - 16, 23, W_REGULAR,
                focused ? C_WHITE : ep->watched ? C_TEXT(0.55f) : C_TEXT(0.86f));
  if (focused) gfx_outline(x - 4, y - 4, EP_W + 8, EP_H + 8, 16, 4, C_WHITE);
}

static void draw_episodes(float y, float dt) {
  int n = shown_count(), vis = (int)(1744 / EP_STEP);
  if (vis < 2) vis = 2;
  float target = ep_at > vis - 2 ? (ep_at - (vis - 2)) * EP_STEP : 0;
  ep_scroll = ui_approach(ep_scroll, target, dt, 50);
  gfx_push_clip(128, y - 12, SC_DESIGN_W - 128, EP_H + 24);
  for (int i = 0; i < n; i++) {
    float x = LEFT + 7 + i * EP_STEP - ep_scroll;
    if (x + EP_W < 128) continue;
    if (x > SC_DESIGN_W) break;
    draw_episode(shown(i), x, y + 7, zone == ZONE_EPISODES && i == ep_at && !nav_has_focus());
  }
  gfx_pop_clip();
}

static void frame(double t, float dt) {
  int on_episode = zone == ZONE_EPISODES && shown_count();
  const episode *ep = on_episode ? shown(ep_at) : NULL;
  /* the focused episode's picture behind the page, else the title's */
  backdrop_set(&bg, ep && ep->thumbnail ? ep->thumbnail : background);
  backdrop_draw(&bg, 653, SC_DESIGN_H, 0.88f, dt);
  static const shade_stop down[] = {{0, 0.04f}, {0.46f, 0.04f}, {0.76f, 0.72f}, {0.96f, 1}};
  static const shade_stop right[] = {{0.34f, 1}, {0.41f, 0.92f}, {0.49f, 0.7f}, {0.58f, 0.42f}, {0.69f, 0.16f}, {0.80f, 0}};
  ui_shade(down, 4, 1);
  ui_shade(right, 6, 0);
  if (loaded) {
    float y = TOP;
    if (ep) {
      y += draw_name(y, 420, 116, 54) + 14;
      text_draw_fit(ep->line ? ep->line : "", LEFT, y, WIDTH, 27, W_REGULAR, C_WHITE);
      y += text_line_height(27) + 10;
      text_draw_wrap(ep->description ? ep->description : "", LEFT, y + (36 - text_line_height(24)) / 2, WIDTH * 0.8f,
                     24, W_REGULAR, C_TEXT(0.72f), 2, 36);
      y += 72 + 24;
    } else {
      y += draw_name(y, 480, 150, 68) + 18;
      y = draw_lines(y) + 12;
      float lh = 27 * 1.45f;
      if (description)
        y += text_draw_wrap(description, LEFT, y + (lh - text_line_height(27)) / 2, WIDTH * 0.6f, 27, W_REGULAR,
                            C_TEXT(0.78f), 3, lh);
      y += 32;
      draw_buttons(y);
      y += 64 + 30;
    }
    if (nseasons) {
      draw_controls(y);
      draw_episodes(y + 46 + 24, dt);
    }
  }
  nav_draw(NAV_HOME, dt);
}

static void enter(void) {}

const screen title_screen = {enter, key, frame};
