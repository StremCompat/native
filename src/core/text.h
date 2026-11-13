/* Text: Inter in five weights plus fallback fonts for scripts it lacks (Korean, Japanese,
   Chinese), drawn from one glyph atlas on the GPU. Sizes are design pixels. */
#ifndef SC_TEXT_H
#define SC_TEXT_H

#include <stdint.h>

enum { W_REGULAR, W_MEDIUM, W_SEMIBOLD, W_BOLD, W_EXTRABOLD, W_COUNT };

int text_init(const char *font_dir);
/* a font tried, in order, for characters the weights above do not have */
int text_add_fallback(const char *path);
/* the surface is this many times the design size: glyphs are drawn at that size, sharp */
void text_set_scale(float scale);

float text_width(const char *s, float size, int weight);
/* one line; y is the top of the line box. Returns the width drawn. */
float text_draw(const char *s, float x, float y, float size, int weight, uint32_t color);
/* one line cut with an ellipsis to fit max_w */
float text_draw_fit(const char *s, float x, float y, float max_w, float size, int weight, uint32_t color);
/* wrapped into lines of max_w, at most max_lines (the last one ends in an ellipsis when cut).
   Returns the height used. Draws nothing when color is 0 (to measure). */
float text_draw_wrap(const char *s, float x, float y, float max_w, float size, int weight, uint32_t color,
                     int max_lines, float line_height);
/* the height of one line of this size */
float text_line_height(float size);

/* when the atlas fills it starts over; counted for the diagnostics */
int text_atlas_resets(void);

#endif
