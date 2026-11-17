/* Settings in two columns, as the web interface lays them out: the groups at the left, the
   options of the chosen group at the right. OK flips a switch, starts changing a value (Left
   and Right, OK keeps it, Back puts it back) or runs an action. The server holds the options
   and their values (/app/settings); measures are the web interface's (screens.css #setWrap). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "keyboard.h"
#include "nav.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define LEFT 176.0f
#define CATS_Y 152.0f
#define PANE_X (LEFT + 390)
#define PANE_W 1100.0f
#define ITEM_H 80.0f
#define MAX_GROUPS 16
#define MAX_ITEMS 24
#define MAX_OPTIONS 40

typedef struct {
  char k[48], label[128], value[128], chosen[64];
  int action, is_switch, on, noptions;
  char options[MAX_OPTIONS][2][64]; /* value and its name */
} item;

typedef struct {
  char name[96], id[32];
  item items[MAX_ITEMS];
  int n;
} group;

static group groups[MAX_GROUPS];
static int ngroups, loaded, in_items, group_at, item_at, editing;
static char email[160], plan[96], original[64];
static float scroll;

/* what an action opens over Settings: diagnostics, the support QR, the server address */
enum { P_NONE, P_DIAG, P_SUPPORT, P_SERVER };
#define QR_MAX 64
static int panel, nlines, qr_size;
static char lines[16][240], qr[QR_MAX * QR_MAX + 1], support_text[400], support_address[96], server_address[200];
static keyboard kb;

static void diag_came(const jval *answer, void *user) {
  (void)user;
  const jval *list = json_get(answer, "lines");
  nlines = 0;
  for (int i = 0; i < json_len(list) && nlines < 16; i++)
    snprintf(lines[nlines++], sizeof lines[0], "%s", json_str(json_at(list, i), ""));
  if (!nlines) snprintf(lines[nlines++], sizeof lines[0], "%s", tx("diagUnavailable"));
}

static void support_came(const jval *answer, void *user) {
  (void)user;
  const jval *rows = json_get(answer, "qr");
  qr_size = json_len(rows) <= QR_MAX ? json_len(rows) : 0;
  for (int y = 0; y < qr_size; y++) {
    const char *row = json_str(json_at(rows, y), "");
    for (int x = 0; x < qr_size; x++) qr[y * qr_size + x] = x < (int)strlen(row) ? row[x] : '0';
  }
  snprintf(support_text, sizeof support_text, "%s", json_str(json_get(answer, "text"), ""));
  snprintf(support_address, sizeof support_address, "%s", json_str(json_get(answer, "address"), ""));
}

/* the streaming server's address shows as the value of its row */
static void server_came(const jval *answer, void *user) {
  (void)user;
  snprintf(server_address, sizeof server_address, "%s", json_str(json_get(answer, "address"), ""));
}

static void server_saved(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const jval *a = json_root(d);
  if (json_bool(json_get(a, "ok"), 0))
    snprintf(server_address, sizeof server_address, "%s", json_str(json_get(a, "address"), kb.text));
  else
    ui_toast(tx("serverInvalid"));
  json_free(d);
}

static void read_model(const jval *answer) {
  const jval *gs = json_get(answer, "groups");
  if (gs->type != J_ARR) return;
  snprintf(email, sizeof email, "%s", json_str(json_get(json_get(answer, "account"), "email"), ""));
  snprintf(plan, sizeof plan, "%s", json_str(json_get(json_get(answer, "account"), "plan"), ""));
  ngroups = 0;
  for (int g = 0; g < json_len(gs) && ngroups < MAX_GROUPS; g++) {
    const jval *gv = json_at(gs, g), *items = json_get(gv, "items");
    group *gr = &groups[ngroups++];
    snprintf(gr->name, sizeof gr->name, "%s", json_str(json_get(gv, "name"), ""));
    snprintf(gr->id, sizeof gr->id, "%s", json_str(json_get(gv, "id"), ""));
    gr->n = 0;
    for (int i = 0; i < json_len(items) && gr->n < MAX_ITEMS; i++) {
      const jval *iv = json_at(items, i), *opts = json_get(iv, "options");
      item *it = &gr->items[gr->n++];
      snprintf(it->k, sizeof it->k, "%s", json_str(json_get(iv, "k"), ""));
      snprintf(it->label, sizeof it->label, "%s", json_str(json_get(iv, "label"), ""));
      snprintf(it->value, sizeof it->value, "%s", json_str(json_get(iv, "value"), ""));
      snprintf(it->chosen, sizeof it->chosen, "%s", json_str(json_get(iv, "chosen"), ""));
      it->action = json_bool(json_get(iv, "action"), 0);
      it->is_switch = json_bool(json_get(iv, "isSwitch"), 0);
      it->on = json_bool(json_get(iv, "on"), 0);
      it->noptions = 0;
      for (int o = 0; o < json_len(opts) && it->noptions < MAX_OPTIONS; o++) {
        snprintf(it->options[it->noptions][0], 64, "%s", json_str(json_get(json_at(opts, o), "v"), ""));
        snprintf(it->options[it->noptions][1], 64, "%s", json_str(json_get(json_at(opts, o), "label"), ""));
        it->noptions++;
      }
    }
  }
  if (group_at >= ngroups) group_at = 0;
  loaded = 1;
}

