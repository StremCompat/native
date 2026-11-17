/* Small helpers shared by every part: files, UTF-8, time, growing strings. */
#ifndef SC_UTIL_H
#define SC_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* the whole file, ending in a zero byte; NULL when it cannot be read */
char *read_file(const char *path, size_t *size);
int write_file(const char *path, const void *data, size_t size);

/* the next character, moving *p past it; invalid bytes come back as U+FFFD */
uint32_t utf8_next(const char **p);

/* a copy of s on the heap */
char *str_dup(const char *s);

/* s in capitals, for the Latin letters with or without accents (others stay as they are) */
void utf8_upper(char *out, size_t size, const char *s);

/* milliseconds since an arbitrary start */
double now_ms(void);

/* the system's memory in MB, all of it and what is free for use; 0 when it is known (Linux:
   webOS and Android), -1 elsewhere */
int sys_memory_mb(int *total, int *available);

/* a string that grows as text is added */
typedef struct {
  char *s;
  size_t len, cap;
} sbuf;
void sb_add(sbuf *b, const char *s);
void sb_addn(sbuf *b, const char *s, size_t n);
void sb_addf(sbuf *b, const char *fmt, ...);
void sb_free(sbuf *b);

/* "%20"-style escaping for a URL query value */
void sb_add_url(sbuf *b, const char *s);

float clampf(float v, float lo, float hi);
/* eases a 0..1 progress: fast start, gentle stop */
float ease_out(float t);

#endif
