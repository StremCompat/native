#include "img.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "util.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define SLOTS 1024 /* open addressing; far more than a screen shows */
#define FADE_MS 220.0
#define MAX_TRIES 2
#define FORGET_MS 120000.0 /* pictures not asked for in this long are forgotten when room is needed */

/* FREE: no picture here. IDLE: known but without a texture (sent away for room, or to retry). */
typedef enum { FREE, IDLE, LOADING, READY, FAILED } state;

typedef struct {
  char *path;
  uint32_t hash;
  state st;
  int tries;
  sc_tex tex;
  double ready_at, used_at;
} entry;

typedef struct {
  unsigned char *px;
  int w, h;
} decoded;

static entry table[SLOTS];
static int used_slots;
static size_t budget, held;
static double clock_now;
static int skip_fades;

static uint32_t hash_of(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
  return h;
}

static size_t tex_bytes(const sc_tex *t) { return (size_t)t->w * t->h * 4; }

void img_init(size_t bytes) { budget = bytes; }

size_t img_bytes(void) { return held; }

void img_skip_fades(int on) { skip_fades = on; }

/* the entry for this path, or the free slot where it would go */
static entry *slot_for(const char *path, uint32_t h) {
  for (int i = 0; i < SLOTS; i++) {
    entry *e = &table[(h + (uint32_t)i) % SLOTS];
    if (e->st == FREE || (e->hash == h && !strcmp(e->path, path))) return e;
  }
  return NULL;
}

/* worker thread: the answer's bytes into pixels */
static void decode(net_reply *r, void *user) {
  (void)user;
  if (r->status != 200 || !r->len) return;
  decoded *d = calloc(1, sizeof *d);
  int ch;
  d->px = stbi_load_from_memory((const unsigned char *)r->body, (int)r->len, &d->w, &d->h, &ch, 4);
  if (!d->px) {
    free(d);
    return;
  }
  r->result = d;
}

/* main thread: pixels into a texture. The entry is found again by its path, since the table
   may have been compacted while the picture was on its way. */
static void arrived(net_reply *r, void *user) {
  char *path = user;
  decoded *d = r->result;
  entry *e = slot_for(path, hash_of(path));
  if (e && e->st == LOADING) {
    if (d) {
      e->tex = gfx_texture(d->w, d->h, d->px, 0);
      e->st = READY;
      e->ready_at = clock_now;
      held += tex_bytes(&e->tex);
    } else if (r->status == 0 && ++e->tries < MAX_TRIES) {
      e->st = IDLE; /* the server was not up yet: asked again the next time it is drawn */
    } else {
      e->st = FAILED;
    }
  }
  if (d) {
    stbi_image_free(d->px);
    free(d);
  }
  free(path);
}

static void load(entry *e) {
  e->st = LOADING;
  net_request(e->path, NULL, NET_LOW, decode, arrived, str_dup(e->path));
}

const sc_tex *img_get(const char *path, float *fade) {
  if (fade) *fade = 0;
  if (!path || !*path) return NULL;
  uint32_t h = hash_of(path);
  entry *e = slot_for(path, h);
  if (!e) return NULL;
  if (e->st == FREE) {
    e->path = str_dup(path);
    e->hash = h;
    used_slots++;
    load(e);
  } else if (e->st == IDLE) {
    load(e);
  }
  e->used_at = clock_now;
  if (e->st != READY) return NULL;
  if (fade && skip_fades) *fade = 1;
  else if (fade) *fade = ease_out(clampf((float)((clock_now - e->ready_at) / FADE_MS), 0, 1));
  return &e->tex;
}

/* entries cannot simply be emptied (that would break the probe chains of others), so when
   the table gets crowded it is rebuilt with only the pictures still in use */
static void compact(void) {
  static entry keep[SLOTS];
  int n = 0;
  for (int i = 0; i < SLOTS; i++) {
    entry *e = &table[i];
    if (e->st == FREE) continue;
    if (e->st == READY || e->st == LOADING || clock_now - e->used_at < FORGET_MS) keep[n++] = *e;
    else free(e->path);
  }
  memset(table, 0, sizeof table);
  for (int i = 0; i < n; i++) *slot_for(keep[i].path, keep[i].hash) = keep[i];
  used_slots = n;
}

void img_frame(double t) {
  double last_frame = clock_now;
  clock_now = t;
  /* the textures not drawn for the longest time go until the rest fits the budget; what the
     last frame drew stays, or the screen would flicker */
  while (held > budget) {
    entry *oldest = NULL;
    for (int i = 0; i < SLOTS; i++)
      if (table[i].st == READY && table[i].used_at < last_frame &&
          (!oldest || table[i].used_at < oldest->used_at))
        oldest = &table[i];
    if (!oldest) break;
    held -= tex_bytes(&oldest->tex);
    gfx_texture_free(&oldest->tex);
    oldest->st = IDLE;
    oldest->tries = 0;
  }
  if (used_slots > SLOTS * 3 / 4) compact();
}
