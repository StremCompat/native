#include "util.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  char *data = malloc((size_t)n + 1);
  if (!data) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(data, 1, (size_t)n, f);
  fclose(f);
  data[got] = 0;
  if (size) *size = got;
  return data;
}

int write_file(const char *path, const void *data, size_t size) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  size_t put = fwrite(data, 1, size, f);
  fclose(f);
  return put == size ? 0 : -1;
}

uint32_t utf8_next(const char **pp) {
  const unsigned char *p = (const unsigned char *)*pp;
  uint32_t c = p[0];
  int n = 0;
  if (c < 0x80) n = 0;
  else if ((c & 0xE0) == 0xC0) c &= 0x1F, n = 1;
  else if ((c & 0xF0) == 0xE0) c &= 0x0F, n = 2;
  else if ((c & 0xF8) == 0xF0) c &= 0x07, n = 3;
  else {
    *pp += 1;
    return 0xFFFD;
  }
  for (int i = 1; i <= n; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      *pp += i;
      return 0xFFFD;
    }
    c = (c << 6) | (p[i] & 0x3F);
  }
  *pp += n + 1;
  return c;
}

double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

static void sb_grow(sbuf *b, size_t extra) {
  if (b->len + extra + 1 <= b->cap) return;
  size_t cap = b->cap ? b->cap * 2 : 64;
  while (cap < b->len + extra + 1) cap *= 2;
  b->s = realloc(b->s, cap);
  b->cap = cap;
}

void sb_addn(sbuf *b, const char *s, size_t n) {
  sb_grow(b, n);
  memcpy(b->s + b->len, s, n);
  b->len += n;
  b->s[b->len] = 0;
}

void sb_add(sbuf *b, const char *s) { sb_addn(b, s, strlen(s)); }

void sb_addf(sbuf *b, const char *fmt, ...) {
  va_list ap, copy;
  va_start(ap, fmt);
  va_copy(copy, ap);
  int n = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (n > 0) {
    sb_grow(b, (size_t)n);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap);
    b->len += (size_t)n;
  }
  va_end(ap);
}

void sb_free(sbuf *b) {
  free(b->s);
  b->s = NULL;
  b->len = b->cap = 0;
}

void sb_add_url(sbuf *b, const char *s) {
  static const char hex[] = "0123456789ABCDEF";
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-_.~", *p)) {
      sb_addn(b, (const char *)p, 1);
    } else {
      char e[3] = {'%', hex[*p >> 4], hex[*p & 15]};
      sb_addn(b, e, 3);
    }
  }
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

float ease_out(float t) {
  t = clampf(t, 0, 1);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

char *str_dup(const char *s) {
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}
