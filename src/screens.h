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

extern const screen home_screen;

void app_show(const screen *s);

#endif
