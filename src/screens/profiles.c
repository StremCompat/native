/* Who is watching: the Nuvio profiles as round faces, the one in use first in focus. A profile
   alone is used without asking. Measures are the web interface's (profiles.css). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "img.h"
#include "keyboard.h"
#include "net.h"
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

/* ---------- the editor: a new profile ("+") or the focused one, its name, colour, avatar ---------- */
#define MAX_AVATARS 48
static const uint32_t colors[] = {0x1E88E5FF, 0xE53935FF, 0x43A047FF, 0xFB8C00FF, 0x8E24AAFF, 0x00ACC1FF, 0xF4511EFF, 0x6D4C41FF};
enum { E_NAME, E_COLOR, E_AVATAR, E_SAVE, E_COUNT };
static int editing, edit_index, edit_row, color_at, avatar_at, navatars, typing, on_edit_button;
static char avatar_ids[MAX_AVATARS][64], avatar_urls[MAX_AVATARS][512];
static keyboard name_kb;

static void avatars_came(const jval *answer, void *user) {
  (void)user;
  const jval *list = json_get(answer, "avatars");
  navatars = 0;
  for (int i = 0; i < json_len(list) && navatars < MAX_AVATARS; i++, navatars++) {
    snprintf(avatar_ids[navatars], sizeof avatar_ids[0], "%s", json_str(json_get(json_at(list, i), "id"), ""));
    snprintf(avatar_urls[navatars], sizeof avatar_urls[0], "%s", json_str(json_get(json_at(list, i), "url"), ""));
  }
}

static void open_editor(int i) {
  editing = 1;
  edit_row = E_NAME;
  typing = 0;
  edit_index = i < count ? list[i].index : 0;
  kb_reset(&name_kb, i < count ? list[i].name : "", 0);
  color_at = 0;
  for (int c = 0; i < count && c < 8; c++)
    if (colors[c] == list[i].color) color_at = c;
  avatar_at = -1;
  api_get("/app/avatars", avatars_came, NULL);
}

static void saved(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  if (!json_bool(json_get(json_root(d), "ok"), 0)) ui_toast(tx("profileNotSaved"));
  json_free(d);
  editing = 0;
  gen++;
  api_get("/app/profiles", listed, (void *)(intptr_t)gen);
}

