#include "keyboard.h"

#include <stdio.h>
#include <string.h>

#include "api.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define KEY 64.0f
#define GAP 8.0f
#define ROWS 4

/* the last row's keys, each as wide as several letters */
enum { ACT_SHIFT, ACT_SYMBOLS, ACT_SPACE, ACT_ERASE, ACT_DONE, ACT_COUNT };
static const int act_width[ACT_COUNT] = {2, 2, 4, 2, 3};

static const char *letters[ROWS - 1] = {"abcdefghijklm", "nopqrstuvwxyz", "1234567890@._"};
static const char *symbols[ROWS - 1] = {"!#$%&*+-/=?^~", "()[]{}<>'\",;:", "1234567890@._"};

static const char *row_keys(const keyboard *kb, int row) { return (kb->symbols ? symbols : letters)[row]; }

static int row_length(const keyboard *kb, int row) {
  return row < ROWS - 1 ? (int)strlen(row_keys(kb, row)) : ACT_COUNT;
}

void kb_reset(keyboard *kb, const char *text, int secret) {
  memset(kb, 0, sizeof *kb);
  snprintf(kb->text, sizeof kb->text, "%s", text ? text : "");
  kb->secret = secret;
}

void kb_type(keyboard *kb, const char *utf8) {
  size_t n = strlen(kb->text), add = strlen(utf8);
  if (n + add < sizeof kb->text) memcpy(kb->text + n, utf8, add + 1);
}

void kb_erase(keyboard *kb) {
  size_t n = strlen(kb->text);
  /* a whole character, not one byte of it */
  while (n > 0 && (kb->text[n - 1] & 0xC0) == 0x80) n--;
  if (n > 0) n--;
  kb->text[n] = 0;
}

static int press(keyboard *kb) {
  if (kb->row < ROWS - 1) {
    char c[2] = {row_keys(kb, kb->row)[kb->col], 0};
    if (kb->shifted && c[0] >= 'a' && c[0] <= 'z') c[0] = (char)(c[0] - 32), kb->shifted = 0;
    kb_type(kb, c);
    return KB_CHANGED;
  }
  switch (kb->col) {
  case ACT_SHIFT: kb->shifted = !kb->shifted; return KB_NONE;
  case ACT_SYMBOLS: kb->symbols = !kb->symbols; return KB_NONE;
  case ACT_SPACE: kb_type(kb, " "); return KB_CHANGED;
  case ACT_ERASE: kb_erase(kb); return KB_CHANGED;
  default: return KB_DONE;
  }
}

/* moving between rows of different lengths keeps the place along the row */
static int column_in(const keyboard *kb, int from_row, int to_row, int col) {
  float x = 0;
  if (from_row == ROWS - 1)
    for (int i = 0; i < col; i++) x += act_width[i];
  else
    x = (float)col;
  if (to_row < ROWS - 1) {
    int n = row_length(kb, to_row);
    return x < n ? (int)x : n - 1;
  }
  float at = 0;
  for (int i = 0; i < ACT_COUNT; i++) {
    at += act_width[i];
    if (x < at) return i;
  }
  return ACT_COUNT - 1;
}

int kb_key(keyboard *kb, app_key k) {
  if (k == K_LEFT && kb->col > 0) kb->col--;
  else if (k == K_RIGHT && kb->col < row_length(kb, kb->row) - 1) kb->col++;
  else if (k == K_UP && kb->row == 0) return KB_UP;
  else if (k == K_UP) kb->col = column_in(kb, kb->row, kb->row - 1, kb->col), kb->row--;
  else if (k == K_DOWN && kb->row < ROWS - 1) kb->col = column_in(kb, kb->row, kb->row + 1, kb->col), kb->row++;
  else if (k == K_OK) return press(kb);
  return KB_NONE;
}

const char *kb_shown(const keyboard *kb) {
  static char dots[KB_TEXT * 3];
  if (!kb->secret) return kb->text;
  size_t n = 0;
  for (const char *p = kb->text; *p && n + 3 < sizeof dots;)
    if ((*p++ & 0xC0) != 0x80) memcpy(dots + n, "\xE2\x80\xA2", 3), n += 3;
  dots[n] = 0;
  return dots;
}

static void draw_key(const char *label, float x, float y, float w, int focused, int on) {
  gfx_rect(x, y, w, KEY, 12, focused ? C_WHITE : on ? C_TEXT(0.22f) : C_TEXT(0.1f));
  float tw = text_width(label, 28, W_SEMIBOLD);
  ui_line(label, x + (w - tw) / 2, y, KEY, 28, W_SEMIBOLD, focused ? C_BG : C_WHITE);
}

float kb_draw(const keyboard *kb, float x, float y) {
  for (int r = 0; r < ROWS - 1; r++) {
    const char *keys = row_keys(kb, r);
    for (int c = 0; keys[c]; c++) {
      char label[2] = {keys[c], 0};
      if (kb->shifted && label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 32);
      draw_key(label, x + c * (KEY + GAP), y + r * (KEY + GAP), KEY, kb->row == r && kb->col == c, 0);
    }
  }
  const char *labels[ACT_COUNT] = {"aA", kb->symbols ? "abc" : "#+=", tx("keyboardSpace"), "\xE2\x86\x90", tx("keyboardDone")};
  float ax = x, ay = y + (ROWS - 1) * (KEY + GAP);
  for (int i = 0; i < ACT_COUNT; i++) {
    float w = act_width[i] * KEY + (act_width[i] - 1) * GAP;
    draw_key(labels[i], ax, ay, w, kb->row == ROWS - 1 && kb->col == i, i == ACT_SHIFT && kb->shifted);
    ax += w + GAP;
  }
  return ROWS * (KEY + GAP) - GAP;
}
