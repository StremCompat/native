/* The first screen: waits for the app's local server, as the web launcher did (app/index.html).
   The server may take a while to start on a TV, and after an update the previous version's
   server can still be the one answering for a moment; only this version's counts. Once it
   answers, app_ready loads the texts and opens the first real screen. After 90 seconds it
   says the server did not start, with a code to report. */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gfx.h"
#include "json.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

/* the version the server must report (build.sh sets it for the TVs; a computer takes any) */
#ifndef APP_VERSION
#define APP_VERSION ""
#endif

#define GIVE_UP_MS 90000.0
#define ASK_EVERY_MS 400.0

static double started, last_ask;
static int asking, done;
static char other[32]; /* another version's server, still answering */

static void health_came(net_reply *r, void *user) {
  (void)user;
  asking = 0;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const char *version = json_str(json_get(json_root(d), "version"), "");
  if (d && (!APP_VERSION[0] || !strcmp(version, APP_VERSION))) done = 1;
  else if (version[0]) snprintf(other, sizeof other, "%s", version);
  json_free(d);
  if (done) app_ready();
}

static void enter(void) {
  started = now_ms();
  last_ask = 0;
  asking = done = 0;
  other[0] = 0;
}

static void key(app_key k) {
  if (k == K_BACK) app_quit();
}

/* the texts come from the server, so these few are written here, as in the web launcher */
static int portuguese(void) { return !strncmp(app_language(), "pt", 2); }

static void frame(double t, float dt) {
  (void)t;
  (void)dt;
  double waited = now_ms() - started;
  if (!done && !asking && waited < GIVE_UP_MS && now_ms() - last_ask >= ASK_EVERY_MS) {
    last_ask = now_ms();
    asking = 1;
    net_request("/health", NULL, NET_HIGH, NULL, health_came, NULL);
  }
  const char *line, *detail = "";
  char code[96] = "";
  if (waited >= GIVE_UP_MS) {
    line = portuguese() ? "O servidor do app n\xC3\xA3o iniciou" : "The app's server did not start";
    detail = portuguese() ? "Desligue a TV da tomada por 30 segundos e abra de novo." : "Unplug the TV for 30 seconds and open the app again.";
    snprintf(code, sizeof code, "%s %s", other[0] ? "STALE_SERVICE" : "RUNTIME_START_TIMEOUT", other[0] ? other : APP_VERSION);
  } else if (waited > 20000) {
    line = portuguese() ? "Quase l\xC3\xA1\xE2\x80\xA6" : "Almost there\xE2\x80\xA6";
  } else {
    line = portuguese() ? "Iniciando\xE2\x80\xA6" : "Starting\xE2\x80\xA6";
  }
  float cy = SC_DESIGN_H / 2.0f;
  float w = text_width("StremCompat", 64, W_BOLD);
  ui_line("StremCompat", (SC_DESIGN_W - w) / 2, cy - 120, 80, 64, W_BOLD, C_WHITE);
  w = text_width(line, 30, W_REGULAR);
  ui_line(line, (SC_DESIGN_W - w) / 2, cy - 20, 44, 30, W_REGULAR, C_TEXT(0.75f));
  if (detail[0]) {
    w = text_width(detail, 26, W_REGULAR);
    ui_line(detail, (SC_DESIGN_W - w) / 2, cy + 40, 40, 26, W_REGULAR, C_TEXT(0.6f));
    w = text_width(code, 22, W_REGULAR);
    ui_line(code, (SC_DESIGN_W - w) / 2, cy + 90, 36, 22, W_REGULAR, C_TEXT(0.4f));
  }
}

const screen boot_screen = {enter, key, frame, NULL, 0};
