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

static void hide_cursor(void);

static void on_key(app_key k) {
  if (k == K_NONE) return;
  last_key_at = last_frame;
  if (k != K_OK && k != K_MENU) hide_cursor();
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

/* a press of the Magic Remote can come as a click and as Enter: the Enter right after a click is
   the same press. A click is told when the button comes up; held down, it opens the card menu as
   holding OK does (K_MENU). */
#define CLICK_ENTER_MS 250.0
static double clicked_at = -1, button_down_at = -1;

/* The Magic Remote's pointer. The arrows hide it at once, and only a real movement (more than the
   hand's tremble) brings it back; while hidden, a press of its button is OK on the focus. The focus
   follows it only once it rests (POINT_REST_MS), as the web does: following every tremble asked the
   server for each card it crossed. Settings can turn it off. */
#define POINT_REST_MS 120.0
#define TREMBLE_PX 12
static int pointer_on = 1, cursor_shown = 1, moved_x = -1, moved_y = -1, rest_x, rest_y;
static Uint32 rest_window;
static double moved_at = -1;

void app_set_pointer(int on) {
  pointer_on = on;
  if (!on) SDL_ShowCursor(SDL_DISABLE), cursor_shown = 0;
}

static void hide_cursor(void) {
  if (!cursor_shown) return;
  cursor_shown = 0;
  moved_at = -1;
  SDL_ShowCursor(SDL_DISABLE);
}

/* a movement: 1 when the pointer is in use (shown, or brought back by it) */
static int pointer_moved(int x, int y) {
  if (!pointer_on) return 0;
  if (!cursor_shown) {
    if (moved_x >= 0 && abs(x - moved_x) < TREMBLE_PX && abs(y - moved_y) < TREMBLE_PX) return 0;
    moved_x = x, moved_y = y;
    if (moved_at < 0) {
      /* the first movement after the arrows only marks where it was */
      moved_at = last_frame;
      return 0;
    }
    cursor_shown = 1;
    SDL_ShowCursor(SDL_ENABLE);
  }
  moved_x = x, moved_y = y;
  return 1;
}

static void pointer(Uint32 window_id, int px, int py, int press) {
  const screen *current = depth ? stack[depth - 1] : NULL;
  SDL_Window *window = SDL_GetWindowFromID(window_id);
  int w, h;
  if (!current || !current->point || !window) return;
  SDL_GetWindowSize(window, &w, &h);
  if (w <= 0 || h <= 0) return;
  if (press) clicked_at = last_frame;
  current->point((float)px * SC_DESIGN_W / w, (float)py * SC_DESIGN_H / h, press);
}

void app_event(const SDL_Event *ev) {
  if (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN) {
    last_key_at = last_frame;
    if (saver_active()) {
      saver_stop();
      return;
    }
    if (ev->type == SDL_MOUSEMOTION) {
      if (pointer_moved(ev->motion.x, ev->motion.y))
        rest_window = ev->motion.windowID, rest_x = ev->motion.x, rest_y = ev->motion.y, moved_at = last_frame;
    } else if (ev->button.button == SDL_BUTTON_LEFT) button_down_at = last_frame;
  } else if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT && button_down_at >= 0) {
    int held = last_frame - button_down_at >= HOLD_MS;
    button_down_at = -1;
    clicked_at = last_frame;
    if (!cursor_shown) {
      /* hidden (or turned off): the button is OK, or the menu when held */
      on_key(held ? K_MENU : K_OK);
      return;
    }
    /* the focus goes where the button was let go, then the press or the hold */
    moved_at = -1;
    pointer(ev->button.windowID, ev->button.x, ev->button.y, !held);
    if (held) on_key(K_MENU);
  } else if (is_ok(ev) && last_frame - clicked_at < CLICK_ENTER_MS) {
    ok_down_at = -1;
  } else if (ev->type == SDL_TEXTINPUT && depth && stack[depth - 1]->text) {
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

/* the profile at the top of the rail, from the state's face (a Stremio account: its e-mail's letter) */
static void show_face(const jval *st) {
  const jval *face = json_get(st, "face");
  nav_set_face(json_str(json_get(face, "name"), json_str(json_get(st, "email"), "")), json_str(json_get(face, "color"), NULL),
               json_str(json_get(face, "avatar"), ""), !strcmp(json_str(json_get(st, "provider"), ""), "nuvio"));
}

static void face_came(const jval *answer, void *user) {
  (void)user;
  if (answer) show_face(answer);
}

void app_refresh_face(void) { api_get("/app/state", face_came, NULL); }

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
  app_set_pointer(json_bool(json_get(st, "pointer"), 1));
  show_face(st);
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
  if (moved_at >= 0 && cursor_shown && t - moved_at >= POINT_REST_MS) {
    moved_at = -1;
    pointer(rest_window, rest_x, rest_y, 0);
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
  hit_reset();
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
      } else if (!strncmp(line, "move ", 5) || !strncmp(line, "click ", 6)) {
        /* the pointer at a point of the 1920x1080 design, after a frame has laid out what it can press */
        float px, py;
        if (sscanf(strchr(line, ' ') + 1, "%f %f", &px, &py) == 2) {
          settle(t);
          if (depth && stack[depth - 1]->point) stack[depth - 1]->point(px, py, line[0] == 'c');
        }
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
