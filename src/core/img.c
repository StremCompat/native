#include "img.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "util.h"

/* stb_image allocates with malloc, so its pixels are freed with free() like ours */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define ENTRIES 512 /* far more than a screen shows; when full, the one drawn longest ago goes */
#define FADE_MS 220.0
#define RETRY_MS 3000.0 /* a server that did not answer is asked again after this */

typedef enum { LOADING, READY, FAILED } state;

/* key: "<width> <address>", so one picture asked at two sizes is two textures */
typedef struct {
  char *key; /* NULL: a free entry */
  state st;
  int unreachable; /* failed because the server did not answer, not because of the picture */
  sc_tex tex;
  double ready_at, used_at;
} entry;

typedef struct {
  unsigned char *px;
  int w, h;
} decoded;

static entry entries[ENTRIES];
static size_t budget, held;
static double clock_now;
static int skip_fades;

static size_t tex_bytes(const sc_tex *t) { return (size_t)t->w * t->h * 4; }

void img_init(size_t bytes) { budget = bytes; }

size_t img_bytes(void) { return held; }

void img_skip_fades(int on) { skip_fades = on; }

static entry *find(const char *key) {
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].key && !strcmp(entries[i].key, key)) return &entries[i];
  return NULL;
}

static void forget(entry *e) {
  if (e->st == READY) held -= tex_bytes(&e->tex);
  gfx_texture_free(&e->tex);
  free(e->key);
  memset(e, 0, sizeof *e);
}

/* the picture drawn longest ago, not one on its way, and not one the last frame drew */
static entry *oldest(double before) {
  entry *o = NULL;
  for (int i = 0; i < ENTRIES; i++) {
    entry *e = &entries[i];
    if (e->key && e->st != LOADING && e->used_at < before && (!o || e->used_at < o->used_at)) o = e;
  }
  return o;
}

/* a picture made smaller by averaging the source pixels under each new one */
static unsigned char *shrink(const unsigned char *src, int w, int h, int dw, int dh) {
  unsigned char *dst = malloc((size_t)dw * dh * 4);
  if (!dst) return NULL;
  for (int y = 0; y < dh; y++) {
    int y0 = y * h / dh, y1 = (y + 1) * h / dh;
    if (y1 <= y0) y1 = y0 + 1;
    for (int x = 0; x < dw; x++) {
      int x0 = x * w / dw, x1 = (x + 1) * w / dw;
      if (x1 <= x0) x1 = x0 + 1;
      unsigned sum[4] = {0, 0, 0, 0}, n = (unsigned)((x1 - x0) * (y1 - y0));
      for (int sy = y0; sy < y1; sy++) {
        const unsigned char *p = src + ((size_t)sy * w + x0) * 4;
        for (int sx = x0; sx < x1; sx++, p += 4)
          for (int c = 0; c < 4; c++) sum[c] += p[c];
      }
      unsigned char *o = dst + ((size_t)y * dw + x) * 4;
      for (int c = 0; c < 4; c++) o[c] = (unsigned char)(sum[c] / n);
    }
  }
  return dst;
}

/* worker thread: the answer's bytes into pixels no wider than the key asks */
static void decode(net_reply *r, void *user) {
  int width = atoi(user), ch;
  if (r->status != 200 || !r->len) return;
  decoded *d = calloc(1, sizeof *d);
  d->px = stbi_load_from_memory((const unsigned char *)r->body, (int)r->len, &d->w, &d->h, &ch, 4);
  if (!d->px) {
    free(d);
    return;
  }
  if (width > 0 && d->w > width) {
    int dh = (int)((long)d->h * width / d->w);
    if (dh < 1) dh = 1;
    unsigned char *small = shrink(d->px, d->w, d->h, width, dh);
    if (small) {
      free(d->px);
      d->px = small;
      d->w = width;
      d->h = dh;
    }
  }
  r->result = d;
}

/* main thread: pixels into a texture. The entry is found again by its key, since it may have
   been given to another picture while this one was on its way. */
static void arrived(net_reply *r, void *user) {
  char *key = user;
  decoded *d = r->result;
  entry *e = find(key);
  if (e && e->st == LOADING) {
    if (d) {
      e->tex = gfx_texture(d->w, d->h, d->px, 0);
      e->st = READY;
      e->ready_at = clock_now;
      held += tex_bytes(&e->tex);
    } else {
      e->st = FAILED;
      e->unreachable = r->status == 0;
      e->ready_at = clock_now;
    }
  }
  if (d) {
    free(d->px);
    free(d);
  }
  free(key);
}

static void load(entry *e) {
  sbuf req = {0};
  sb_add(&req, "/app/img?u=");
  sb_add_url(&req, strchr(e->key, ' ') + 1);
  /* a small picture (a channel logo, a store card) is made that wide by the server first */
  int width = atoi(e->key);
  if (width > 0 && width <= 640) sb_addf(&req, "&w=%d", width * 2);
  e->st = LOADING;
  net_request(req.s, NULL, NET_LOW, decode, arrived, str_dup(e->key));
  sb_free(&req);
}

static void key_of(char *key, size_t size, const char *url, int width) { snprintf(key, size, "%d %s", width, url); }

int img_failed(const char *url, int width) {
  if (!url || !*url) return 1;
  char key[2048];
  key_of(key, sizeof key, url, width);
  entry *e = find(key);
  return e && e->st == FAILED && !e->unreachable;
}

sc_tex img_load_file(const char *path, int alpha_only) {
  sc_tex t = {0, 0, 0, alpha_only};
  size_t n = 0;
  unsigned char *data = (unsigned char *)read_file(path, &n);
  int w, h, ch;
  unsigned char *px = data ? stbi_load_from_memory(data, (int)n, &w, &h, &ch, 4) : NULL;
  free(data);
  if (!px) return t;
  /* the alpha channel alone, packed in place */
  if (alpha_only)
    for (int i = 0; i < w * h; i++) px[i] = px[i * 4 + 3];
  t = gfx_texture(w, h, px, alpha_only);
  free(px);
  return t;
}

const sc_tex *img_get(const char *url, int width, float *fade) {
  if (fade) *fade = 0;
  if (!url || !*url) return NULL;
  char key[2048];
  key_of(key, sizeof key, url, width);
  entry *e = find(key);
  if (!e) {
    for (int i = 0; i < ENTRIES && !e; i++)
      if (!entries[i].key) e = &entries[i];
    if (!e && (e = oldest(clock_now))) forget(e);
    if (!e) return NULL;
    e->key = str_dup(key);
    load(e);
  } else if (e->st == FAILED && e->unreachable && clock_now - e->ready_at > RETRY_MS) {
    load(e);
  }
  e->used_at = clock_now;
  if (e->st != READY) return NULL;
  if (fade && skip_fades) *fade = 1;
  else if (fade) *fade = ease_out(clampf((float)((clock_now - e->ready_at) / FADE_MS), 0, 1));
  return &e->tex;
}

void img_frame(double t) {
  double last_frame = clock_now;
  clock_now = t;
  /* the pictures not drawn for the longest time go until the rest fits the budget */
  entry *o;
  while (held > budget && (o = oldest(last_frame))) forget(o);
}
