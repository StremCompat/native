#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "json.h"
#include "nav.h"
#include "gfx.h"
#include "img.h"
#include "net.h"
#include "platform.h"
#include "saver.h"
#include "screens.h"
#include "ui.h"
#include "util.h"

static char asset_root[512] = "assets";
static int quit;
const char *app_stage = "start";

const char *app_asset_dir(const char *sub) {
  static char path[600];
  snprintf(path, sizeof path, "%s/%s", asset_root, sub);
  return path;
}

int app_init(int argc, char **argv) {
  (void)argc;
  char *base = SDL_GetBasePath();
  if (base) {
    snprintf(asset_root, sizeof asset_root, "%sassets", base);
    SDL_free(base);
  }
  (void)argv;
  return 0;
}

/* the keys every remote sends the same way; the rest come from platform_key */
static app_key key_of(const SDL_Event *ev) {
  if (ev->type != SDL_KEYDOWN) return K_NONE;
  app_key k = platform_key(ev);
  if (k != K_NONE) return k;
  switch (ev->key.keysym.sym) {
  case SDLK_UP: return K_UP;
  case SDLK_DOWN: return K_DOWN;
  case SDLK_LEFT: return K_LEFT;
  case SDLK_RIGHT: return K_RIGHT;
  case SDLK_AUDIOPLAY:
  case SDLK_AUDIOSTOP: return K_PLAY;
  default: return K_NONE;
  }
}

#define STACK 8

static const screen *stack[STACK];
static int depth;
static double last_frame;

void app_show(const screen *s) {
  depth = 1;
  stack[0] = s;
  s->enter();
}

void app_push(const screen *s) {
  if (depth < STACK) depth++;
  stack[depth - 1] = s;
  s->enter();
}

void app_back(void) {
  if (depth > 1) depth--;
  stack[depth - 1]->enter();
}

void app_quit(void) { quit = 1; }

/* when a key last came, for the screensaver; a key while it shows only ends it */
static double last_key_at = -1;

static void on_key(app_key k) {
  if (k == K_NONE) return;
  last_key_at = last_frame;
  if (saver_active()) {
    saver_stop();
    return;
  }
  if (depth) stack[depth - 1]->key(k);
}

/* OK is told on release, or as K_MENU once it has been held down (web: hold OK) */
#define HOLD_MS 600.0
static double ok_down_at = -1;
static int ok_held;

static int is_ok(const SDL_Event *ev) {
  SDL_Keycode k = ev->key.keysym.sym;
  return k == SDLK_RETURN || k == SDLK_KP_ENTER;
}

void app_event(const SDL_Event *ev) {
  if (ev->type == SDL_TEXTINPUT && depth && stack[depth - 1]->text) {
    stack[depth - 1]->text(ev->text.text);
  } else if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) && is_ok(ev)) {
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat) ok_down_at = last_frame, ok_held = 0;
    else if (ev->type == SDL_KEYUP && ok_down_at >= 0) {
      if (!ok_held) on_key(K_OK);
      ok_down_at = -1;
    }
  } else if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_MENU) {
    on_key(K_MENU);
  } else {
    on_key(key_of(ev));
  }
}

int app_wants_quit(void) { return quit; }

static char language[32];

const char *app_language(void) { return language; }

/* the icons, then the screen that waits for the server, which starts with the app on the TV */
int app_start(void) {
  if (ui_init()) return -1;
  platform_language(language, sizeof language);
  platform_start_server();
  app_show(&boot_screen);
  return 0;
}

void app_ready(void) {
  /* the server is told the TV's language for the texts that follow */
  char path[96];
  snprintf(path, sizeof path, "/app/state?tvLanguage=%s", language);
  net_reply r = {0};
  net_fetch(path, NULL, &r);
  /* no answer is not "signed out": the opening screen waits for the server again (a slow TV can
     still be bringing the account) instead of showing the sign-in to someone signed in */
  if (r.status != 200 || !r.body) {
    free(r.body);
    app_show(&boot_screen);
    return;
  }
  api_load_strings();
  /* signed in: Home, or for a Nuvio account who is watching first; otherwise signing in */
  jdoc *d = json_parse(r.body, r.len);
  const jval *st = json_root(d);
  nav_show_calendar(strcmp(json_str(json_get(st, "provider"), ""), "nuvio") != 0);
  if (!json_bool(json_get(st, "signedIn"), 0)) app_show(&login_screen);
  else if (!strcmp(json_str(json_get(st, "provider"), ""), "nuvio")) profiles_open(1);
  else app_show(&home_screen);
  update_check();
  saver_init();
  json_free(d);
  free(r.body);
}

void app_frame(int w, int h, double t) {
  float dt = last_frame > 0 && t > last_frame ? (float)(t - last_frame) : 0;
  last_frame = t;
  if (ok_down_at >= 0 && !ok_held && t - ok_down_at >= HOLD_MS) {
    ok_held = 1;
    on_key(K_MENU);
  }
  gfx_begin(w, h, C_BG);
  platform_poll();
  update_poll();
  /* a title asked from the system's home screen opens over Home once Home is there */
  char link_type[16], link_id[128];
  if (depth && stack[0] == &home_screen && stack[depth - 1] != &player_screen &&
      platform_take_link(link_type, link_id, sizeof link_id))
    title_open(link_type, link_id);
  if (depth > 1 && stack[depth - 1]->overlay) stack[depth - 2]->frame(t, dt);
  if (depth) stack[depth - 1]->frame(t, dt);
  ui_draw_toast(t);
  if (last_key_at < 0) last_key_at = t;
  saver_tick(t, last_key_at, depth && stack[depth - 1] != &player_screen);
  saver_draw(t);
  gfx_end();
}

/* before a screenshot: every answer from the server in (pictures loading start more), so the
   picture does not depend on how fast the machine was. Gives up after 15 real seconds. */
static void settle(double t) {
  double give_up = now_ms() + 15000;
  do {
    net_poll();
    img_frame(t);
    app_frame(SC_DESIGN_W, SC_DESIGN_H, t);
    if (net_pending()) SDL_Delay(5);
  } while (net_pending() && now_ms() < give_up);
}

int app_run_script(const char *script_path, const char *shot_path) {
  double t = 0;
  int rc = 0;
  char *script = script_path ? read_file(script_path, NULL) : NULL;
  if (script) {
    char *line = strtok(script, "\n");
    while (line) {
      char arg[400] = "";
      if (sscanf(line, "key %399s", arg) == 1) {
        static const struct {
          const char *name;
          app_key key;
        } keys[] = {{"up", K_UP}, {"down", K_DOWN}, {"left", K_LEFT}, {"right", K_RIGHT}, {"ok", K_OK}, {"back", K_BACK}, {"menu", K_MENU}};
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
          if (!strcmp(arg, keys[i].name)) on_key(keys[i].key);
        t += 16;
      } else if (sscanf(line, "wait %399s", arg) == 1) {
        /* what the server answered by now counts from now, then time passes */
        settle(t);
        t += atof(arg);
      } else if (sscanf(line, "shot %399s", arg) == 1) {
        settle(t);
        rc |= main_draw_and_save(arg, t);
      }
      line = strtok(NULL, "\n");
    }
    free(script);
  }
  if (shot_path) {
    settle(t);
    rc |= main_draw_and_save(shot_path, t);
  }
  return rc;
}
