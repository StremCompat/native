/* The first screen: waits for the app's local server, as the web launcher did (app/index.html),
   and looks like it: the logo breathing, a loading line, a status that changes and a hint.
   The server may take a while to start on a TV, and after an update the previous version's
   server can still be the one answering for a moment; only this version's counts. Once it
   answers, app_ready loads the texts and opens the first real screen. After 90 seconds it says
   the service did not answer, with what the service and the server said, for a photo. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gfx.h"
#include "img.h"
#include "json.h"
#include "net.h"
#include "platform.h"
#include "qr.h"
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
/* without this version's server by then, the app starts it itself */
#define RUN_ITSELF_MS 6000.0
#define BG SC_RGBA(9, 11, 18, 255)
#define ACCENT SC_RGBA(34, 211, 238, 255)

static double started, last_ask;
static int asking, done, failed, ran_itself;
static char server_note[96]; /* the last answer to /health, for the failure screen */
static int stale;           /* that answer came from another version's server */
static sc_tex logo;

static void health_came(net_reply *r, void *user) {
  (void)user;
  asking = 0;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const char *version = json_str(json_get(json_root(d), "version"), "");
  done = d && (!APP_VERSION[0] || !strcmp(version, APP_VERSION));
  stale = !done && version[0];
  if (stale) snprintf(server_note, sizeof server_note, "server %s", version);
  else if (!done) snprintf(server_note, sizeof server_note, r->status ? "server HTTP %d" : "server unreachable", r->status);
  json_free(d);
  if (done) app_ready();
}

static void enter(void) {
  if (!logo.id) logo = img_load_file(app_asset_dir("logo.png"), 0);
  started = now_ms();
  last_ask = 0;
  asking = done = failed = stale = ran_itself = 0;
  snprintf(server_note, sizeof server_note, "server unreachable");
}

static void key(app_key k) {
  if (k == K_BACK) app_quit();
  /* Try again: the service is asked once more */
  else if (k == K_OK && failed) {
    platform_start_server();
    enter();
  }
}

/* the texts come from the server, so these few are written here, as in the web launcher */
static int portuguese(void) { return !strncmp(app_language(), "pt", 2); }
#define T(pt, en) (portuguese() ? (pt) : (en))

/* the repository its reports go to */
#if defined(__EMSCRIPTEN__)
#define REPORT_REPO "samsung"
#elif defined(__ANDROID__)
#define REPORT_REPO "android-tv"
#else
#define REPORT_REPO "webos"
#endif

static void centred(const char *s, float y, float box, float size, int weight, uint32_t color);

/* A QR code a phone scans to open a bug report already filled in with what the screen says (no
   keys or account in it), made here because the server is what failed */
static void draw_report(const char *code, float y) {
  static char modules[QR_MAX_SIZE * QR_MAX_SIZE + 1], made_for[300];
  static int size;
  if (strcmp(made_for, code)) {
    snprintf(made_for, sizeof made_for, "%s", code);
    sbuf url = {0};
    sb_add(&url, "https://github.com/StremCompat/" REPORT_REPO "/issues/new?title=");
    char title[120];
    snprintf(title, sizeof title, "Native start: %.100s", code);
    sb_add_url(&url, title);
    size = qr_encode(url.s, modules);
    sb_free(&url);
  }
  if (!size) return;
  float box = 190;
  ui_qr(modules, size, (SC_DESIGN_W - box) / 2, y, box);
  centred(T("Escaneie com o celular para relatar o problema", "Scan with your phone to report it"), y + box + 14, 24, 15,
          W_REGULAR, C_TEXT(0.5f));
}

static void centred(const char *s, float y, float box, float size, int weight, uint32_t color) {
  float w = text_width(s, size, weight);
  ui_line(s, (SC_DESIGN_W - w) / 2, y, box, size, weight, color);
}

