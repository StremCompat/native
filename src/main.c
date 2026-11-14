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
#include "text.h"
#include "util.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"

/* pictures kept on the GPU; the TV builds set their own */
#ifndef IMG_BUDGET
#define IMG_BUDGET (96u << 20)
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

int main(int argc, char **argv) {
  const char *shot = NULL, *script = NULL;
  int win_w = 1280, win_h = 720, port = 8082;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) script = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &win_w, &win_h);
    else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
  }
  platform_init();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  Uint32 flags = SDL_WINDOW_OPENGL | (shot ? SDL_WINDOW_HIDDEN : platform_window_flags());
  /* a whole-screen window takes the screen's own size (720p or 1080p panels alike) */
  SDL_DisplayMode screen;
  if ((flags & SDL_WINDOW_FULLSCREEN) && SDL_GetDesktopDisplayMode(0, &screen) == 0) win_w = screen.w, win_h = screen.h;
  SDL_Window *win = SDL_CreateWindow("StremCompat", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, flags);
  if (!win) {
    fprintf(stderr, "window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) {
    fprintf(stderr, "GL context: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetSwapInterval(1);
  int missing = sc_gl_load(gl_lookup);
  if (missing) {
    fprintf(stderr, "GL: %d entry points missing\n", missing);
    return 1;
  }
  if (app_init(argc, argv) || gfx_init() || text_init(app_asset_dir("fonts"))) return 1;
  platform_add_fonts();
  net_init(port, 3);
  img_init(IMG_BUDGET);
  if (app_start()) return 1;

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

  int running = 1;
  while (running) {
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