static void model_came(const jval *answer, void *user) {
  (void)user;
  if (answer) read_model(answer);
}

static item *current(void) {
  return group_at < ngroups && item_at < groups[group_at].n ? &groups[group_at].items[item_at] : NULL;
}

/* a new value: kept by the server, which answers the options again (names may change with it) */
static void changed(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  if (d) read_model(json_root(d));
  json_free(d);
}

static void store(const item *it, const char *value) {
  sbuf b = {0};
  sb_add(&b, "{\"k\":");
  sb_add_json_str(&b, it->k);
  sb_add(&b, ",\"v\":");
  sb_add_json_str(&b, value);
  sb_add(&b, "}");
  net_request("/app/settings", b.s, NET_HIGH, NULL, changed, NULL);
  sb_free(&b);
  /* the interface's own texts follow a new language */
  if (!strcmp(it->k, "uiLanguage")) {
    api_load_strings();
    home_reload();
  }
}

static void step_value(item *it, int dir) {
  int at = 0;
  for (int o = 0; o < it->noptions; o++)
    if (!strcmp(it->options[o][0], it->chosen)) at = o;
  at = (at + dir + it->noptions) % it->noptions;
  /* through a copy: the compiler cannot tell the option and the value apart in the same item */
  char v[64], name[64];
  snprintf(v, sizeof v, "%s", it->options[at][0]);
  snprintf(name, sizeof name, "%s", it->options[at][1]);
  snprintf(it->chosen, sizeof it->chosen, "%s", v);
  snprintf(it->value, sizeof it->value, "%s", name);
  store(it, it->chosen);
}

static void action_done(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  const jval *a = json_root(d);
  const char *message = json_str(json_get(a, "message"), "");
  if (*message) ui_toast(message);
  if (json_bool(json_get(a, "signedOut"), 0)) {
    home_reload();
    app_show(&login_screen);
  }
  if (json_bool(json_get(a, "changed"), 0)) home_reload();
  json_free(d);
}

static void speed_came(const jval *answer, void *user) {
  (void)user;
  if (!answer || json_get(answer, "latencyMs")->type != J_NUM) {
    ui_toast(txf("connectionFailed", json_str(json_get(answer, "error"), "\xE2\x80\x94"), NULL));
    return;
  }
  char ms[16], mbps[16];
  snprintf(ms, sizeof ms, "%.0f", json_num(json_get(answer, "latencyMs"), 0));
  char line[300];
  snprintf(line, sizeof line, "%s", txf("connectionLatency", ms, NULL));
  if (json_get(answer, "mbps")->type == J_NUM) {
    snprintf(mbps, sizeof mbps, "%.1f", json_num(json_get(answer, "mbps"), 0));
    snprintf(line + strlen(line), sizeof line - strlen(line), "  \xC2\xB7  %s",
             txf("connectionSpeed", mbps, json_str(json_get(answer, "host"), "")));
  }
  ui_toast(line);
}

static void run_action(const item *it) {
  if (!strcmp(it->k, "__profile")) return profiles_open(0);
  if (!strcmp(it->k, "__diag")) {
    panel = P_DIAG;
    nlines = 0;
    api_get("/app/diag", diag_came, NULL);
    return;
  }
  if (!strcmp(it->k, "__support")) {
    panel = P_SUPPORT;
    api_get("/app/support", support_came, NULL);
    return;
  }
  if (!strcmp(it->k, "__server")) {
    panel = P_SERVER;
    kb_reset(&kb, server_address, 0);
    return;
  }
  if (!strcmp(it->k, "__rows")) return app_push(&rows_screen);
  if (!strcmp(it->k, "__m3u")) return channels_open(1);
  if (!strcmp(it->k, "__m3uLists")) return channels_open(2);
  if (!strcmp(it->k, "__reload")) {
    api_load_strings();
    home_reload();
    app_show(&home_screen);
    return;
  }
  if (!strcmp(it->k, "__speed")) {
    ui_toast(tx("testingConnection"));
    api_get("/speedtest", speed_came, NULL);
    return;
  }
  if (!strcmp(it->k, "__sync")) ui_toast(tx("syncing"));
  sbuf b = {0};
  sb_add(&b, "{\"k\":");
  sb_add_json_str(&b, it->k);
  sb_add(&b, "}");
  net_request("/app/settings/action", b.s, NET_HIGH, NULL, action_done, NULL);
  sb_free(&b);
}

