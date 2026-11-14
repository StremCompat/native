/* Who is watching: the Nuvio profiles as round faces, the one in use first in focus. A profile
   alone is used without asking. Measures are the web interface's (profiles.css). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "img.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define MAX_PROFILES 6
#define FACE 220.0f
#define CARD 250.0f
#define FACE_GAP 44.0f

typedef struct {
  int index, locked;
  char name[96], avatar[512];
  uint32_t color;
} profile;

static profile list[MAX_PROFILES];
static int count, at, gen, loaded, busy, at_start;

static uint32_t hex_color(const char *hex) {
  unsigned v = 0x1E88E5;
  if (hex && hex[0] == '#') sscanf(hex + 1, "%6x", &v);
  return SC_RGBA((v >> 16) & 255, (v >> 8) & 255, v & 255, 255);
}

static void used(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  busy = 0;
  if (json_bool(json_get(answer, "ok"), 0)) {
    home_reload();
    app_show(&home_screen);
  }
}

static void use(int i) {
  if (busy || i >= count || list[i].locked) return;
  busy = 1;
  char path[64];
  snprintf(path, sizeof path, "/app/profile?index=%d", list[i].index);
  api_get(path, used, (void *)(intptr_t)gen);
}

static void listed(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  const jval *rows = json_get(answer, "profiles");
  count = 0;
  at = 0;
  for (int i = 0; i < json_len(rows) && count < MAX_PROFILES; i++) {
    const jval *r = json_at(rows, i);
    profile *p = &list[count++];
    p->index = (int)json_num(json_get(r, "index"), 1);
    p->locked = json_bool(json_get(r, "locked"), 0);
    snprintf(p->name, sizeof p->name, "%s", json_str(json_get(r, "name"), ""));
    snprintf(p->avatar, sizeof p->avatar, "%s", json_str(json_get(r, "avatar"), ""));
    p->color = hex_color(json_str(json_get(r, "color"), NULL));
    if (json_bool(json_get(r, "current"), 0)) at = count - 1;
  }
  loaded = 1;
  /* nothing to ask at the start when there is one profile */
  if (at_start && count == 1 && !list[0].locked) use(0);
  else if (!count) app_show(&home_screen);
}

void profiles_open(int starting) {
  gen++;
  loaded = busy = 0;
  at_start = starting;
  api_get("/app/profiles", listed, (void *)(intptr_t)gen);
  if (starting) app_show(&profiles_screen);
  else app_push(&profiles_screen);
}

static void key(app_key k) {
  if (k == K_LEFT && at > 0) at--;
  else if (k == K_RIGHT && at < count - 1) at++;
  else if (k == K_OK) use(at);
  else if (k == K_BACK && !at_start) app_back();
}

static void frame(double t, float dt) {
  (void)t;
  (void)dt;
  gfx_gradient(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(29, 29, 32, 255), C_BG, 1);
  const char *title = tx("whoIsWatching");
  text_draw(title, (SC_DESIGN_W - text_width(title, 64, W_EXTRABOLD)) / 2, SC_DESIGN_H * 0.15f, 64, W_EXTRABOLD, C_WHITE);
  if (!loaded) return;
  float total = count * CARD + (count - 1) * FACE_GAP, x = (SC_DESIGN_W - total) / 2, y = SC_DESIGN_H * 0.15f + 77 + 70;
  for (int i = 0; i < count; i++, x += CARD + FACE_GAP) {
    const profile *p = &list[i];
    int focused = i == at;
    float s = focused ? FACE * 1.06f : FACE, fx = x + (CARD - s) / 2, fy = y + (FACE - s) / 2;
    gfx_push_opacity(p->locked ? 0.45f : 1);
    const sc_tex *pic = img_get(p->avatar[0] ? p->avatar : NULL, 256, NULL);
    gfx_rect(fx, fy, s, s, s / 2, p->color);
    if (pic) gfx_image(pic, fx, fy, s, s, s / 2, 1, COVER);
    else {
      /* the first letter of the name on the profile's colour */
      char letter[8] = {0};
      const char *n = p->name;
      utf8_next(&n);
      memcpy(letter, p->name, (size_t)(n - p->name) < sizeof letter ? (size_t)(n - p->name) : 1);
      char up[16];
      utf8_upper(up, sizeof up, letter);
      float lw = text_width(up, 92, W_EXTRABOLD);
      ui_line(up, fx + (s - lw) / 2, fy, s, 92, W_EXTRABOLD, C_WHITE);
    }
    gfx_pop_opacity();
    if (focused) gfx_outline(fx - 7, fy - 7, s + 14, s + 14, s / 2 + 7, 7, C_WHITE);
    char number[12];
    snprintf(number, sizeof number, "%d", p->index);
    const char *name = p->name[0] ? p->name : txf("profileNumber", number, NULL);
    float nw = text_width(name, 32, W_BOLD);
    text_draw_fit(name, x + (nw < CARD ? (CARD - nw) / 2 : 0), y + FACE + 24, CARD, 32, W_BOLD, focused ? C_WHITE : C_TEXT(0.72f));
    if (p->locked) {
      const char *note = tx("profileLocked");
      float lw = text_width(note, 19, W_REGULAR);
      text_draw_fit(note, x + (lw < CARD ? (CARD - lw) / 2 : 0), y + FACE + 24 + 48, CARD, 19, W_REGULAR, C_TEXT(0.55f));
    }
  }
  const char *hint = tx("profilesHint");
  text_draw(hint, (SC_DESIGN_W - text_width(hint, 22, W_REGULAR)) / 2, SC_DESIGN_H - 48 - 27, 22, W_REGULAR, C_TEXT(0.55f));
}

static void enter(void) {}

const screen profiles_screen = {enter, key, frame, NULL, 0};
