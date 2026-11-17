/* Requests to the app's own local server (127.0.0.1), the only thing the interface talks to.
   They run on worker threads; the answers are handed back on the main thread by net_poll, so
   screens never wait for the network while drawing. */
#ifndef SC_NET_H
#define SC_NET_H

#include <stddef.h>

typedef struct {
  int status;   /* HTTP status, or 0 when the server could not be reached */
  char *body;   /* ends in a zero byte; freed after the done callback */
  size_t len;
  void *result; /* what the work callback made of the body (a decoded picture...) */
} net_reply;

/* runs on the worker thread after the answer arrived, for slow work such as decoding */
typedef void (*net_work_fn)(net_reply *r, void *user);
/* runs on the main thread, inside net_poll */
typedef void (*net_done_fn)(net_reply *r, void *user);

/* screens' data first, pictures after; the slow lane has a worker of its own, for what the server
   may hold a while (the sources as the addons answer), so it never keeps the others waiting */
enum { NET_HIGH, NET_LOW, NET_SLOW };

int net_init(int port, int workers);
void net_shutdown(void);

/* body NULL sends a GET, otherwise a POST of JSON */
void net_request(const char *path, const char *body, int priority, net_work_fn work, net_done_fn done, void *user);
/* the same, waiting for the answer here; the caller frees r->body */
void net_fetch(const char *path, const char *body, net_reply *r);

void net_poll(void);
/* requests not yet handed back */
int net_pending(void);

#endif
