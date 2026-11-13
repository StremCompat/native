/* The application: screens, focus and what each key does, independent of the platform. */
#ifndef SC_APP_H
#define SC_APP_H

#include <SDL.h>

/* where the app's own files are (fonts, pictures): "<app dir>/assets/<sub>" */
const char *app_asset_dir(const char *sub);

int app_init(int argc, char **argv);
void app_event(const SDL_Event *ev);
/* draws one frame on a surface of w x h at time t (milliseconds) */
void app_frame(int w, int h, double t);
int app_wants_quit(void);

/* Runs a test script offscreen: one command per line, "key down", "wait 300", "shot name".
   Time is virtual, so animations land at the same point on every run. */
int app_run_script(const char *script_path, const char *shot_path);

/* saves the current frame (main.c) */
int main_draw_and_save(const char *path, double t);

/* the keys the app understands, whatever the remote or keyboard sends */
typedef enum {
  K_NONE,
  K_UP,
  K_DOWN,
  K_LEFT,
  K_RIGHT,
  K_OK,
  K_BACK,
  K_PLAY,
  K_RED,
  K_GREEN,
  K_YELLOW,
  K_BLUE
} app_key;

#endif
