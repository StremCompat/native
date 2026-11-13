/* Pictures (posters, backgrounds, logos) fetched from the local server, decoded on a worker
   thread and kept as textures within a memory budget; the least recently drawn go first. */
#ifndef SC_IMG_H
#define SC_IMG_H

#include <stddef.h>

#include "gfx.h"

void img_init(size_t budget_bytes);
/* once per frame, before drawing: the clock for fades, and room made within the budget */
void img_frame(double t);

/* the picture at this server path, or NULL while it loads or when it failed. fade, when
   given, runs from 0 to 1 over the first moments after it arrived. */
const sc_tex *img_get(const char *path, float *fade);

/* screenshots: pictures show at once instead of fading in, so every run looks the same */
void img_skip_fades(int on);

/* textures held and their size, for the diagnostics */
size_t img_bytes(void);

#endif
