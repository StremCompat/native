/* QR codes drawn by the app itself, for when the local server is what failed (the start screen's
   report link). The same symbol as the server's (service/server/qr.js): byte mode, error
   correction level M, versions 1 to 10 (up to 213 bytes). */
#ifndef SC_QR_H
#define SC_QR_H

#define QR_MAX_SIZE 57 /* version 10 */

/* the smallest symbol holding text, as size x size bytes of '1' (dark) and '0', row by row, in
   out (at least QR_MAX_SIZE * QR_MAX_SIZE + 1); its side, or 0 when text is too long */
int qr_encode(const char *text, char *out);

#endif
