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

/* ---------- a PIN: four digits, from the remote's numbers or the keys on screen ---------- */
enum { PIN_OPEN, PIN_SET, PIN_CLEAR };
static int pin_mode, pin_purpose, pin_profile, pin_key;
static char pin[5], pin_title[160];
static void use_with(int i, const char *code);
static void pin_sent(net_reply *r, void *user);

static void ask_pin(int purpose, int i, const char *title) {
  pin_mode = 1;
  pin_purpose = purpose;
  pin_profile = i;
  pin_key = 0;
  pin[0] = 0;
  snprintf(pin_title, sizeof pin_title, "%s", title);
}

static void pin_type(char digit) {
  size_t n = strlen(pin);
  if (!digit) {
    if (n) pin[n - 1] = 0;
    return;
  }
  if (n < 4) pin[n] = digit, pin[n + 1] = 0;
  if (strlen(pin) < 4) return;
  pin_mode = 0;
  if (pin_purpose == PIN_OPEN) return use_with(pin_profile, pin);
  char body[96];
  snprintf(body, sizeof body, "{\"index\":%d,\"%s\":\"%s\"}", list[pin_profile].index, pin_purpose == PIN_SET ? "pin" : "current", pin);
  net_request("/app/profile/pin", body, NET_HIGH, NULL, pin_sent, NULL);
}

/* The pad as a phone's: 1 to 9 in three rows, then 0 in the middle and Delete at the right (cell 9,
   bottom left, is empty). The focus moves in four directions over it. */
#define PIN_KEY 84.0f
#define PIN_GAP 18.0f
#define PIN_EMPTY 9
#define PIN_ZERO 10
#define PIN_DELETE 11

static void pin_press(int cell) {
  if (cell == PIN_DELETE) pin_type(0);
  else if (cell == PIN_ZERO) pin_type('0');
  else if (cell < PIN_EMPTY) pin_type((char)('1' + cell));
}

static void pin_keys(app_key k) {
  int row = pin_key / 3, col = pin_key % 3;
  if (k == K_BACK) pin_mode = 0;
  else if (k == K_LEFT && col > 0) col--;
  else if (k == K_RIGHT && col < 2) col++;
  else if (k == K_UP && row > 0) row--;
  else if (k == K_DOWN && row < 3) row++;
  else if (k == K_OK) pin_press(pin_key);
  int next = row * 3 + col;
  /* the empty cell is stepped over: towards 0 from above or from the right, back up from the left */
  if (next == PIN_EMPTY) next = k == K_LEFT ? pin_key : PIN_ZERO;
  pin_key = next;
}

static void draw_pin(void) {
  float cy = SC_DESIGN_H * 0.12f;
  float tw = text_width(pin_title, 40, W_BOLD);
  ui_line(pin_title, (SC_DESIGN_W - tw) / 2, cy, 56, 40, W_BOLD, C_WHITE);
  for (int i = 0; i < 4; i++) {
    float x = SC_DESIGN_W / 2.0f - 2 * 46 + i * 46 + 10;
    if (i < (int)strlen(pin)) gfx_rect(x, cy + 92, 24, 24, 12, C_WHITE);
    else gfx_outline(x, cy + 92, 24, 24, 12, 3, C_TEXT(0.45f));
  }
  float left = (SC_DESIGN_W - (3 * PIN_KEY + 2 * PIN_GAP)) / 2, top = cy + 160;
  for (int i = 0; i < 12; i++) {
    if (i == PIN_EMPTY) continue;
    float x = left + (i % 3) * (PIN_KEY + PIN_GAP), y = top + (i / 3) * (PIN_KEY + PIN_GAP);
    int f = i == pin_key;
    gfx_rect(x, y, PIN_KEY, PIN_KEY, PIN_KEY / 2, f ? C_WHITE : C_TEXT(0.1f));
    hit_add(i, x, y, PIN_KEY, PIN_KEY);
    if (i == PIN_DELETE) {
      ui_icon(ICON_CLOSE, x + (PIN_KEY - 30) / 2, y + (PIN_KEY - 30) / 2, 30, f ? C_BG : C_WHITE);
      continue;
    }
    char label[2] = {(char)(i == PIN_ZERO ? '0' : '1' + i), 0};
    float lw = text_width(label, 34, W_SEMIBOLD);
    ui_line(label, x + (PIN_KEY - lw) / 2, y, PIN_KEY, 34, W_SEMIBOLD, f ? C_BG : C_WHITE);
  }
}

static void used(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  busy = 0;
  if (json_bool(json_get(answer, "ok"), 0)) {
    home_reload();
    app_show(&home_screen);
    return;
  }
  /* a wrong PIN asks again; the account may make it wait a little after several */
  if (!strcmp(json_str(json_get(answer, "reason"), ""), "wrong")) {
    char wait[16];
    int seconds = (int)json_num(json_get(answer, "retryAfter"), 0);
    snprintf(wait, sizeof wait, "%d", seconds);
    ui_toast(seconds ? txf("pinWait", wait, NULL) : tx("pinWrong"));
    pin_mode = 1; /* the same question again */
    pin[0] = 0;
  } else ui_toast(tx("profilesUnavailable"));
}