static void frame(double t, float dt) {
  (void)dt;
  double waited = now_ms() - started;
  if (!done && !failed && waited >= GIVE_UP_MS) failed = 1;
  if (!done && !ran_itself && waited >= RUN_ITSELF_MS) {
    ran_itself = 1;
    platform_run_server();
  }
  if (!done && !failed && !asking && now_ms() - last_ask >= ASK_EVERY_MS) {
    last_ask = now_ms();
    asking = 1;
    net_request("/health", NULL, NET_HIGH, NULL, health_came, NULL);
  }
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, BG);
  float cy = SC_DESIGN_H / 2.0f - 90;
  /* the logo breathes while it loads (web: opacity .72 to 1 over 1.8 s) */
  float breath = failed ? 0.72f : 0.86f + 0.14f * (float)sin(t / 1800.0 * 6.2831853);
  if (logo.id) gfx_image(&logo, (SC_DESIGN_W - 94) / 2.0f, cy - 94, 94, 94, 0, breath, 0);
  centred("StremCompat", cy + 18, 40, 27, W_BOLD, C_WHITE);
  float ty = cy + 86;
  if (!failed) {
    /* a short bar sliding across a track (web: .btrack / .bfill) */
    float tw = 220, fw = tw * 0.34f, tx = (SC_DESIGN_W - tw) / 2;
    float p = (float)fmod(t, 1450.0) / 1450.0f, eased = p < 0.5f ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p);
    gfx_rect(tx, ty, tw, 3, 1.5f, C_TEXT(0.1f));
    gfx_push_clip(tx, ty, tw, 3);
    gfx_rect(tx + fw * (-1.2f + 4.5f * eased), ty, fw, 3, 1.5f, ACCENT);
    gfx_pop_clip();
  }
  float y = ty + 25;
  if (failed) {
    centred(stale ? T("Uma vers\xC3\xA3o anterior do app ainda est\xC3\xA1 rodando na TV.", "An earlier version of the app is still running on the TV.")
                  : T("O servi\xC3\xA7o do app n\xC3\xA3o respondeu.", "The app service did not respond."),
            y, 30, 19, W_REGULAR, SC_RGBA(255, 154, 154, 255));
    centred(T("Tire a TV da tomada por 30 segundos e abra o app de novo.", "Unplug the TV for 30 seconds and open the app again."), y + 40, 26, 16,
            W_REGULAR, C_TEXT(0.34f));
    const char *retry = T("Tentar de novo", "Try again");
    float bw = text_width(retry, 21, W_BOLD) + 68, bx = (SC_DESIGN_W - bw) / 2;
    gfx_rect(bx, y + 90, bw, 54, 12, C_WHITE);
    ui_line(retry, bx + 34, y + 90, 54, 21, W_BOLD, C_BG);
    char code[300];
    snprintf(code, sizeof code, "%s %s \xC2\xB7 %s \xC2\xB7 %s", stale ? "STALE_SERVICE" : "RUNTIME_START_TIMEOUT", APP_VERSION,
             platform_server_status(), server_note);
    centred(code, y + 164, 24, 15, W_REGULAR, C_TEXT(0.34f));
    draw_report(code, y + 204);
    return;
  }
  centred(waited > 20000 ? T("Quase pronto\xE2\x80\xA6", "Almost ready\xE2\x80\xA6")
          : waited > 3000 ? T("Preparando o servidor local\xE2\x80\xA6", "Preparing the local server\xE2\x80\xA6")
                          : T("Iniciando\xE2\x80\xA6", "Starting\xE2\x80\xA6"),
          y, 30, 19, W_REGULAR, C_TEXT(0.55f));
  if (waited > 8000)
    centred(T("Na primeira abertura depois de instalar, isso pode levar alguns segundos.", "The first launch after installing can take a few seconds."),
            y + 40, 26, 16, W_REGULAR, C_TEXT(0.34f));
}

const screen boot_screen = {enter, key, frame, NULL, 0};