static void key(app_key k) {
  if (nav_has_focus()) {
    nav_key(k);
    return;
  }
  if (panel == P_SERVER) {
    if (k == K_BACK) panel = P_NONE;
    else if (kb_key(&kb, k) == KB_DONE) {
      panel = P_NONE;
      sbuf b = {0};
      sb_add(&b, "{\"address\":");
      sb_add_json_str(&b, kb.text);
      sb_add(&b, "}");
      net_request("/serverconfig", b.s, NET_HIGH, NULL, server_saved, NULL);
      sb_free(&b);
    }
    return;
  }
  if (panel) {
    if (k == K_BACK || k == K_OK) panel = P_NONE;
    return;
  }
  item *it = current();
  if (editing && it) {
    if (k == K_LEFT || k == K_RIGHT) step_value(it, k == K_RIGHT ? 1 : -1);
    else if (k == K_OK) editing = 0;
    else if (k == K_BACK) {
      if (strcmp(it->chosen, original)) store(it, original);
      editing = 0;
    }
    return;
  }
  if (!in_items) {
    if (k == K_UP && group_at > 0) group_at--, item_at = 0;
    else if (k == K_DOWN && group_at < ngroups - 1) group_at++, item_at = 0;
    else if ((k == K_RIGHT || k == K_OK) && ngroups) in_items = 1;
    else if (k == K_LEFT) nav_focus(NAV_SETTINGS);
    else if (k == K_BACK) app_show(&home_screen);
    return;
  }
  if (k == K_UP && item_at > 0) item_at--;
  else if (k == K_DOWN && it && item_at < groups[group_at].n - 1) item_at++;
  else if (k == K_LEFT || k == K_BACK) in_items = 0;
  else if (k == K_OK && it) {
    if (it->action) run_action(it);
    else if (it->is_switch) {
      it->on = !it->on;
      store(it, it->on ? "true" : "false");
    } else {
      snprintf(original, sizeof original, "%s", it->chosen);
      editing = 1;
    }
  }
}

static void draw_item(const item *it, float y, int focused, int out) {
  int edit = focused && editing;
  uint32_t back = edit ? C_WHITE : out ? SC_RGBA(229, 72, 77, focused ? 255 : 41) : focused ? C_TEXT(0.16f) : C_TEXT(0.06f);
  gfx_rect(PANE_X, y, PANE_W, ITEM_H, 14, back);
  if (focused && !edit && !out) gfx_outline(PANE_X, y, PANE_W, ITEM_H, 14, 3, C_WHITE);
  uint32_t fore = edit ? C_BG : out ? (focused ? C_WHITE : SC_RGBA(255, 180, 180, 255)) : C_WHITE;
  ui_line(it->label, PANE_X + 28, y, ITEM_H, 27, W_SEMIBOLD, fore);
  float right = PANE_X + PANE_W - 28;
  if (it->is_switch) {
    gfx_rect(right - 66, y + 22, 66, 36, 18, it->on ? C_WHITE : C_TEXT(0.24f));
    gfx_rect(right - 66 + (it->on ? 34 : 4), y + 26, 28, 28, 14, it->on ? C_BG : C_WHITE);
  } else if (!it->action || !strcmp(it->k, "__server")) {
    char shown[260];
    snprintf(shown, sizeof shown, "%s  \xE2\x80\xBA", !strcmp(it->k, "__server") ? server_address : it->value);
    float w = text_width(shown, 24, W_BOLD) + 34;
    gfx_rect(right - w, y + (ITEM_H - 46) / 2, w, 46, 18, edit ? C_BG : C_TEXT(0.08f));
    ui_line(shown, right - w + 17, y + (ITEM_H - 46) / 2, 46, 24, W_BOLD, edit ? C_WHITE : C_TEXT(0.88f));
  }
}

