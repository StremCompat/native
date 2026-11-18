#include "qr.h"

#include <stdlib.h>
#include <string.h>

/* per version 1 to 10, level M: error correction codewords per block, and the number of blocks */
static const int ECC_PER_BLOCK[] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
static const int BLOCKS[] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};

#define MAX_CODEWORDS 400

/* ---------- arithmetic in GF(256), modulo x^8 + x^4 + x^3 + x^2 + 1 ---------- */
static int multiply(int x, int y) {
  int z = 0;
  for (int i = 7; i >= 0; i--) {
    z = ((z << 1) ^ ((z >> 7) * 0x11d)) & 0xff;
    z ^= ((y >> i) & 1) * x;
  }
  return z;
}

static void divisor(int degree, int *out) {
  int root = 1;
  for (int i = 0; i < degree - 1; i++) out[i] = 0;
  out[degree - 1] = 1;
  for (int i = 0; i < degree; i++) {
    for (int j = 0; j < degree; j++) {
      out[j] = multiply(out[j], root);
      if (j + 1 < degree) out[j] ^= out[j + 1];
    }
    root = multiply(root, 0x02);
  }
}

static void remainder(const int *data, int n, const int *div, int degree, int *out) {
  for (int i = 0; i < degree; i++) out[i] = 0;
  for (int k = 0; k < n; k++) {
    int factor = data[k] ^ out[0];
    memmove(out, out + 1, (size_t)(degree - 1) * sizeof *out);
    out[degree - 1] = 0;
    for (int i = 0; i < degree; i++) out[i] ^= multiply(div[i], factor);
  }
}

/* ---------- sizes ---------- */
static int raw_modules(int version) {
  int result = (16 * version + 128) * version + 64;
  if (version >= 2) {
    int align = version / 7 + 2;
    result -= (25 * align - 10) * align - 55;
    if (version >= 7) result -= 36;
  }
  return result;
}

static int data_codewords(int version) { return raw_modules(version) / 8 - ECC_PER_BLOCK[version] * BLOCKS[version]; }

static int alignment_positions(int version, int size, int *out) {
  if (version == 1) return 0;
  int count = version / 7 + 2, step = ((version * 4 + 4 + (count * 2 - 2) - 1) / (count * 2 - 2)) * 2, n = 1;
  out[0] = 6;
  for (int pos = size - 7; n < count; pos -= step) {
    memmove(out + 2, out + 1, (size_t)(n - 1) * sizeof *out);
    out[1] = pos;
    n++;
  }
  return count;
}

/* ---------- the data: mode, length, bytes, padding, then error correction, interleaved ---------- */
static int codewords(const unsigned char *bytes, int len, int version, int *result) {
  static unsigned char bits[MAX_CODEWORDS * 8];
  int nbits = 0;
#define PUT(value, length)                                                                                 \
  for (int b_ = (length) - 1; b_ >= 0; b_--) bits[nbits++] = (unsigned char)(((value) >> b_) & 1)
  PUT(4, 4);
  PUT(len, version < 10 ? 8 : 16);
  for (int i = 0; i < len; i++) PUT(bytes[i], 8);
  int capacity = data_codewords(version) * 8, tail = capacity - nbits < 4 ? capacity - nbits : 4;
  PUT(0, tail);
  PUT(0, (8 - nbits % 8) % 8);
  for (int pad = 0xec; nbits < capacity; pad ^= 0xec ^ 0x11) PUT(pad, 8);
#undef PUT
  int data[MAX_CODEWORDS], ndata = nbits / 8;
  for (int i = 0; i < ndata; i++) {
    int byte = 0;
    for (int k = 0; k < 8; k++) byte = (byte << 1) | bits[i * 8 + k];
    data[i] = byte;
  }
  int block_count = BLOCKS[version], ecc_len = ECC_PER_BLOCK[version], raw = raw_modules(version) / 8,
      short_blocks = block_count - raw % block_count, short_len = raw / block_count;
  int div[32], blocks[8][160], block_len = short_len + 1, at = 0;
  divisor(ecc_len, div);
  for (int i = 0; i < block_count; i++) {
    int n = short_len - ecc_len + (i < short_blocks ? 0 : 1);
    memcpy(blocks[i], data + at, (size_t)n * sizeof(int));
    at += n;
    int ecc[32];
    remainder(blocks[i], n, div, ecc_len, ecc);
    if (i < short_blocks) blocks[i][n++] = 0;
    memcpy(blocks[i] + n, ecc, (size_t)ecc_len * sizeof(int));
  }
  int out = 0;
  for (int i = 0; i < block_len; i++)
    for (int j = 0; j < block_count; j++)
      if (i != short_len - ecc_len || j >= short_blocks) result[out++] = blocks[j][i];
  return out;
}

/* ---------- the symbol ---------- */
static int masked(int mask, int x, int y) {
  switch (mask) {
  case 0: return (x + y) % 2 == 0;
  case 1: return y % 2 == 0;
  case 2: return x % 3 == 0;
  case 3: return (x + y) % 3 == 0;
  case 4: return (x / 3 + y / 2) % 2 == 0;
  case 5: return (x * y) % 2 + (x * y) % 3 == 0;
  case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
  default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
  }
}

static unsigned char dark[QR_MAX_SIZE][QR_MAX_SIZE], fixed[QR_MAX_SIZE][QR_MAX_SIZE];

static void set(int x, int y, int on) {
  dark[y][x] = (unsigned char)on;
  fixed[y][x] = 1;
}

