#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "gfx.h"
#include "img.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "ui.h"
#include "util.h"

static char asset_root[512] = "assets";
static int quit;

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
  case SDLK_RETURN:
  case SDLK_KP_ENTER: return K_OK;
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

static void on_key(app_key k) {
  if (k != K_NONE && depth) stack[depth - 1]->key(k);
}

void app_event(const SDL_Event *ev) { on_key(key_of(ev)); }

int app_wants_quit(void) { return quit; }

/* the texts and the icons, then Home. The server starts with the app on the TV, so its first
   answer may take a moment: it is waited for up to 30 seconds. */
int app_start(void) {
  if (ui_init()) return -1;
  for (int i = 0; i < 120 && api_load_strings(); i++) SDL_Delay(250);
  app_show(&home_screen);
  return 0;
}

void app_frame(int w, int h, double t) {
  float dt = last_frame > 0 && t > last_frame ? (float)(t - last_frame) : 0;
  last_frame = t;
  gfx_begin(w, h, C_BG);
  if (depth) stack[depth - 1]->frame(t, dt);
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
        } keys[] = {{"up", K_UP}, {"down", K_DOWN}, {"left", K_LEFT}, {"right", K_RIGHT}, {"ok", K_OK}, {"back", K_BACK}};
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
