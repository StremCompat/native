/* A keyboard drawn on screen and used with the remote's arrows, for every text the app asks
   (search, email, password, list addresses); a real keyboard types into it too. */
#ifndef SC_KEYBOARD_H
#define SC_KEYBOARD_H

#include "app.h"

#define KB_TEXT 256

typedef struct {
  char text[KB_TEXT];
  int secret;  /* shown as dots */
  int row, col;
  int shifted, symbols;
} keyboard;

enum { KB_NONE, KB_CHANGED, KB_DONE, KB_UP };

void kb_reset(keyboard *kb, const char *text, int secret);
/* the remote: KB_CHANGED when the text changed, KB_DONE on Done, KB_UP for Up from the top row */
int kb_key(keyboard *kb, app_key k);
/* letters from a real keyboard, and its Backspace */
void kb_type(keyboard *kb, const char *utf8);
void kb_erase(keyboard *kb);
/* the keys, from (x, y); returns the height used */
float kb_draw(const keyboard *kb, float x, float y);
/* the text as shown: dots for a secret one */
const char *kb_shown(const keyboard *kb);

#endif