static void use_with(int i, const char *code) {
  if (busy || i >= count) return;
  busy = 1;
  char path[96];
  snprintf(path, sizeof path, "/app/profile?index=%d&pin=%s", list[i].index, code);
  api_get(path, used, (void *)(intptr_t)gen);
}

/* a protected profile asks its PIN first */
static void use(int i) {
  if (i >= count) return;
  if (!list[i].locked) return use_with(i, "");
  char number[12];
  snprintf(number, sizeof number, "%d", list[i].index);
  ask_pin(PIN_OPEN, i, txf("pinEnter", list[i].name[0] ? list[i].name : txf("profileNumber", number, NULL), NULL));
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
  /* nothing to ask at the start when there is one profile without a PIN */
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
enum { E_NAME, E_COLOR, E_AVATAR, E_PIN, E_SAVE, E_COUNT };
static int editing, edit_index, edit_locked, edit_at, edit_row, color_at, avatar_at, navatars, typing, on_edit_button;
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
  edit_locked = i < count && list[i].locked;
  edit_at = i;
  kb_reset(&name_kb, i < count ? list[i].name : "", 0);
  color_at = 0;
  for (int c = 0; i < count && c < 8; c++)
    if (colors[c] == list[i].color) color_at = c;
  avatar_at = -1;
  api_get("/app/avatars", avatars_came, NULL);
}

/* the PIN changed (or not): the profiles again, with the editor closed */
static void pin_sent(net_reply *r, void *user) {
  (void)user;
  jdoc *d = r->status == 200 && r->body ? json_parse(r->body, r->len) : NULL;
  int ok = json_bool(json_get(json_root(d), "ok"), 0);
  json_free(d);
  ui_toast(tx(ok ? (pin_purpose == PIN_SET ? "pinSaved" : "pinRemoved") : "pinNotSaved"));
  if (!ok) return;
  editing = 0;
  gen++;
  api_get("/app/profiles", listed, (void *)(intptr_t)gen);
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
  /* a new profile gets its PIN once it exists */
  else if (k == K_UP && edit_row > 0) edit_row -= edit_row == E_SAVE && !edit_index ? 2 : 1;
  else if (k == K_DOWN && edit_row < E_COUNT - 1) edit_row += edit_row == E_AVATAR && !edit_index ? 2 : 1;
  else if (edit_row == E_COLOR && k == K_LEFT && color_at > 0) color_at--;
  else if (edit_row == E_COLOR && k == K_RIGHT && color_at < 7) color_at++;
  else if (edit_row == E_AVATAR && k == K_LEFT && avatar_at > -1) avatar_at--;
  else if (edit_row == E_AVATAR && k == K_RIGHT && avatar_at < navatars - 1) avatar_at++;
  else if (k == K_OK && edit_row == E_NAME) typing = 1;
  else if (k == K_OK && edit_row == E_SAVE) save();
  else if (k == K_OK && edit_row == E_PIN) ask_pin(edit_locked ? PIN_CLEAR : PIN_SET, edit_at, tx(edit_locked ? "pinCurrent" : "pinNew"));
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
  y += 136;
  if (edit_index) {
    const char *pl = tx(edit_locked ? "pinRemove" : "pinSet");
    float pw = text_width(pl, 22, W_BOLD) + 60;
    int pf = edit_row == E_PIN;
    gfx_rect(cx - pw / 2, y, pw, 52, 26, pf ? C_WHITE : 0);
    if (!pf) gfx_outline(cx - pw / 2, y, pw, 52, 26, 2, C_TEXT(0.25f));
    ui_line(pl, cx - pw / 2 + 30, y, 52, 22, W_BOLD, pf ? C_BG : C_TEXT(0.75f));
  }
  y += 82;
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
  if (pin_mode) return pin_keys(k);
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
  if (pin_mode && utf8[0] >= '0' && utf8[0] <= '9' && !utf8[1]) return pin_type(utf8[0]);
  if (typing) kb_type(&name_kb, utf8);
}

static void frame(double t, float dt) {
  (void)t;
  (void)dt;
  gfx_gradient(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(29, 29, 32, 255), C_BG, 1);
  /* the PIN pad stands alone, with its own title */
  if (loaded && pin_mode) return draw_pin();
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

static void enter(void) { editing = on_edit_button = pin_mode = 0; }

static void point(float x, float y, int press) {
  if (!loaded || busy) return;
  /* the PIN pad comes first, as for the keys: the editor opens it too */
  if (pin_mode) {
    int cell = hit_at(x, y);
    if (cell < 0) return;
    pin_key = cell;
    if (press) pin_press(cell);
    return;
  }
  if (editing) return;
  int places = count + (count < MAX_PROFILES);
  float total = places * CARD + (places - 1) * FACE_GAP;
  float dx = x - (SC_DESIGN_W - total) / 2;
  float top = SC_DESIGN_H * 0.15f + 147;
  if (dx < 0 || y < top || y >= top + FACE + 75) return;
  int i = (int)(dx / (CARD + FACE_GAP));
  if (i >= places || dx - i * (CARD + FACE_GAP) >= CARD) return;
  at = i;
  on_edit_button = 0;
  if (press) key(K_OK);
}

const screen profiles_screen = {enter, key, frame, text, 0, point};
