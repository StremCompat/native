/* Signing in: a code and its QR to open on the phone at the left, an email and password at the
   right, for a Stremio or a Nuvio account. The server does the asking (/app/login/...); measures
   are the web interface's (login.css). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "keyboard.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

enum { F_PROVIDER, F_EMAIL, F_PASSWORD, F_BUTTON, F_COUNT };

static int gen, nuvio, focus = F_EMAIL, typing, busy;
static char code[32], qr_id[96], status_key[48], error[256];
static char *qr_rows;     /* the QR's modules, row after row, '1' dark */
static int qr_size;
static double every_ms = 2500, next_poll;
static char email[KB_TEXT], password[KB_TEXT];
static keyboard kb;
static double clock_now;
static float lift; /* the page moves up while the keyboard is open */

static void set_status(const char *key) { snprintf(status_key, sizeof status_key, "%s", key); }

static void start_code(void);

static void signed_in(int profiles) {
  gen++;
  set_status("accountConnected");
  if (profiles) profiles_open(1);
  else app_show(&home_screen);
}

static void polled(const jval *answer, void *user) {
  if ((intptr_t)user != gen || !answer) return;
  if (json_bool(json_get(answer, "done"), 0)) signed_in(json_bool(json_get(answer, "profiles"), 0));
  else if (json_bool(json_get(answer, "expired"), 0)) start_code();
}

static void code_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  const jval *rows = json_get(answer, "qr");
  free(qr_rows);
  qr_rows = NULL;
  qr_size = json_len(rows);
  if (!answer || !qr_size) {
    set_status("qrUnavailable");
    return;
  }
  qr_rows = malloc((size_t)qr_size * (size_t)qr_size + 1);
  for (int y = 0; y < qr_size; y++) {
    const char *row = json_str(json_at(rows, y), "");
    for (int x = 0; x < qr_size; x++) qr_rows[y * qr_size + x] = x < (int)strlen(row) ? row[x] : '0';
  }
  snprintf(code, sizeof code, "%s", json_str(json_get(answer, "code"), ""));
  snprintf(qr_id, sizeof qr_id, "%s", json_str(json_get(answer, "id"), ""));
  every_ms = json_num(json_get(answer, "every"), 2.5) * 1000;
  next_poll = clock_now + every_ms;
  set_status("waitingPhone");
}

static void start_code(void) {
  gen++;
  code[0] = qr_id[0] = 0;
  qr_size = 0;
  set_status("generatingCode");
  api_get(nuvio ? "/app/login/start?provider=nuvio" : "/app/login/start?provider=stremio", code_came,
          (void *)(intptr_t)gen);
}

static void poll(void) {
  if (!qr_id[0] || clock_now < next_poll || busy) return;
  next_poll = clock_now + every_ms;
  sbuf p = {0};
  sb_addf(&p, "/app/login/poll?provider=%s&id=", nuvio ? "nuvio" : "stremio");
  sb_add_url(&p, qr_id);
  api_get(p.s, polled, (void *)(intptr_t)gen);
  sb_free(&p);
}

/* email and password: the answer is waited for here, the server signs in */
static void emailed(net_reply *r, void *user) {
  busy = 0;
  if ((intptr_t)user != gen) return;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const jval *a = json_root(d);
  if (json_bool(json_get(a, "done"), 0)) signed_in(json_bool(json_get(a, "profiles"), 0));
  else {
    const char *why = json_str(json_get(a, "error"), "");
    snprintf(error, sizeof error, "%s", *why ? why : tx("signInFailed"));
  }
  json_free(d);
}

static void sign_in(void) {
  if (!email[0] || !password[0]) {
    snprintf(error, sizeof error, "%s", tx("enterCredentials"));
    return;
  }
  sbuf b = {0};
  sb_add(&b, "{\"email\":");
  sb_add_json_str(&b, email);
  sb_add(&b, ",\"password\":");
  sb_add_json_str(&b, password);
  sb_addf(&b, ",\"provider\":\"%s\"}", nuvio ? "nuvio" : "stremio");
  error[0] = 0;
  set_status("signingIn");
  busy = 1;
  net_request("/app/login/email", b.s, NET_HIGH, NULL, emailed, (void *)(intptr_t)gen);
  sb_free(&b);
}

static void enter(void) {
  if (!code[0]) start_code();
}

/* the keyboard writes into the field it was opened on; Done goes to the next one */
static void field_done(void) {
  snprintf(focus == F_EMAIL ? email : password, KB_TEXT, "%s", kb.text);
  typing = 0;
  focus++;
}

static void key(app_key k) {
  if (typing) {
    if (k == K_BACK) {
      typing = 0;
      return;
    }
    if (kb_key(&kb, k) == KB_DONE) field_done();
    else snprintf(focus == F_EMAIL ? email : password, KB_TEXT, "%s", kb.text);
    return;
  }
  if (k == K_UP && focus > 0) focus--;
  else if (k == K_DOWN && focus < F_COUNT - 1) focus++;
  else if (focus == F_PROVIDER && (k == K_LEFT || k == K_RIGHT || k == K_OK)) {
    nuvio = !nuvio;
    error[0] = 0;
    start_code();
  } else if (k == K_OK && (focus == F_EMAIL || focus == F_PASSWORD)) {
    kb_reset(&kb, focus == F_EMAIL ? email : password, focus == F_PASSWORD);
    typing = 1;
  } else if (k == K_OK && focus == F_BUTTON && !busy) {
    sign_in();
  } else if (k == K_BACK) {
    app_quit();
  }
}

static void text(const char *utf8) {
  if (!typing) return;
  kb_type(&kb, utf8);
  snprintf(focus == F_EMAIL ? email : password, KB_TEXT, "%s", kb.text);
}