static void draw_panel(void) {
  if (!panel) return;
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(0, 0, 0, 170));
  float w = 1100, h = panel == P_SUPPORT ? 420 : panel == P_SERVER ? 520 : 70 + 40.0f * (nlines ? nlines : 1),
        x = (SC_DESIGN_W - w) / 2, y = (SC_DESIGN_H - h) / 2;
  gfx_rect(x, y, w, h, 18, SC_RGBA(20, 20, 22, 250));
  if (panel == P_DIAG) {
    if (!nlines) text_draw(tx("diagLoading"), x + 40, y + 35, 24, W_REGULAR, C_TEXT(0.8f));
    for (int i = 0; i < nlines; i++) text_draw_fit(lines[i], x + 40, y + 35 + i * 40, w - 80, 24, W_REGULAR, C_TEXT(0.86f));
  } else if (panel == P_SUPPORT) {
    ui_qr(qr_size ? qr : NULL, qr_size, x + 40, y + 40, 340);
    text_draw_wrap(support_text, x + 420, y + 60, w - 460, 26, W_REGULAR, C_TEXT(0.86f), 6, 36);
    text_draw(support_address, x + 420, y + 320, 28, W_BOLD, C_WHITE);
  } else {
    float kw = 13 * 72 - 8, kx = (SC_DESIGN_W - kw) / 2;
    text_draw(tx("serverAddress"), kx, y + 30, 24, W_BOLD, C_TEXT(0.7f));
    gfx_rect(kx, y + 72, kw, 60, 10, C_TEXT(0.1f));
    text_draw_fit(kb.text, kx + 18, y + 72 + (60 - text_line_height(26)) / 2, kw - 36, 26, W_REGULAR, C_WHITE);
    kb_draw(&kb, kx, y + 160);
  }
}

static void frame(double t, float dt) {
  (void)t;
  text_draw(tx("navSettings"), LEFT + 4, 56, 50, W_EXTRABOLD, C_WHITE);
  if (!loaded) {
    nav_draw(NAV_SETTINGS, dt);
    return;
  }
  /* the groups */
  float y = CATS_Y;
  for (int g = 0; g < ngroups; g++) {
    int out = !strcmp(groups[g].id, "logout"), f = !in_items && g == group_at && !nav_has_focus(), cur = g == group_at;
    if (out) y += 28;
    uint32_t back = f ? (out ? SC_RGBA(229, 72, 77, 255) : C_WHITE) : cur ? (out ? SC_RGBA(229, 72, 77, 41) : C_TEXT(0.1f)) : 0;
    if (back) gfx_rect(LEFT, y, 330, 59, 12, back);
    uint32_t fore = f ? (out ? C_WHITE : SC_RGBA(18, 16, 26, 255)) : out ? SC_RGBA(255, 138, 138, 255) : cur ? C_WHITE : C_TEXT(0.66f);
    text_draw_fit(groups[g].name, LEFT + 24, y + (59 - text_line_height(27)) / 2, 282, 27, W_SEMIBOLD, fore);
    y += 59 + 6;
  }
  /* the options of the chosen group, with the account on top of Account and Sign out */
  const group *gr = &groups[group_at];
  float top = CATS_Y;
  if (!strcmp(gr->id, "account") || !strcmp(gr->id, "logout")) {
    gfx_rect(PANE_X, top, PANE_W, 116, 16, C_TEXT(0.06f));
    text_draw_fit(email, PANE_X + 26, top + 20, PANE_W - 52, 32, W_BOLD, C_WHITE);
    text_draw_fit(plan, PANE_X + 26, top + 20 + 47, PANE_W - 52, 24, W_REGULAR, C_TEXT(0.72f));
    top += 116 + 18;
  }
  char heading[128];
  utf8_upper(heading, sizeof heading, gr->name);
  text_draw(heading, PANE_X + 4, top + 4, 22, W_EXTRABOLD, C_TEXT(0.5f));
  top += 4 + 27 + 14;
  float at = item_at * (ITEM_H + 10);
  scroll = ui_approach(scroll, at > 560 ? at - 560 : 0, dt, 50);
  gfx_push_clip(PANE_X - 4, top - 4, PANE_W + 8, SC_DESIGN_H - top);
  for (int i = 0; i < gr->n; i++)
    draw_item(&gr->items[i], top + i * (ITEM_H + 10) - scroll, in_items && i == item_at && !nav_has_focus(),
              !strcmp(gr->items[i].k, "__logout"));
  gfx_pop_clip();
  nav_draw(NAV_SETTINGS, dt);
  draw_panel();
}

static void enter(void) {
  in_items = editing = 0;
  panel = P_NONE;
  api_get("/app/settings", model_came, NULL);
  api_get("/serverconfig", server_came, NULL);
}

const screen settings_screen = {enter, key, frame, NULL, 0};
