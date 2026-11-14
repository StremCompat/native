/* What every screen draws the same way: the colours, icons, the focus ring, cards, and the
   easing that moves things toward where they should be. Sizes are the web interface's. */
#ifndef SC_UI_H
#define SC_UI_H

#include <stdint.h>

#include "gfx.h"
#include "icons.h"
#include "json.h"

#define C_BG SC_RGBA(10, 10, 11, 255)
#define C_WHITE SC_RGBA(255, 255, 255, 255)
#define C_CARD SC_RGBA(26, 26, 28, 255)
/* white at a strength from 0 to 1, as the web interface writes rgba(255,255,255,a) */
#define C_TEXT(a) SC_RGBA(255, 255, 255, (int)((a) * 255.0f + 0.5f))

int ui_init(void);

void ui_icon(icon_id id, float x, float y, float size, uint32_t color);

/* v moved toward target for dt milliseconds: most of the way within tau, then gently */
float ui_approach(float v, float target, float dt, float tau);

/* the white ring around what has the focus, with the dark gap of the web interface */
void ui_focus_ring(float x, float y, float w, float h, float radius);

/* a shade of the background colour over the whole screen, as a CSS linear-gradient: stops of
   (position 0..1, strength 0..1), top to bottom or left to right; past the ends the nearest
   stop holds */
typedef struct {
  float at, alpha;
} shade_stop;
void ui_shade(const shade_stop *stops, int n, int vertical);

/* one line of text centred in a CSS line box of height box; returns its width */
float ui_line(const char *s, float x, float y, float box, float size, int weight, uint32_t color);
/* "7.4 [IMDb]" in a line box; returns its width */
float ui_rating(const char *rating, float x, float y, float box, float size, float badge_size, uint32_t color);

/* the picture behind a screen, fading from the last one to the next */
typedef struct {
  char *now, *before;
  float mix;
} backdrop;
void backdrop_set(backdrop *b, const char *url);
/* from x to the right edge, h tall, keeping the picture's top */
void backdrop_draw(backdrop *b, float x, float h, float opacity, float dt);

/* A list to choose from, as the web player's menus (#menu): a heading, then rows; the one
   in use is marked. Used by the player, the sources' language filter and the card menus. */
#define MENU_MAX 80
typedef struct {
  char title[96];
  struct {
    char label[160];
    int on, id;
  } items[MENU_MAX];
  int n, at, open;
} menu;
void menu_begin(menu *m, const char *title);
void menu_add(menu *m, const char *label, int on, int id);
/* Up and Down move; OK returns the chosen row's id; Back closes (-2); otherwise -1 */
int menu_key(menu *m, int key);
/* x is its left edge, bottom where it ends */
void menu_draw(const menu *m, float x, float bottom);
float menu_height(const menu *m);

/* where a row of cards stands so the focused one shows: it moves only once the focus nears
   the right edge, keeping one card after it in sight (web: slideRow) */
float ui_row_offset(int focused, float step, float width);

/* a QR code in a white square of side box: modules holds size x size of '1' (dark) and '0' */
void ui_qr(const char *modules, int size, float x, float y, float box);

/* a short message at the top of the screen for a few seconds (any screen) */
void ui_toast(const char *text);
void ui_draw_toast(double t);

/* a title as a row shows it (the server decides its pictures and its lines) */
typedef struct {
  char *id, *type, *name, *art, *backdrop, *poster, *subtitle, *video_id, *info, *description;
  char *shape, *glyph; /* a Collections folder: poster, square or wide, and its emoji or letters */
  float progress;
  int watched, up_next, in_library, fresh, cw;
} card;

void card_read(card *c, const jval *v);
void card_clear(card *c);

/* poster cards (232 x 347) or wide ones (400 x 225) with the title and progress on them */
#define CARD_W 232.0f
#define CARD_H 347.0f
#define WIDE_W 400.0f
#define WIDE_H 225.0f
#define CARD_GAP 20.0f
void ui_card(const card *c, float x, float y, int wide, int focused);
/* a poster card of any size with its name over the bottom (grids) */
void ui_tile(const card *c, float x, float y, float w, float h, int focused);
/* a Collections folder tile as Nuvio draws it: its shape, its cover (else its emoji or first
   letters) and its name under the picture; the width its shape takes */
void ui_folder_tile(const card *c, float x, float y, int focused);
float ui_folder_width(const card *c);
/* the name over the bottom of a poster card (.titlelabel), for rows that name their titles */
void ui_card_name(const card *c, float x, float y, int focused);

#endif
