/* Settings > Organize catalogs: the Home rows in a list, as the web panel (setup.js rowsPanel).
   OK picks a row up to move it with up and down, right shows or hides it. Each change is kept
   at once (/app/rows); Home is built again when this closes. */
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "net.h"
#include "screens.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define MAX_ROWS 64
#define BOX_W 900.0f
#define ITEM_H 96.0f
#define LIST_TOP 260.0f
#define LIST_H 640.0f

typedef struct {
  char key[160], title[160];
  int hidden;
} row;

static row rows[MAX_ROWS];
static int count, at, moving, loaded;
static float scroll;

static void listed(const jval *answer, void *user) {
  (void)user;
  const jval *list = json_get(answer, "rows");
  count = 0;
  for (int i = 0; i < json_len(list) && count < MAX_ROWS; i++, count++) {
    const jval *r = json_at(list, i);
    snprintf(rows[count].key, sizeof rows[0].key, "%s", json_str(json_get(r, "key"), ""));
    snprintf(rows[count].title, sizeof rows[0].title, "%s", json_str(json_get(r, "title"), ""));
    rows[count].hidden = json_bool(json_get(r, "hidden"), 0);
  }
  loaded = 1;
}

static void save(void) {
  sbuf b = {0};
  sb_add(&b, "{\"rows\":[");
  for (int i = 0; i < count; i++) {
    sb_add(&b, i ? ",{\"key\":" : "{\"key\":");
    sb_add_json_str(&b, rows[i].key);
    sb_addf(&b, ",\"hidden\":%s}", rows[i].hidden ? "true" : "false");
  }
  sb_add(&b, "]}");
  net_request("/app/rows", b.s, NET_HIGH, NULL, NULL, NULL);
  sb_free(&b);
}

static void key(app_key k) {
  int next = at + (k == K_DOWN ? 1 : k == K_UP ? -1 : 0);
  if (next != at && next >= 0 && next < count) {
    if (moving) {
      row r = rows[at];
      rows[at] = rows[next];
      rows[next] = r;
      save();
    }
    at = next;
  } else if ((k == K_RIGHT || k == K_LEFT) && count) {
    rows[at].hidden = !rows[at].hidden;
    save();
  } else if (k == K_OK) moving = !moving;
  else if (k == K_BACK) {
    home_reload();
    app_back();
  }
}

static void frame(double t, float dt) {
  (void)t;
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(6, 6, 7, 230));
  float bx = (SC_DESIGN_W - BOX_W) / 2;
  gfx_rect(bx, 120, BOX_W, 860, 22, SC_RGBA(21, 21, 23, 255));
  const char *title = tx("organizeCatalogs");
  ui_line(title, (SC_DESIGN_W - text_width(title, 40, W_EXTRABOLD)) / 2, 160, 56, 40, W_EXTRABOLD, C_WHITE);
  if (loaded && !count) text_draw_wrap(tx("noRows"), bx + 46, LIST_TOP, BOX_W - 92, 26, W_REGULAR, C_TEXT(0.72f), 3, 36);
  float target = at * (ITEM_H + 12) + ITEM_H > LIST_H ? at * (ITEM_H + 12) + ITEM_H - LIST_H : 0;
  scroll = ui_approach(scroll, target, dt, 50);
  gfx_push_clip(bx, LIST_TOP - 6, BOX_W, LIST_H + 12);
  for (int i = 0; i < count; i++) {
    float y = LIST_TOP + i * (ITEM_H + 12) - scroll;
    if (y + ITEM_H < LIST_TOP - 6 || y > LIST_TOP + LIST_H) continue;
    int f = i == at;
    gfx_rect(bx + 40, y, BOX_W - 80, ITEM_H, 14, f ? C_WHITE : SC_RGBA(42, 42, 45, 255));
    hit_add(i, bx + 40, y, BOX_W - 80, ITEM_H);
    if (f && moving) gfx_outline(bx + 36, y - 4, BOX_W - 72, ITEM_H + 8, 17, 4, SC_RGBA(34, 211, 238, 255));
    uint32_t c = f ? C_BG : C_WHITE;
    gfx_push_opacity(rows[i].hidden ? 0.45f : 1);
    text_draw_fit(rows[i].title, bx + 66, y + 16, BOX_W - 132, 28, W_BOLD, c);
    gfx_pop_opacity();
    text_draw(tx(rows[i].hidden ? "rowHidden" : "rowShown"), bx + 66, y + 56, 21, W_REGULAR, f ? SC_RGBA(60, 60, 64, 255) : C_TEXT(0.6f));
  }
  gfx_pop_clip();
  const char *hint = tx("rowsHint");
  text_draw_fit(hint, bx + 46, 930, BOX_W - 92, 20, W_REGULAR, C_TEXT(0.55f));
}

static void enter(void) {
  at = moving = loaded = 0;
  scroll = 0;
  api_get("/app/rows", listed, NULL);
}

/* the pointer: a click on a row shows or hides it on Home (moving rows stays with the remote) */
static void point(float x, float y, int press) {
  int id = hit_at(x, y);
  if (!press || id < 0 || moving) return;
  at = id;
  key(K_RIGHT);
}

const screen rows_screen = {enter, key, frame, NULL, 1, point};
