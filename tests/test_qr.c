/* The app's QR codes are the same symbols as the server's (service/server/qr.js): the expected
   modules below are what qr.js gives for these texts. */
#include <stdio.h>
#include <string.h>

#include "qr.h"

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  FILE *f = fopen(argv[1], "r");
  if (!f) return 2;
  static char expected[2][QR_MAX_SIZE * QR_MAX_SIZE + 2], got[QR_MAX_SIZE * QR_MAX_SIZE + 1];
  const char *texts[2] = {"https://github.com/StremCompat/webos/issues/new?title=Start%20failed%20STALE_SERVICE%205.0.6", "HELLO"};
  int fails = 0;
  for (int i = 0; i < 2; i++) {
    if (!fgets(expected[i], sizeof expected[i], f)) return 2;
    expected[i][strcspn(expected[i], "\r\n")] = 0;
    int size = qr_encode(texts[i], got);
    if (!size || strcmp(got, expected[i])) {
      printf("FAIL: the symbol of \"%s\" differs from the server's (size %d)\n", texts[i], size);
      fails++;
    }
  }
  fclose(f);
  if (!fails) printf("OK: the app's QR codes match the server's\n");
  return fails;
}
