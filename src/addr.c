#include "addr.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "gfx.h"
#include "keyboard.h"
#include "net.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define MAX_SUGGESTED 12
#define POLL_MS 2000.0

static int active, kind, gen, on_suggest, sug_at, nsug, polling;
static char title[96], phone_url[160];
static char sug_name[MAX_SUGGESTED][96], sug_url[MAX_SUGGESTED][400];
static char *qr;
static int qr_size;
static double last_poll;
static addr_done_fn done;
static keyboard kb;

static void finish(const char *value) {
  addr_done_fn fn = done;
  addr_close();
  if (fn && value && *value) fn(value);
}

static void phone_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen || !active) return;
  const jval *rows = json_get(answer, "qr");
  qr_size = json_len(rows);
  free(qr);
  qr = qr_size ? malloc((size_t)qr_size * (size_t)qr_size + 1) : NULL;
  for (int y = 0; qr && y < qr_size; y++) {
    const char *row = json_str(json_at(rows, y), "");
    for (int x = 0; x < qr_size; x++) qr[y * qr_size + x] = x < (int)strlen(row) ? row[x] : '0';
  }
  snprintf(phone_url, sizeof phone_url, "%s", json_str(json_get(answer, "url"), ""));
  if (!qr) snprintf(phone_url, sizeof phone_url, "%s", tx("phoneUnavailable"));
}

/* the answer of the POST that opened the phone page */
static void phone_started(net_reply *r, void *user) {
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  phone_came(json_root(d), user);
  json_free(d);
}

static void polled(const jval *answer, void *user) {
  polling = 0;
  if ((intptr_t)user != gen || !active) return;
  const char *value = json_str(json_get(answer, "value"), NULL);
  if (value) finish(value);
}

static void suggested_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen || !active) return;
  const jval *list = json_get(answer, "lists");
  nsug = 0;
  for (int i = 0; i < json_len(list) && nsug < MAX_SUGGESTED; i++, nsug++) {
    snprintf(sug_name[nsug], sizeof sug_name[0], "%s", json_str(json_get(json_at(list, i), "name"), ""));
    snprintf(sug_url[nsug], sizeof sug_url[0], "%s", json_str(json_get(json_at(list, i), "url"), ""));
  }
}

void addr_open(const char *t, int k, addr_done_fn fn) {
  active = 1;
  kind = k;
  done = fn;
  gen++;
  on_suggest = sug_at = nsug = polling = 0;
  qr_size = 0;
  free(qr);
  qr = NULL;
  phone_url[0] = 0;
  snprintf(title, sizeof title, "%s", t);
  kb_reset(&kb, k == ADDR_TEXT ? "" : "https://", 0);
  if (k != ADDR_TEXT) net_request("/app/phone/start", "{}", NET_HIGH, NULL, phone_started, (void *)(intptr_t)gen);
  if (k == ADDR_M3U) api_get("/app/channels/suggested", suggested_came, (void *)(intptr_t)gen);
}

int addr_active(void) { return active; }

void addr_close(void) {
  if (active && kind != ADDR_TEXT) net_request("/app/phone/stop", "{}", NET_LOW, NULL, NULL, NULL);
  active = 0;
  free(qr);
  qr = NULL;
}

void addr_key(app_key k) {
  if (on_suggest) {
    if (k == K_LEFT && sug_at > 0) sug_at--;
    else if (k == K_RIGHT && sug_at < nsug - 1) sug_at++;
    else if (k == K_DOWN) on_suggest = 0;
    else if (k == K_OK) finish(sug_url[sug_at]);
    else if (k == K_BACK) addr_close();
    return;
  }
  if (k == K_BACK) return addr_close();
  int r = kb_key(&kb, k);
  if (r == KB_DONE) finish(kb.text);
  else if (r == KB_UP && nsug) on_suggest = 1;
}

/* where the keys were drawn last, for the pointer */
static float keys_x, keys_y;
#define SUGGEST_HIT 9000

void addr_point(float x, float y, int press) {
  if (x < 180 || x >= SC_DESIGN_W - 180 || y < 90 || y >= SC_DESIGN_H - 90) {
    if (press) addr_close();
    return;
  }
  int id = hit_at(x, y);
  if (id >= SUGGEST_HIT && id - SUGGEST_HIT < nsug) {
    on_suggest = 1;
    sug_at = id - SUGGEST_HIT;
    if (press) finish(sug_url[sug_at]);
    return;
  }
  on_suggest = 0;
  if (kb_point(&kb, x - keys_x, y - keys_y, press) == KB_DONE) finish(kb.text);
}

void addr_text(const char *utf8) {
  if (active && !on_suggest) kb_type(&kb, utf8);
}

void addr_draw(void) {
  if (!active) return;
  /* the phone page is asked every two seconds while the panel is up */
  if (kind != ADDR_TEXT && qr && !polling && now_ms() - last_poll > POLL_MS) {
    last_poll = now_ms();
    polling = 1;
    api_get("/app/phone/poll", polled, (void *)(intptr_t)gen);
  }
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(6, 6, 7, 235));
  float bx = 180, by = 90, bw = SC_DESIGN_W - 360, bh = SC_DESIGN_H - 180;
  gfx_rect(bx, by, bw, bh, 22, SC_RGBA(21, 21, 23, 255));
  text_draw(title, bx + 46, by + 40, 36, W_EXTRABOLD, C_WHITE);
  float fx = bx + 46, fy = by + 104, fw = kind == ADDR_TEXT ? bw - 92 : bw - 92 - 420;
  gfx_rect(fx, fy, fw, 64, 14, SC_RGBA(42, 42, 45, 255));
  text_draw_fit(kb.text, fx + 22, fy + (64 - text_line_height(28)) / 2, fw - 44, 28, W_REGULAR, C_WHITE);
  float ky = fy + 96;
  /* the suggested lists, between the field and the keys */
  if (nsug) {
    text_draw(tx("suggestedLists"), fx, ky, 20, W_BOLD, C_TEXT(0.6f));
    float sx = fx;
    for (int i = 0; i < nsug && sx < fx + fw; i++) {
      float w = text_width(sug_name[i], 20, W_SEMIBOLD) + 40;
      int f = on_suggest && i == sug_at;
      gfx_rect(sx, ky + 34, w, 48, 24, f ? C_WHITE : C_TEXT(0.12f));
      hit_add(SUGGEST_HIT + i, sx, ky + 34, w, 48);
      ui_line(sug_name[i], sx + 20, ky + 34, 48, 20, W_SEMIBOLD, f ? C_BG : C_WHITE);
      sx += w + 12;
    }
    ky += 104;
  }
  kb_draw(&kb, fx, ky);
  keys_x = fx, keys_y = ky;
  if (kind != ADDR_TEXT) {
    float qx = bx + bw - 46 - 360;
    if (qr) ui_qr(qr, qr_size, qx + 30, fy, 300);
    text_draw_wrap(tx("phoneScan"), qx, fy + 330, 360, 21, W_REGULAR, C_TEXT(0.8f), 4, 30);
    text_draw_fit(phone_url, qx, fy + 460, 360, 17, W_REGULAR, C_TEXT(0.45f));
  }
}
