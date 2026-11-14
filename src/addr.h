/* An address typed in the middle of the screen (an M3U list's, an addon's) or a text to search, as
   the web panel (text-panel.js): the app's keyboard, a real keyboard, and for addresses a QR code
   that opens a page on the TV where the phone pastes it (server/phone-link.js). An M3U panel also
   offers suggested lists of free channels. The screen that opens it hands it keys and text and
   draws it last. */
#ifndef SC_ADDR_H
#define SC_ADDR_H

#include "app.h"

typedef void (*addr_done_fn)(const char *value);

enum { ADDR_TEXT, ADDR_URL, ADDR_M3U };

void addr_open(const char *title, int kind, addr_done_fn done);
int addr_active(void);
void addr_close(void);
void addr_key(app_key k);
void addr_text(const char *utf8);
void addr_draw(void);

#endif
