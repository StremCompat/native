/* Windows and desktop Linux: for development and the screenshot tests. */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

#ifdef _WIN32

void platform_add_fonts(void) {
  /* Korean, Japanese, Simplified and Traditional Chinese, then symbols */
  static const char *names[] = {"malgun.ttf", "YuGothR.ttc", "msgothic.ttc", "msyh.ttc", "msjh.ttc", "seguisym.ttf"};
  const char *windir = getenv("WINDIR");
  char path[512];
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    snprintf(path, sizeof path, "%s/Fonts/%s", windir ? windir : "C:/Windows", names[i]);
    FILE *f = fopen(path, "rb");
    if (!f) continue;
    fclose(f);
    text_add_fallback(path);
  }
}

#else

#include "fontscan.h"

void platform_add_fonts(void) { fontscan_add("/usr/share/fonts"); }

#endif
