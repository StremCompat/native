/* What can be done with a title, held OK on its card (web: card-menu.js): watch or continue, its
   page, similar titles, watched or not, out of Continue watching, the new episodes hidden, in or
   out of the library. Drawn over the screen it was opened on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

enum { A_OPEN, A_SIMILAR, A_TRAILER, A_WATCHED, A_UNWATCHED, A_CW_WATCHED, A_CW_REMOVE, A_HIDE_NEW, A_SHOW_WATCHED, A_SHOW_UNWATCHED, A_LIBRARY };

static card it;
static int cw;
static menu actions;

static void acted(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const char *message = json_str(json_get(json_root(d), "message"), "");
  if (*message) ui_toast(message);
  json_free(d);
  /* what changed shows on Home next time it is drawn; the Library builds itself on return */
  home_reload();
}

static void send(const char *action, int watched) {
  sbuf b = {0};
  sb_addf(&b, "{\"action\":\"%s\",\"watched\":%s,\"item\":{\"id\":", action, watched ? "true" : "false");
  sb_add_json_str(&b, it.id ? it.id : "");
  sb_add(&b, ",\"type\":");
  sb_add_json_str(&b, it.type ? it.type : "");
  sb_add(&b, ",\"name\":");
  sb_add_json_str(&b, it.name ? it.name : "");
  sb_add(&b, ",\"poster\":");
  sb_add_json_str(&b, it.poster ? it.poster : "");
  sb_add(&b, ",\"background\":");
  sb_add_json_str(&b, it.backdrop ? it.backdrop : "");
  if (it.video_id) {
    sb_add(&b, ",\"videoId\":");
    sb_add_json_str(&b, it.video_id);
  }
  sb_add(&b, "}}");
  net_request("/app/act", b.s, NET_HIGH, NULL, acted, NULL);
  sb_free(&b);
}

static char *copy(const char *s) { return s ? str_dup(s) : NULL; }

void card_menu_open(const card *c, int continue_watching) {
  card_clear(&it);
  it.id = copy(c->id);
  it.type = copy(c->type);
  it.name = copy(c->name);
  it.poster = copy(c->poster);
  it.backdrop = copy(c->backdrop);
  it.video_id = copy(c->video_id);
  cw = continue_watching;
  int series = it.type && !strcmp(it.type, "series");
  menu_begin(&actions, it.name ? it.name : "");
  /* play and the library first, what dismisses last */
  menu_add(&actions, cw ? tx("continueLabel") : tx("details"), 0, A_OPEN);
  menu_add(&actions, tx(c->in_library ? "removeLibrary" : "addLibrary"), 0, A_LIBRARY);
  if (cw) menu_add(&actions, tx("markWatched"), 0, A_CW_WATCHED);
  /* a film anywhere else: watched or not, either way; a series, all of it at once */
  if (!cw && !series) menu_add(&actions, tx(c->watched ? "markUnwatched" : "markWatched"), 0, c->watched ? A_UNWATCHED : A_WATCHED);
  if (series) menu_add(&actions, tx(c->watched ? "markSeriesUnwatched" : "markSeriesWatched"), 0, c->watched ? A_SHOW_UNWATCHED : A_SHOW_WATCHED);
  menu_add(&actions, tx("similarTitles"), 0, A_SIMILAR);
  menu_add(&actions, tx("trailer"), 0, A_TRAILER);
  if (cw) menu_add(&actions, tx("removeFromCw"), 0, A_CW_REMOVE);
  if (c->fresh) menu_add(&actions, tx("hideNewEpisodes"), 0, A_HIDE_NEW);
  app_push(&card_menu_screen);
}

static void key(app_key k) {
  int chosen = menu_key(&actions, k);
  if (chosen == -2) {
    app_back();
    return;
  }
  if (chosen < 0) return;
  app_back();
  switch (chosen) {
  case A_OPEN: title_open(it.type, it.id); break;
  case A_SIMILAR: similar_open(it.type, it.id); break;
  case A_TRAILER: player_play_trailer(it.type, it.id); break;
  case A_WATCHED: send("watched", 1); break;
  case A_UNWATCHED: send("watched", 0); break;
  case A_CW_WATCHED: send("cwWatched", 1); break;
  case A_CW_REMOVE: send("cwRemove", 1); break;
  case A_HIDE_NEW: send("hideNew", 1); break;
  case A_SHOW_WATCHED: send("show", 1); break;
  case A_SHOW_UNWATCHED: send("show", 0); break;
  default: send("library", 1); break;
  }
}

static void frame(double t, float dt) {
  (void)t;
  (void)dt;
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(0, 0, 0, 150));
  float h = menu_height(&actions);
  menu_draw(&actions, SC_DESIGN_W / 2 - 300, SC_DESIGN_H / 2 + h / 2);
}

static void enter(void) {}

const screen card_menu_screen = {enter, key, frame, NULL, 1};
