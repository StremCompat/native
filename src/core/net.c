#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define close_socket close
#endif

#include "net.h"

#include <SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define TIMEOUT_S 20
/* Each lane has workers of its own, so the screens' data never waits behind pictures (whose workers
   also decode them) or behind a slow addon: with three shared workers, a Home's catalogs queued
   behind its covers and backdrops on a TV's processor. */
static const int LANE_WORKERS[3] = {4, 3, 4}; /* NET_HIGH (the screens' data), NET_LOW (pictures), NET_SLOW */
#define MAX_WORKERS 11

typedef struct job job;
struct job {
  char *path, *body;
  net_work_fn work;
  net_done_fn done;
  void *user;
  net_reply reply;
  job *next;
};

static int port;
static SDL_mutex *lock;
static SDL_cond *wake;
static job *queue[3], *finished; /* waiting by lane (net.h); answered, for net_poll */
static SDL_Thread *workers[MAX_WORKERS];
static int nworkers, stopping, pending;

static void push(job **list, job *j) {
  j->next = NULL;
  while (*list) list = &(*list)->next;
  *list = j;
}

static job *pop(job **list) {
  job *j = *list;
  if (j) *list = j->next;
  return j;
}

/* "HTTP/1.1 200 OK\r\n...\r\n\r\nbody": the status and the body moved to the front */
static void parse_reply(sbuf *raw, net_reply *r) {
  r->status = 0;
  r->body = NULL;
  r->len = 0;
  if (!raw->s) return;
  const char *sp = strchr(raw->s, ' '), *head_end = strstr(raw->s, "\r\n\r\n");
  if (!sp || !head_end) {
    sb_free(raw);
    return;
  }
  r->status = atoi(sp + 1);
  size_t start = (size_t)(head_end + 4 - raw->s);
  r->len = raw->len - start;
  memmove(raw->s, raw->s + start, r->len);
  raw->s[r->len] = 0;
  r->body = raw->s;
  raw->s = NULL;
}

/* HTTP/1.0 with the connection closed at the end: the server sends the body plainly, never
   in chunks, and its end is where the connection ends */
static void fetch(const char *path, const char *body, net_reply *r) {
  sbuf raw = {0};
  sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  addr.sin_addr.s_addr = htonl(0x7F000001);
  if (s == INVALID_SOCKET) goto done;
#ifdef _WIN32
  DWORD tv = TIMEOUT_S * 1000;
#else
  struct timeval tv = {TIMEOUT_S, 0};
#endif
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
  if (connect(s, (struct sockaddr *)&addr, sizeof addr) != 0) goto done;
  sbuf req = {0};
  size_t blen = body ? strlen(body) : 0;
  sb_addf(&req, "%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n", body ? "POST" : "GET", path);
  if (body) sb_addf(&req, "Content-Type: application/json\r\nContent-Length: %u\r\n", (unsigned)blen);
  sb_add(&req, "\r\n");
  if (body) sb_addn(&req, body, blen);
  size_t sent = 0;
  while (sent < req.len) {
    int n = send(s, req.s + sent, (int)(req.len - sent), 0);
    if (n <= 0) break;
    sent += (size_t)n;
  }
  int complete = sent == req.len;
  sb_free(&req);
  if (!complete) goto done;
  char buf[16384];
  for (;;) {
    int n = recv(s, buf, sizeof buf, 0);
    if (n <= 0) break;
    sb_addn(&raw, buf, (size_t)n);
  }
done:
  if (s != INVALID_SOCKET) close_socket(s);
  parse_reply(&raw, r);
}

/* a worker takes only its lane's requests; the screens' data, when its own lane is idle, can take
   a picture's, never the other way round */
static job *next_job(int lane) {
  job *j = pop(&queue[lane]);
  if (!j && lane == NET_HIGH) j = pop(&queue[NET_LOW]);
  return j;
}

static int worker(void *arg) {
  int lane = (int)(intptr_t)arg;
  for (;;) {
    SDL_LockMutex(lock);
    job *j = NULL;
    while (!stopping && !(j = next_job(lane))) SDL_CondWait(wake, lock);
    SDL_UnlockMutex(lock);
    if (!j) return 0;
    fetch(j->path, j->body, &j->reply);
    if (j->work) j->work(&j->reply, j->user);
    SDL_LockMutex(lock);
    push(&finished, j);
    SDL_UnlockMutex(lock);
  }
}

int net_init(int server_port, int nthreads) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa)) return -1;
#endif
  port = server_port;
  lock = SDL_CreateMutex();
  wake = SDL_CreateCond();
  (void)nthreads;
  static const char *names[3] = {"net-data", "net-pictures", "net-slow"};
  for (int lane = 0; lane < 3; lane++)
    for (int i = 0; i < LANE_WORKERS[lane]; i++)
      workers[nworkers++] = SDL_CreateThread(worker, names[lane], (void *)(intptr_t)lane);
  return 0;
}

static void free_job(job *j) {
  free(j->path);
  free(j->body);
  free(j->reply.body);
  free(j);
}

void net_shutdown(void) {
  SDL_LockMutex(lock);
  stopping = 1;
  SDL_CondBroadcast(wake);
  SDL_UnlockMutex(lock);
  for (int i = 0; i < nworkers; i++) SDL_WaitThread(workers[i], NULL);
  job *j;
  for (int q = 0; q < 3; q++)
    while ((j = pop(&queue[q]))) free_job(j);
  while ((j = pop(&finished))) free_job(j);
}

void net_request(const char *path, const char *body, int priority, net_work_fn work, net_done_fn done, void *user) {
  job *j = calloc(1, sizeof *j);
  j->path = str_dup(path);
  j->body = body ? str_dup(body) : NULL;
  j->work = work;
  j->done = done;
  j->user = user;
  SDL_LockMutex(lock);
  push(&queue[priority == NET_LOW || priority == NET_SLOW ? priority : NET_HIGH], j);
  pending++;
  /* every worker wakes: each takes only what its lane (net.h) allows */
  SDL_CondBroadcast(wake);
  SDL_UnlockMutex(lock);
}

void net_fetch(const char *path, const char *body, net_reply *r) {
  fetch(path, body, r);
  r->result = NULL;
}

void net_poll(void) {
  SDL_LockMutex(lock);
  job *list = finished;
  finished = NULL;
  SDL_UnlockMutex(lock);
  while (list) {
    job *j = pop(&list);
    if (j->done) j->done(&j->reply, j->user);
    pending--;
    free_job(j);
  }
}

int net_pending(void) { return pending; }