/* ---------- drawing ---------- */
static void draw_qr(float x, float y) {
  gfx_rect(x, y, 300, 300, 18, C_WHITE);
  if (!qr_size) return;
  /* the modules within the white square, with its own quiet margin */
  float cell = (300 - 24) / (float)(qr_size + 4), ox = x + 12 + 2 * cell, oy = y + 12 + 2 * cell;
  for (int r = 0; r < qr_size; r++)
    for (int c = 0; c < qr_size; c++)
      if (qr_rows[r * qr_size + c] == '1') gfx_rect(ox + c * cell, oy + r * cell, cell + 0.5f, cell + 0.5f, 0, SC_RGBA(0, 0, 0, 255));
}

static float centred(const char *s, float cx, float y, float size, int weight, uint32_t color) {
  float w = text_width(s, size, weight);
  text_draw(s, cx - w / 2, y, size, weight, color);
  return w;
}

static void draw_field(int f, const char *placeholder, const char *value, int secret, float x, float y) {
  int focused = focus == f;
  gfx_rect(x, y, 568, 70, 12, focused ? C_TEXT(0.22f) : C_TEXT(0.10f));
  if (focused) gfx_outline(x - 3, y - 3, 574, 76, 15, 3, C_WHITE);
  keyboard shown;
  kb_reset(&shown, value, secret);
  const char *s = *value ? kb_shown(&shown) : placeholder;
  text_draw_fit(s, x + 24, y + (70 - text_line_height(28)) / 2, 520, 28, W_REGULAR, *value ? C_WHITE : C_TEXT(0.45f));
}

static void frame(double t, float dt) {
  clock_now = t;
  poll();
  lift = ui_approach(lift, typing ? 190 : 0, dt, 60);
  gfx_push_offset(0, -lift);
  /* the two halves side by side, centred, with a line between them */
  float qr_w = 420, form_w = 568, gap = 90, total = qr_w + gap * 2 + form_w, left = (SC_DESIGN_W - total) / 2;
  float cx = left + qr_w / 2, top = 230;
  centred(tx("phoneSignIn"), cx, top, 40, W_BOLD, C_WHITE);
  centred(tx("scanCode"), cx, top + 60, 24, W_REGULAR, C_TEXT(0.72f));
  draw_qr(cx - 150, top + 116);
  if (code[0]) centred(code, cx, top + 436, 46, W_EXTRABOLD, C_WHITE);
  centred(tx(status_key), cx, top + 512, 22, W_REGULAR, C_TEXT(0.72f));
  gfx_rect(left + qr_w + gap, top - 10, 1, 560, 0, C_TEXT(0.14f));
  /* Stremio | Nuvio */
  float fx = left + qr_w + gap * 2, fy = top - 6;
  const char *names[2] = {"Stremio", "Nuvio"};
  float pw = 10;
  for (int i = 0; i < 2; i++) pw += text_width(names[i], 22, W_BOLD) + 40;
  gfx_rect(fx, fy, pw, 52, 26, C_TEXT(0.08f));
  if (focus == F_PROVIDER && !typing) gfx_outline(fx, fy, pw, 52, 26, 3, C_WHITE);
  float px = fx + 5;
  for (int i = 0; i < 2; i++) {
    float w = text_width(names[i], 22, W_BOLD) + 40;
    int on = i == nuvio;
    if (on) gfx_rect(px, fy + 5, w, 42, 21, focus == F_PROVIDER && !typing ? C_WHITE : C_TEXT(0.2f));
    ui_line(names[i], px + 20, fy + 5, 42, 22, on ? W_BOLD : W_REGULAR,
            on && focus == F_PROVIDER && !typing ? C_BG : on ? C_WHITE : C_TEXT(0.72f));
    px += w;
  }
  text_draw(tx("emailSignIn"), fx, fy + 70, 40, W_BOLD, C_WHITE);
  draw_field(F_EMAIL, tx("email"), email, 0, fx, fy + 140);
  draw_field(F_PASSWORD, tx("password"), password, 1, fx, fy + 226);
  int bf = focus == F_BUTTON && !typing;
  float bw = text_width(tx("signIn"), 29, W_BOLD) + 108;
  gfx_rect(fx, fy + 316, bw, 70, 12, bf ? C_WHITE : C_TEXT(0.14f));
  ui_line(tx("signIn"), fx + 54, fy + 316, 70, 29, W_BOLD, bf ? C_BG : C_WHITE);
  if (error[0]) text_draw_wrap(error, fx, fy + 402, 568, 23, W_REGULAR, SC_RGBA(255, 154, 154, 255), 3, 31);
  /* the notes at the bottom */
  centred(tx("accountNote"), SC_DESIGN_W / 2, SC_DESIGN_H - 66 - 30, 24, W_REGULAR, C_TEXT(0.82f));
  centred(tx("unofficialNote"), SC_DESIGN_W / 2, SC_DESIGN_H - 32 - 25, 20, W_REGULAR, C_TEXT(0.6f));
  centred(tx("syncSiteNote"), SC_DESIGN_W / 2, SC_DESIGN_H - 6 - 19, 15, W_REGULAR, C_TEXT(0.42f));
  gfx_pop_offset();
  if (typing) {
    /* the keyboard over the bottom of the screen, under the field it writes into */
    float kw = 13 * 72 - 8, kx = (SC_DESIGN_W - kw) / 2, ky = SC_DESIGN_H - 360;
    gfx_rect(kx - 30, ky - 30, kw + 60, 360, 18, SC_RGBA(20, 20, 22, 250));
    kb_draw(&kb, kx, ky);
  }
}

const screen login_screen = {enter, key, frame, text};
