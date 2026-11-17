/* The screensaver: after some minutes without a key on the menus, the backdrops of what the viewer
   watches drift slowly behind their names and a clock, as the web interface does; any key brings
   the app back, and only does that. */
#ifndef SC_SAVER_H
#define SC_SAVER_H

/* asks the server for the minutes (Settings) once the app is ready */
void saver_init(void);
/* each frame with the time of the last key: may start it */
void saver_tick(double t, double last_key_at, int allowed);
int saver_active(void);
void saver_stop(void);
void saver_draw(double t);

#endif
