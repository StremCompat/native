/* Entry point: the window, the GL context and the frame loop. With --shot FILE it draws the
   screens offscreen at 1920x1080 and saves a picture, for the automated tests. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "gfx.h"
#include "gl.h"
#include "img.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "text.h"
#include "util.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"

/* pictures kept on the GPU; the TV builds set their own */
#ifndef IMG_BUDGET
#define IMG_BUDGET (96u << 20)
#endif

/* the local server's port: the TV builds set theirs, the computer uses the dev server's */
#ifndef SC_PORT
#define SC_PORT 8082
#endif

static void *gl_lookup(const char *name) { return SDL_GL_GetProcAddress(name); }

typedef struct {
  unsigned int fbo, tex;
} offscreen;

static int make_offscreen(offscreen *o, int w, int h) {
  glGenFramebuffers(1, &o->fbo);
  glGenTextures(1, &o->tex);
  glBindTexture(GL_TEXTURE_2D, o->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, o->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, o->tex, 0);
  return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? 0 : -1;
}

static int save_shot(const char *path, int w, int h) {
  unsigned char *px = malloc((size_t)w * h * 4);
  gfx_read(px, w, h);
  int ok = stbi_write_png(path, w, h, 4, px, w * 4);
  free(px);
  return ok ? 0 : -1;
}

static SDL_Window *win;
static int running = 1;

/* one frame: the events, the network's answers, the screens */
static void step(void) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) running = 0;
    else app_event(&ev);
  }
  int w, h;
  SDL_GL_GetDrawableSize(win, &w, &h);
  text_set_scale((float)h / SC_DESIGN_H);
  double t = now_ms();
  net_poll();
  img_frame(t);
  app_frame(w, h, t);
  SDL_GL_SwapWindow(win);
  if (app_wants_quit()) running = 0;
}

/* the start failed: said in the log and to the person, then the app closes */
static int fail(const char *what) {
  char message[300];
  snprintf(message, sizeof message, "StremCompat: %s (%s)", what, SDL_GetError());
  fprintf(stderr, "%s\n", message);
  platform_fatal(message);
  return 1;
}

int main(int argc, char **argv) {
  const char *shot = NULL, *script = NULL;
  int win_w = 1280, win_h = 720, port = SC_PORT;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) script = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &win_w, &win_h);
    else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
  }
  platform_init();
  app_stage = "SDL";
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return fail("SDL");
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  /* the video plays on the TV's plane under the window (webOS, Samsung, Android): the window needs
     see-through pixels, or the player's clear paints black over the picture. 8 bits a colour,
     nothing the 2D drawing does not use. */
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
  Uint32 flags = SDL_WINDOW_OPENGL | (shot ? SDL_WINDOW_HIDDEN : platform_window_flags());
  /* a whole-screen window takes the screen's own size (720p or 1080p panels alike) */
  SDL_DisplayMode screen;
  if ((flags & SDL_WINDOW_FULLSCREEN) && SDL_GetDesktopDisplayMode(0, &screen) == 0) win_w = screen.w, win_h = screen.h;
  win = SDL_CreateWindow("StremCompat", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, flags);
  if (!win) return fail("window");
  app_stage = "GL";
  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) return fail("GL context");
  SDL_GL_SetSwapInterval(1);
  int missing = sc_gl_load(gl_lookup);
  if (missing) return fail("GL entry points missing");
  app_stage = "drawing";
  if (app_init(argc, argv) || gfx_init()) return fail("drawing");
  app_stage = "fonts";
  if (text_init(app_asset_dir("fonts"))) return fail("fonts");
  platform_add_fonts();
  net_init(port, 0);
  img_init(IMG_BUDGET);
  app_stage = "first screen";
  if (app_start()) return fail("first screen");
  app_stage = "running";

  if (shot) {
    /* offscreen at the design size: the same picture whatever the window */
    offscreen o;
    if (make_offscreen(&o, SC_DESIGN_W, SC_DESIGN_H)) {
      fprintf(stderr, "offscreen buffer refused\n");
      return 1;
    }
    text_set_scale(1.0f);
    img_skip_fades(1);
    int rc = app_run_script(script, shot);
    net_shutdown();
    SDL_Quit();
    return rc;
  }

#ifdef __EMSCRIPTEN__
  /* the browser calls each frame; this never returns */
  emscripten_set_main_loop(step, 0, 1);
#endif
  while (running) step();
  /* a video playing: its last report and its release go out before the network stops */
  player_quit();
  SDL_Delay(200);
  net_shutdown();
  SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

/* used by the script runner: draw one frame offscreen and keep it */
int main_draw_and_save(const char *path, double t) {
  img_frame(t);
  app_frame(SC_DESIGN_W, SC_DESIGN_H, t);
  glFinish();
  return save_shot(path, SC_DESIGN_W, SC_DESIGN_H);
}
