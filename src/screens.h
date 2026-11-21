/* The screens. Each one reads keys and draws itself; app.c shows one at a time. */
#ifndef SC_SCREENS_H
#define SC_SCREENS_H

#include "app.h"
#include "ui.h"

typedef struct {
  /* called each time the screen is shown */
  void (*enter)(void);
  void (*key)(app_key k);
  /* t: the clock in milliseconds; dt: since the last frame */
  void (*frame)(double t, float dt);
  /* letters from a real keyboard, for the screens that take text (NULL elsewhere) */
  void (*text)(const char *utf8);
  /* drawn over the screen below it (a panel or a menu), which shows underneath */
  int overlay;
  /* the pointer at (x, y) on the 1920x1080 design: press 0 when it moved there, 1 for a click */
  void (*point)(float x, float y, int press);
} screen;

extern const screen home_screen, title_screen, sources_screen, player_screen, login_screen, profiles_screen, search_screen,
    library_screen, discover_screen, settings_screen, addons_screen,
    card_menu_screen, similar_screen, calendar_screen, channels_screen, boot_screen, rows_screen, folder_screen;

/* the first screen, with nothing to go back to */
void app_show(const screen *s);
/* a screen over the current one, and back to the one before */
void app_push(const screen *s);
void app_back(void);

/* Home builds itself again the next time it is shown (after signing in or a new profile) */
void home_reload(void);
/* who is watching; at the start a single profile is used without asking */
void profiles_open(int starting);

/* the update to the newer version: downloaded by the server, installed by the system */
void update_check(void);
void update_start(void);
/* once per frame: how the download goes */
void update_poll(void);

/* screens opened on something */
void card_menu_open(const card *c, int continue_watching);
void similar_open(const char *type, const char *id);
void title_open(const char *type, const char *id);
/* the same, drawn at once with what the card knows (name, picture, synopsis) while the rest comes */
void title_open_card(const card *c);
void sources_open(const char *type, const char *id, const char *video, const char *name);
/* the same, playing the best source by itself once it can be told (Best on the title page) */
void sources_open_best(const char *type, const char *id, const char *video, const char *name);
/* the sources of the next episode: the one like the source just played starts by itself */
void sources_open_next(const char *type, const char *id, const char *video);
/* a source of the last search by its number, or with seq < 0 the one played last time */
void player_start(const char *type, const char *id, const char *vid, int seq);
/* a picture straight from an address (a live channel); live has no length */
void player_play_address(const char *url, const char *title, int live);
/* Channels, with what = 1 the add-a-list panel open, 2 the lists (Settings > Channels) */
void channels_open(int what);
/* a Collections folder by its id: tabs over a grid, or rows when it asks for them */
void folder_open(const char *id);
/* the title's trailer, inside the app, where a free source has one */
void player_play_trailer(const char *type, const char *id);
/* the app closes: a video playing is reported and let go */
void player_quit(void);

#endif
