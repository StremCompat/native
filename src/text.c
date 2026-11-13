#include "text.h"
#include "gfx.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* only part of stb_truetype is used; the rest would be warned about as unused */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#define ATLAS 1024
#define CACHE_SLOTS 4096
#define MAX_FONTS 12
#define MAX_CANDIDATES 64
#define BLOCKS (0x110000 >> 8) /* 256-character blocks of Unicode */

typedef struct {
  unsigned char *data;
  stbtt_fontinfo info;
  int ascent, descent, gap;
} font;

typedef struct {
  uint32_t key_cp;
  uint16_t key_size; /* pixel size at the surface */
  uint8_t key_weight, used;
  uint8_t font;
  int16_t x, y, w, h;      /* in the atlas */
  int16_t ox, oy;          /* offset from the pen, surface pixels */
  float advance;           /* surface pixels */
} glyph;

/* A system font that may cover what Inter does not. It is only read from disk when a
   character is missing, and kept only if it has it; a block it lacked is not tried again. */
typedef struct {
  char *path;
  int loaded;
  uint8_t lacks[BLOCKS / 8];
} candidate;

static font fonts[MAX_FONTS];
static int nfonts;            /* the first W_COUNT are the Inter weights, then the fallbacks in use */
static candidate candidates[MAX_CANDIDATES];
static int ncandidates;
static glyph cache[CACHE_SLOTS];
static sc_tex atlas;
static int pen_x, pen_y, row_h;
static float scale = 1.0f;
static int resets;

static int load_font(font *f, const char *path) {
  size_t n = 0;
  f->data = (unsigned char *)read_file(path, &n);
  if (!f->data) return -1;
  if (!stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
    free(f->data);
    f->data = NULL;
    return -1;
  }
  stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->gap);
  return 0;
}

int text_init(const char *dir) {
  static const char *names[W_COUNT] = {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf",
                                       "Inter-Bold.ttf", "Inter-ExtraBold.ttf"};
  char path[512];
  for (int i = 0; i < W_COUNT; i++) {
    snprintf(path, sizeof path, "%s/%s", dir, names[i]);
    if (load_font(&fonts[i], path)) {
      fprintf(stderr, "font missing: %s\n", path);
      return -1;
    }
  }
  nfonts = W_COUNT;
  unsigned char *blank = calloc(ATLAS * ATLAS, 1);
  atlas = gfx_texture(ATLAS, ATLAS, blank, 1);
  free(blank);
  return 0;
}

int text_add_fallback(const char *path) {
  if (ncandidates >= MAX_CANDIDATES) return -1;
  candidates[ncandidates++].path = str_dup(path);
  return 0;
}

void text_set_scale(float s) {
  if (fabsf(s - scale) < 0.001f) return;
  scale = s;
  memset(cache, 0, sizeof cache);
  pen_x = pen_y = row_h = 0;
}

int text_atlas_resets(void) { return resets; }

/* the font that has this character: the weight asked for, else a fallback in use, else the
   next system font that turns out to have it */
static int font_for(uint32_t cp, int weight) {
  if (stbtt_FindGlyphIndex(&fonts[weight].info, (int)cp)) return weight;
  for (int i = W_COUNT; i < nfonts; i++)
    if (stbtt_FindGlyphIndex(&fonts[i].info, (int)cp)) return i;
  uint32_t block = cp >> 8;
  if (block >= BLOCKS) return weight;
  for (int i = 0; i < ncandidates && nfonts < MAX_FONTS; i++) {
    candidate *c = &candidates[i];
    if (c->loaded || (c->lacks[block / 8] & (1u << (block % 8)))) continue;
    font *f = &fonts[nfonts];
    if (!load_font(f, c->path) && stbtt_FindGlyphIndex(&f->info, (int)cp)) {
      c->loaded = 1;
      return nfonts++;
    }
    free(f->data);
    memset(f, 0, sizeof *f);
    c->lacks[block / 8] |= (uint8_t)(1u << (block % 8));
  }
  return weight;
}

static void reset_atlas(void) {
  unsigned char *blank = calloc(ATLAS * ATLAS, 1);
  gfx_texture_update(&atlas, 0, 0, ATLAS, ATLAS, blank);
  free(blank);
  memset(cache, 0, sizeof cache);
  pen_x = pen_y = row_h = 0;
  resets++;
}

