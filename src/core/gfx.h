/* Drawing on the GPU in batches: rounded rectangles, two-colour gradients, pictures and text
   glyphs, all through one shader. Coordinates are design pixels of a 1920x1080 screen,
   scaled to whatever the TV's surface really is. */
#ifndef SC_GFX_H
#define SC_GFX_H

#include <stdint.h>

#define SC_DESIGN_W 1920
#define SC_DESIGN_H 1080

typedef struct {
  unsigned int id;
  int w, h;
  int alpha_only;
} sc_tex;

/* colours are 0xRRGGBBAA */
#define SC_RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

int gfx_init(void);
void gfx_begin(int surface_w, int surface_h, uint32_t clear);
void gfx_end(void);
/* the whole surface to one colour; a clear one lets the video plane show through */
void gfx_clear(uint32_t color);

void gfx_rect(float x, float y, float w, float h, float radius, uint32_t color);
/* colour a at the start, b at the end; vertical runs top to bottom */
void gfx_gradient(float x, float y, float w, float h, float radius, uint32_t a, uint32_t b, int vertical);
/* a ring of the given width just inside the rectangle (focus outlines) */
void gfx_outline(float x, float y, float w, float h, float radius, float width, uint32_t color);
/* how a picture fills its box: stretched, or cropped to keep its shape (centred, or keeping
   its top, as backdrops do) */
enum { STRETCH, COVER, COVER_TOP };
void gfx_image(const sc_tex *t, float x, float y, float w, float h, float radius, float opacity, int cover);
/* part of an alpha texture, tinted (glyphs) */
void gfx_alpha_quad(const sc_tex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    uint32_t color);

/* everything drawn next is multiplied by this opacity (fades) */
void gfx_push_opacity(float o);
void gfx_pop_opacity(void);
/* drawing outside this box is cut */
void gfx_push_clip(float x, float y, float w, float h);
void gfx_pop_clip(void);
/* a move applied to everything drawn next (scrolling rows) */
void gfx_push_offset(float dx, float dy);
void gfx_pop_offset(void);

sc_tex gfx_texture(int w, int h, const unsigned char *pixels, int alpha_only);
void gfx_texture_update(sc_tex *t, int x, int y, int w, int h, const unsigned char *pixels);
void gfx_texture_free(sc_tex *t);

/* the last frame, RGBA top to bottom, for screenshots */
int gfx_read(unsigned char *out, int w, int h);

/* frames drawn and draw calls in the last frame, for the diagnostics */
int gfx_draw_calls(void);

#endif
