/* The screens. Each one reads keys and draws itself; app.c shows one at a time. */
#ifndef SC_SCREENS_H
#define SC_SCREENS_H

#include "app.h"

typedef struct {
  /* called each time the screen is shown */
  void (*enter)(void);
  void (*key)(app_key k);
  /* t: the clock in milliseconds; dt: since the last frame */
  void (*frame)(double t, float dt);
} screen;

extern const screen home_screen, title_screen, sources_screen;

/* the first screen, with nothing to go back to */
void app_show(const screen *s);
/* a screen over the current one, and back to the one before */
void app_push(const screen *s);
void app_back(void);

/* screens opened on something */
void title_open(const char *type, const char *id);
void sources_open(const char *type, const char *id, const char *video, const char *name);

#endif
