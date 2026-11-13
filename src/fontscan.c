#include "fontscan.h"

#ifndef _WIN32
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "text.h"
#include "util.h"

#define MAX_FILES 256

static char *files[MAX_FILES];
static int nfiles;

static int is_font(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && (!strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf") || !strcasecmp(dot, ".ttc"));
}

static void walk(const char *dir, int depth) {
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) && nfiles < MAX_FILES) {
    if (e->d_name[0] == '.') continue;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    if (is_font(e->d_name)) files[nfiles++] = str_dup(path);
    else if (depth < 4) walk(path, depth + 1);
  }
  closedir(d);
}

static int cjk_rank(const char *path) {
  static const char *hints[] = {"cjk", "fallback", "kr", "jp", "sc", "tc", "han", "gothic", "ming", "hei"};
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  char low[256];
  size_t n = 0;
  for (; name[n] && n < sizeof low - 1; n++) low[n] = (char)(name[n] >= 'A' && name[n] <= 'Z' ? name[n] + 32 : name[n]);
  low[n] = 0;
  for (size_t i = 0; i < sizeof hints / sizeof hints[0]; i++)
    if (strstr(low, hints[i])) return 0;
  return 1;
}

static int by_rank(const void *a, const void *b) {
  const char *x = *(char *const *)a, *y = *(char *const *)b;
  int r = cjk_rank(x) - cjk_rank(y);
  return r ? r : strcmp(x, y);
}

void fontscan_add(const char *dir) {
  nfiles = 0;
  walk(dir, 0);
  qsort(files, (size_t)nfiles, sizeof files[0], by_rank);
  for (int i = 0; i < nfiles; i++) {
    text_add_fallback(files[i]);
    free(files[i]);
  }
}
#endif
