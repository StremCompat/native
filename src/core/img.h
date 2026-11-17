/* Pictures (posters, backgrounds, logos) fetched through the local server, decoded and made
   no wider than they are shown on a worker thread, and kept as textures within a memory
   budget; the least recently drawn go first. */
#ifndef SC_IMG_H
#define SC_IMG_H

#include <stddef.h>

#include "gfx.h"

/* the textures' memory budget: by the system's memory where it is known, else fallback_bytes */
void img_init(size_t fallback_bytes);
/* once per frame, before drawing: the clock for fades, and room made within the budget */
void img_frame(double t);

/* the picture at this web address, at most width pixels wide (0: as it comes), or NULL
   while it loads or when it failed. fade, when given, runs from 0 to 1 over the first
   moments after it arrived. */
const sc_tex *img_get(const char *url, int width, float *fade);

/* the picture at this address could not be had (to show the title in its place) */
int img_failed(const char *url, int width);

/* a picture from the app's own files as an alpha-only texture (icons), white where opaque */
sc_tex img_load_file(const char *path, int alpha_only);

/* screenshots: pictures show at once instead of fading in, so every run looks the same */
void img_skip_fades(int on);

/* textures held and their size, for the diagnostics */
size_t img_bytes(void);

#endif