static void save(void) {
  if (!name_kb.text[0]) return;
  sbuf b = {0};
  sb_add(&b, "{\"name\":");
  sb_add_json_str(&b, name_kb.text);
  uint32_t c = colors[color_at];
  sb_addf(&b, ",\"color\":\"#%02X%02X%02X\"", (c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255);
  if (avatar_at >= 0) {
    sb_add(&b, ",\"avatar\":");
    sb_add_json_str(&b, avatar_ids[avatar_at]);
  }
  if (edit_index) sb_addf(&b, ",\"index\":%d", edit_index);
  sb_add(&b, "}");
  net_request("/app/profile/save", b.s, NET_HIGH, NULL, saved, NULL);
  sb_free(&b);
}

static void editor_key(app_key k) {
  if (typing) {
    if (k == K_BACK) typing = 0;
    else if (kb_key(&name_kb, k) == KB_DONE) typing = 0, edit_row = E_COLOR;
    return;
  }
  if (k == K_BACK) editing = 0;
  else if (k == K_UP && edit_row > 0) edit_row--;
  else if (k == K_DOWN && edit_row < E_COUNT - 1) edit_row++;
  else if (edit_row == E_COLOR && k == K_LEFT && color_at > 0) color_at--;
  else if (edit_row == E_COLOR && k == K_RIGHT && color_at < 7) color_at++;
  else if (edit_row == E_AVATAR && k == K_LEFT && avatar_at > -1) avatar_at--;
  else if (edit_row == E_AVATAR && k == K_RIGHT && avatar_at < navatars - 1) avatar_at++;
  else if (k == K_OK && edit_row == E_NAME) typing = 1;
  else if (k == K_OK && edit_row == E_SAVE) save();
}

static void draw_editor(void) {
  float cx = SC_DESIGN_W / 2, y = SC_DESIGN_H * 0.12f;
  const sc_tex *pic = avatar_at >= 0 ? img_get(avatar_urls[avatar_at], 256, NULL) : NULL;
  gfx_rect(cx - 90, y, 180, 180, 90, colors[color_at]);
  if (pic) gfx_image(pic, cx - 90, y, 180, 180, 90, 1, COVER);
  y += 214;
  int f = edit_row == E_NAME;
  gfx_rect(cx - 280, y, 560, 72, 14, f ? C_TEXT(0.2f) : C_TEXT(0.1f));
  if (f) gfx_outline(cx - 283, y - 3, 566, 78, 17, 3, C_WHITE);
  const char *shown = name_kb.text[0] ? name_kb.text : tx("profileName");
  float nw = text_width(shown, 32, W_REGULAR);
  ui_line(shown, cx - (nw < 520 ? nw / 2 : 260), y, 72, 32, W_REGULAR, name_kb.text[0] ? C_WHITE : C_TEXT(0.45f));
  y += 102;
  for (int c = 0; c < 8; c++) {
    float x = cx - 4 * 76 + c * 76 + 10;
    gfx_rect(x, y, 56, 56, 28, colors[c]);
    if (c == color_at) gfx_outline(x - 6, y - 6, 68, 68, 34, edit_row == E_COLOR ? 5 : 3, edit_row == E_COLOR ? C_WHITE : C_TEXT(0.5f));
  }
  y += 96;
  /* the avatars in a row that follows the chosen one; the first place is none (the letter) */
  float step = 120, start = cx - step / 2 - (avatar_at + 1) * step;
  gfx_push_clip(cx - 500, y - 10, 1000, 130);
  for (int a = -1; a < navatars; a++) {
    float x = start + (a + 1) * step;
    gfx_rect(x, y, 100, 100, 50, colors[color_at]);
    const sc_tex *av = a >= 0 ? img_get(avatar_urls[a], 128, NULL) : NULL;
    if (av) gfx_image(av, x, y, 100, 100, 50, 1, COVER);
    if (a == avatar_at) gfx_outline(x - 6, y - 6, 112, 112, 56, edit_row == E_AVATAR ? 5 : 3, edit_row == E_AVATAR ? C_WHITE : C_TEXT(0.5f));
  }
  gfx_pop_clip();
  y += 150;
  const char *label = tx("save");
  float bw = text_width(label, 28, W_BOLD) + 80;
  f = edit_row == E_SAVE;
  gfx_rect(cx - bw / 2, y, bw, 70, 35, f ? C_WHITE : C_TEXT(0.14f));
  ui_line(label, cx - bw / 2 + 40, y, 70, 28, W_BOLD, f ? C_BG : C_WHITE);
  if (typing) {
    float kw = 13 * 72 - 8, kx = (SC_DESIGN_W - kw) / 2, ky = SC_DESIGN_H - 340;
    gfx_rect(kx - 30, ky - 30, kw + 60, 340, 18, SC_RGBA(20, 20, 22, 250));
    kb_draw(&name_kb, kx, ky);
  }
}

static void key(app_key k) {
  if (editing) return editor_key(k);
  if (on_edit_button) {
    if (k == K_UP) on_edit_button = 0;
    else if (k == K_OK && at < count) open_editor(at);
    return;
  }
  /* the last place is "Add profile" while there is room for one */
  int places = count + (count < MAX_PROFILES);
  if (k == K_LEFT && at > 0) at--;
  else if (k == K_RIGHT && at < places - 1) at++;
  else if (k == K_DOWN && at < count) on_edit_button = 1;
  else if (k == K_OK && at == count) open_editor(count);
  else if (k == K_OK) use(at);
  else if (k == K_BACK && !at_start) app_back();
}

static void text(const char *utf8) {
  if (typing) kb_type(&name_kb, utf8);
}

static void frame(double t, float dt) {
  (void)t;
  (void)dt;
  gfx_gradient(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(29, 29, 32, 255), C_BG, 1);
  const char *title = tx("whoIsWatching");
  text_draw(title, (SC_DESIGN_W - text_width(title, 64, W_EXTRABOLD)) / 2, SC_DESIGN_H * 0.15f, 64, W_EXTRABOLD, C_WHITE);
  if (!loaded) return;
  if (editing) return draw_editor();
  int places = count + (count < MAX_PROFILES);
  float total = places * CARD + (places - 1) * FACE_GAP, x = (SC_DESIGN_W - total) / 2, y = SC_DESIGN_H * 0.15f + 77 + 70;
  for (int i = 0; i < count; i++, x += CARD + FACE_GAP) {
    const profile *p = &list[i];
    int focused = i == at && !on_edit_button;
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
  if (count < MAX_PROFILES) {
    /* Add profile: a ring with a plus */
    int focused = at == count && !on_edit_button;
    float fx = x + (CARD - FACE) / 2;
    gfx_outline(fx, y, FACE, FACE, FACE / 2, 4, C_TEXT(0.35f));
    if (focused) gfx_outline(fx - 7, y - 7, FACE + 14, FACE + 14, FACE / 2 + 7, 7, C_WHITE);
    float pw = text_width("+", 92, W_REGULAR);
    ui_line("+", fx + (FACE - pw) / 2, y, FACE, 92, W_REGULAR, C_TEXT(0.75f));
    const char *add = tx("addProfile");
    float aw = text_width(add, 32, W_BOLD);
    text_draw_fit(add, x + (aw < CARD ? (CARD - aw) / 2 : 0), y + FACE + 24, CARD, 32, W_BOLD, focused ? C_WHITE : C_TEXT(0.72f));
  }
  /* Edit profile, under the faces */
  const char *editl = tx("editProfile");
  float ew = text_width(editl, 28, W_BOLD) + 80, ey = y + FACE + 24 + 100;
  gfx_rect((SC_DESIGN_W - ew) / 2, ey, ew, 66, 33, on_edit_button ? C_WHITE : 0);
  if (!on_edit_button) gfx_outline((SC_DESIGN_W - ew) / 2, ey, ew, 66, 33, 3, C_TEXT(0.35f));
  ui_line(editl, (SC_DESIGN_W - ew) / 2 + 40, ey, 66, 28, W_BOLD, on_edit_button ? C_BG : C_TEXT(0.85f));
  const char *hint = tx("profilesHint");
  text_draw(hint, (SC_DESIGN_W - text_width(hint, 22, W_REGULAR)) / 2, SC_DESIGN_H - 48 - 27, 22, W_REGULAR, C_TEXT(0.55f));
}

static void enter(void) { editing = on_edit_button = 0; }

const screen profiles_screen = {enter, key, frame, text, 0};