static glyph *get_glyph(uint32_t cp, float size, int weight) {
  uint16_t px = (uint16_t)lroundf(size * scale);
  uint32_t h = (cp * 2654435761u) ^ ((uint32_t)px << 8) ^ (uint32_t)weight;
  for (int probe = 0; probe < 8; probe++) {
    glyph *g = &cache[(h + (uint32_t)probe) % CACHE_SLOTS];
    if (g->used && g->key_cp == cp && g->key_size == px && g->key_weight == weight) return g;
    if (!g->used) {
      int fi = font_for(cp, weight);
      font *f = &fonts[fi];
      float s = stbtt_ScaleForMappingEmToPixels(&f->info, px);
      int x0, y0, x1, y1, adv, lsb;
      stbtt_GetCodepointHMetrics(&f->info, (int)cp, &adv, &lsb);
      stbtt_GetCodepointBitmapBox(&f->info, (int)cp, s, s, &x0, &y0, &x1, &y1);
      int w = x1 - x0, hh = y1 - y0;
      if (pen_x + w + 1 > ATLAS) {
        pen_x = 0;
        pen_y += row_h + 1;
        row_h = 0;
      }
      if (pen_y + hh + 1 > ATLAS) {
        reset_atlas();
        return get_glyph(cp, size, weight);
      }
      if (w > 0 && hh > 0) {
        unsigned char *bmp = calloc((size_t)w * hh, 1);
        stbtt_MakeCodepointBitmap(&f->info, bmp, w, hh, w, s, s, (int)cp);
        gfx_texture_update(&atlas, pen_x, pen_y, w, hh, bmp);
        free(bmp);
      }
      g->used = 1;
      g->key_cp = cp;
      g->key_size = px;
      g->key_weight = (uint8_t)weight;
      g->font = (uint8_t)fi;
      g->x = (int16_t)pen_x;
      g->y = (int16_t)pen_y;
      g->w = (int16_t)w;
      g->h = (int16_t)hh;
      g->ox = (int16_t)x0;
      /* glyph boxes are measured from the baseline; keep them relative to the Inter baseline */
      g->oy = (int16_t)y0;
      g->advance = adv * s;
      pen_x += w + 1;
      if (hh > row_h) row_h = hh;
      return g;
    }
  }
  /* the slots around this one are taken: start the cache over */
  reset_atlas();
  return get_glyph(cp, size, weight);
}

float text_line_height(float size) {
  font *f = &fonts[0];
  float s = stbtt_ScaleForMappingEmToPixels(&f->info, size);
  return (f->ascent - f->descent) * s;
}

static float baseline(float size) {
  font *f = &fonts[0];
  return f->ascent * stbtt_ScaleForMappingEmToPixels(&f->info, size);
}

static float run(const char *s, int len, float x, float y, float size, int weight, uint32_t color, int draw) {
  const char *p = s, *end = len < 0 ? s + strlen(s) : s + len;
  float pen = 0, base = baseline(size);
  while (p < end && *p) {
    uint32_t cp = utf8_next(&p);
    if (cp == '\n') continue;
    glyph *g = get_glyph(cp, size, weight);
    if (draw && g->w > 0 && color) {
      float gx = x + pen + g->ox / scale, gy = y + base + g->oy / scale;
      gfx_alpha_quad(&atlas, gx, gy, g->w / scale, g->h / scale, (float)g->x / ATLAS, (float)g->y / ATLAS,
                     (float)(g->x + g->w) / ATLAS, (float)(g->y + g->h) / ATLAS, color);
    }
    pen += g->advance / scale;
  }
  return pen;
}

float text_width(const char *s, float size, int weight) { return run(s, -1, 0, 0, size, weight, 0, 0); }

float text_draw(const char *s, float x, float y, float size, int weight, uint32_t color) {
  return run(s, -1, x, y, size, weight, color, 1);
}

/* the longest start of s (in bytes) whose width plus the ellipsis fits */
static int fit_prefix(const char *s, int len, float max_w, float size, int weight) {
  float ell = text_width("\xE2\x80\xA6", size, weight), pen = 0;
  const char *p = s, *last = s;
  while (p < s + len && *p) {
    const char *before = p;
    uint32_t cp = utf8_next(&p);
    pen += get_glyph(cp, size, weight)->advance / scale;
    if (pen + ell > max_w) return (int)(before - s);
    last = p;
  }
  return (int)(last - s);
}

float text_draw_fit(const char *s, float x, float y, float max_w, float size, int weight, uint32_t color) {
  float w = text_width(s, size, weight);
  if (w <= max_w) return run(s, -1, x, y, size, weight, color, 1);
  int n = fit_prefix(s, (int)strlen(s), max_w, size, weight);
  /* no space before the ellipsis */
  while (n > 0 && s[n - 1] == ' ') n--;
  float drawn = run(s, n, x, y, size, weight, color, 1);
  return drawn + run("\xE2\x80\xA6", -1, x + drawn, y, size, weight, color, 1);
}

float text_draw_wrap(const char *s, float x, float y, float max_w, float size, int weight, uint32_t color,
                     int max_lines, float line_height) {
  const char *p = s;
  int lines = 0;
  if (line_height <= 0) line_height = text_line_height(size) * 1.25f;
  while (*p && lines < max_lines) {
    /* take words while they fit; a single word longer than the line is cut */
    const char *line = p, *brk = NULL, *q = p;
    float pen = 0;
    while (*q && *q != '\n') {
      const char *before = q;
      uint32_t cp = utf8_next(&q);
      if (cp == ' ') brk = before;
      pen += get_glyph(cp, size, weight)->advance / scale;
      /* CJK text has no spaces: any character can end the line */
      if (cp >= 0x2E80) brk = before;
      if (pen > max_w) {
        q = brk && brk > line ? brk : before;
        break;
      }
    }
    int len = (int)(q - line), last = lines == max_lines - 1;
    const char *next = q;
    while (*next == ' ' || *next == '\n') next++;
    if (last && *next) {
      /* the rest does not fit: end this line with an ellipsis */
      if (color) text_draw_fit(line, x, y + lines * line_height, max_w, size, weight, color);
      lines++;
      break;
    }
    if (color) run(line, len, x, y + lines * line_height, size, weight, color, 1);
    lines++;
    p = next;
  }
  return lines * line_height;
}
