#include "saver.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "api.h"
#include "img.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define MAX_SLIDES 30
#define SLIDE_MS 12000.0
#define FADE_MS 1500.0

static struct {
  char *name, *background;
} slides[MAX_SLIDES];
static int nslides, active, asking, cancelled, started_at_slide;
static double minutes = 5, started_at;

static void clear_slides(void) {
  for (int i = 0; i < nslides; i++) free(slides[i].name), free(slides[i].background);
  nslides = 0;
}

static void came(const jval *answer, void *user) {
  int start = (int)(intptr_t)user;
  asking = 0;
  if (!answer) return;
  minutes = json_num(json_get(answer, "minutes"), minutes);
  /* a key came while the pictures were asked for */
  if (!start || cancelled) return;
  clear_slides();
  const jval *list = json_get(answer, "items");
  for (int i = 0; i < json_len(list) && nslides < MAX_SLIDES; i++) {
    const char *bg = json_str(json_get(json_at(list, i), "background"), "");
    if (!*bg) continue;
    slides[nslides].name = str_dup(json_str(json_get(json_at(list, i), "name"), ""));
    slides[nslides].background = str_dup(bg);
    nslides++;
  }
  /* nothing to show: it stays off until the next key */
  active = nslides > 0;
  started_at = -1;
}

void saver_init(void) { api_get("/app/screensaver?items=0", came, (void *)0); }

void saver_tick(double t, double last_key_at, int allowed) {
  if (active || asking || !allowed || minutes <= 0 || t - last_key_at < minutes * 60000) return;
  asking = 1;
  cancelled = 0;
  api_get("/app/screensaver", came, (void *)1);
}

int saver_active(void) { return active; }

/* a later start asks the server again, with what changed meanwhile */
void saver_stop(void) { active = 0, cancelled = 1; }

void saver_draw(double t) {
  if (!active) return;
  if (started_at < 0) started_at = t, started_at_slide = rand() % nslides;
  double run = t - started_at;
  int at = (started_at_slide + (int)(run / SLIDE_MS)) % nslides, before = (at + nslides - 1) % nslides;
  double into = run - (int)(run / SLIDE_MS) * SLIDE_MS;
  float fade = run < SLIDE_MS ? 1 : (float)clampf((float)(into / FADE_MS), 0, 1);
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(0, 0, 0, 255));
  /* each picture drifts slowly across its 12 seconds, which also keeps an OLED panel safe */
  for (int layer = 0; layer < 2; layer++) {
    int i = layer ? at : before;
    if (!layer && fade >= 1) continue;
    float t01 = (float)clampf((float)((layer ? into : into + SLIDE_MS) / (SLIDE_MS + FADE_MS)), 0, 1);
    float zoom = 1.04f + 0.06f * t01, w = SC_DESIGN_W * zoom, h = SC_DESIGN_H * zoom;
    float dx = (i % 2 ? -1 : 1) * 40 * t01;
    const sc_tex *pic = img_get(slides[i].background, 1280, NULL);
    if (pic) gfx_image(pic, (SC_DESIGN_W - w) / 2 + dx, (SC_DESIGN_H - h) / 2, w, h, 0, layer ? fade : 1, COVER);
  }
  gfx_gradient(0, SC_DESIGN_H * 0.55f, SC_DESIGN_W, SC_DESIGN_H * 0.45f, 0, SC_RGBA(0, 0, 0, 0), SC_RGBA(0, 0, 0, 200), 1);
  text_draw_fit(slides[at].name, 96, SC_DESIGN_H - 150, SC_DESIGN_W * 0.6f, 52, W_EXTRABOLD, C_TEXT(fade * 0.95f));
  char clock[16];
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  if (tm) strftime(clock, sizeof clock, "%H:%M", tm);
  else snprintf(clock, sizeof clock, "--:--");
  text_draw(clock, SC_DESIGN_W - 96 - text_width(clock, 56, W_BOLD), 70, 56, W_BOLD, C_TEXT(0.9f));
}