static int iabs(int v) { return v < 0 ? -v : v; }
static int imax(int a, int b) { return a > b ? a : b; }

static void symbol(int version, const int *data, int ndata, int mask) {
  int size = version * 4 + 17;
  memset(dark, 0, sizeof dark);
  memset(fixed, 0, sizeof fixed);
  for (int i = 0; i < size; i++) set(6, i, i % 2 == 0), set(i, 6, i % 2 == 0);
  int finders[3][2] = {{3, 3}, {size - 4, 3}, {3, size - 4}};
  for (int f = 0; f < 3; f++)
    for (int dy = -4; dy <= 4; dy++)
      for (int dx = -4; dx <= 4; dx++) {
        int d = imax(iabs(dx), iabs(dy)), x = finders[f][0] + dx, y = finders[f][1] + dy;
        if (x >= 0 && x < size && y >= 0 && y < size) set(x, y, d != 2 && d != 4);
      }
  int align[7], na = alignment_positions(version, size, align);
  for (int a = 0; a < na; a++)
    for (int b = 0; b < na; b++) {
      if ((a == 0 && b == 0) || (a == 0 && b == na - 1) || (a == na - 1 && b == 0)) continue;
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) set(align[a] + dx, align[b] + dy, imax(iabs(dx), iabs(dy)) != 1);
    }
  /* the format: level M (00) and the mask */
  int rem = mask;
  for (int k = 0; k < 10; k++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  int fbits = ((mask << 10) | rem) ^ 0x5412;
#define BIT(n) (((fbits >> (n)) & 1) != 0)
  for (int k = 0; k <= 5; k++) set(8, k, BIT(k));
  set(8, 7, BIT(6));
  set(8, 8, BIT(7));
  set(7, 8, BIT(8));
  for (int k = 9; k < 15; k++) set(14 - k, 8, BIT(k));
  for (int k = 0; k < 8; k++) set(size - 1 - k, 8, BIT(k));
  for (int k = 8; k < 15; k++) set(8, size - 15 + k, BIT(k));
#undef BIT
  set(8, size - 8, 1);
  if (version >= 7) {
    int vrem = version;
    for (int i = 0; i < 12; i++) vrem = (vrem << 1) ^ ((vrem >> 11) * 0x1f25);
    int vbits = (version << 12) | vrem;
    for (int i = 0; i < 18; i++) {
      int on = ((vbits >> i) & 1) != 0, a = size - 11 + i % 3, b = i / 3;
      set(a, b, on);
      set(b, a, on);
    }
  }
  int n = 0;
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < size; vert++)
      for (int j = 0; j < 2; j++) {
        int x = right - j, up = ((right + 1) & 2) == 0, y = up ? size - 1 - vert : vert;
        if (!fixed[y][x] && n < ndata * 8) {
          dark[y][x] = (unsigned char)(((data[n >> 3] >> (7 - (n & 7))) & 1) != 0);
          n++;
        }
      }
  }
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      if (!fixed[y][x] && masked(mask, x, y)) dark[y][x] = !dark[y][x];
}

/* how hard a symbol is to read, as the standard scores it: the mask with the lowest score is used */
static int penalty(int size) {
  int score = 0, dark_count = 0;
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < size; a++) {
      int run = 1;
      for (int b = 1; b <= size; b++) {
        int same = b < size && (pass ? dark[b][a] == dark[b - 1][a] : dark[a][b] == dark[a][b - 1]);
        if (same) run++;
        else {
          if (run >= 5) score += run - 2;
          run = 1;
        }
      }
    }
  for (int y = 0; y < size - 1; y++)
    for (int x = 0; x < size - 1; x++)
      if (dark[y][x] == dark[y][x + 1] && dark[y][x] == dark[y + 1][x] && dark[y][x] == dark[y + 1][x + 1]) score += 3;
  static const unsigned char finder[7] = {1, 0, 1, 1, 1, 0, 1};
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < size; a++)
      for (int b = 0; b + 7 <= size; b++) {
        int match = 1;
        for (int k = 0; k < 7 && match; k++) match = (pass ? dark[b + k][a] : dark[a][b + k]) == finder[k];
        if (!match) continue;
        int before = 1, after = 1;
        for (int k = 1; k <= 4; k++) {
          if (b - k >= 0 && (pass ? dark[b - k][a] : dark[a][b - k])) before = 0;
          if (b + 6 + k < size && (pass ? dark[b + 6 + k][a] : dark[a][b + 6 + k])) after = 0;
        }
        if (before || after) score += 40;
      }
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) dark_count += dark[y][x];
  score += iabs(dark_count * 20 - size * size * 10) / (size * size) * 10;
  return score;
}

int qr_encode(const char *text, char *out) {
  int len = (int)strlen(text), version = 1;
  while (version <= 10 && data_codewords(version) - (version < 10 ? 2 : 3) < len) version++;
  if (version > 10) return 0;
  int data[MAX_CODEWORDS], ndata = codewords((const unsigned char *)text, len, version, data), size = version * 4 + 17;
  int best = 0, best_score = -1;
  for (int mask = 0; mask < 8; mask++) {
    symbol(version, data, ndata, mask);
    int score = penalty(size);
    if (best_score < 0 || score < best_score) best = mask, best_score = score;
  }
  symbol(version, data, ndata, best);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) out[y * size + x] = dark[y][x] ? '1' : '0';
  out[size * size] = 0;
  return size;
}
