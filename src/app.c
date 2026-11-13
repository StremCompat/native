#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "img.h"
#include "net.h"
#include "text.h"
#include "util.h"

static char asset_root[512] = "assets";
static int quit;
static int focus;
static double clock_ms;

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

static app_key key_of(const SDL_Event *ev) {
  if (ev->type != SDL_KEYDOWN) return K_NONE;
  switch (ev->key.keysym.sym) {
  case SDLK_UP: return K_UP;
  case SDLK_DOWN: return K_DOWN;
  case SDLK_LEFT: return K_LEFT;
  case SDLK_RIGHT: return K_RIGHT;
  case SDLK_RETURN: return K_OK;
  case SDLK_ESCAPE:
  case SDLK_BACKSPACE: return K_BACK;
  default: return K_NONE;
  }
}

static void on_key(app_key k) {
  if (k == K_RIGHT && focus < 5) focus++;
  else if (k == K_LEFT && focus > 0) focus--;
  else if (k == K_BACK) quit = 1;
}

void app_event(const SDL_Event *ev) { on_key(key_of(ev)); }

int app_wants_quit(void) { return quit; }

#define BG SC_RGBA(10, 10, 11, 255)
#define WHITE SC_RGBA(255, 255, 255, 255)

void app_frame(int w, int h, double t) {
  clock_ms = t;
  gfx_begin(w, h, BG);
  /* side menu */
  gfx_gradient(0, 0, 128, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 235), SC_RGBA(10, 10, 11, 0), 0);
  gfx_rect(34, 416, 60, 56, 14, WHITE);
  /* hero */
  text_draw("StremCompat", 176, 380, 76, W_EXTRABOLD, WHITE);
  text_draw("Action \xE2\x80\xA2 Crime \xE2\x80\xA2 Drama", 176, 500, 26, W_SEMIBOLD, SC_RGBA(255, 255, 255, 158));
  text_draw_wrap("A native client drawn by the TV's own GPU: the same look, much smoother on older sets. "
                 "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xE4\xB8\xAD\xE6\x96\x87",
                 176, 548, 980, 30, W_REGULAR, SC_RGBA(255, 255, 255, 199), 2, 0);
  gfx_rect(176, 650, 200, 64, 32, WHITE);
  text_draw("See details", 206, 667, 26, W_BOLD, BG);
  /* a row of cards */
  text_draw("Continue watching", 178, 850, 26, W_BOLD, SC_RGBA(255, 255, 255, 199));
  for (int i = 0; i < 6; i++) {
    float x = 176 + i * 420.0f, y = 895;
    gfx_gradient(x, y, 400, 225, 12, SC_RGBA(40, 40, 44, 255), SC_RGBA(22, 22, 24, 255), 1);
    if (i == focus) gfx_outline(x - 7, y - 7, 414, 239, 18, 4, WHITE);
  }
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
