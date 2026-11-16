#include "webos_luna.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Two handles on the bus: the app's own name for its calls (starting its server, the screen saver),
   which the app's role permits; and, for the media service only, a name of the form
   com.webos.media.client.*, which that service answers (without it some webOS 4 systems take a load
   and never reply). Taking the media name for everything cost the app the right to call its own
   service. */
static LSHandle *bus, *media;
static int media_tried;
static char error[256], media_name[200];

const char *luna_error(void) { return error; }
const char *luna_name(void) { return media_name[0] ? media_name : "the app's own"; }

static LSHandle *attach(const char *name) {
  LSHandle *h = NULL;
  LSError err;
  LSErrorInit(&err);
  if (!LSRegister(name, &h, &err)) {
    snprintf(error, sizeof error, "Luna: %s", err.message ? err.message : "no bus");
    LSErrorFree(&err);
    return NULL;
  }
  LSErrorInit(&err);
  if (!LSGmainContextAttach(h, g_main_context_default(), &err)) {
    snprintf(error, sizeof error, "Luna: %s", err.message ? err.message : "no main loop");
    LSErrorFree(&err);
    return NULL;
  }
  return h;
}

/* the app's own name; an unnamed handle on systems that refuse it */
int luna_ready(void) {
  if (!bus) bus = attach(SC_APP_ID);
  if (!bus) bus = attach(NULL);
  return bus != NULL;
}

/* the media name, then the same with the process number (a name still held by a run that did not
   close cleanly), else the app's handle */
static LSHandle *media_handle(void) {
  if (!media_tried) {
    media_tried = 1;
    char name[200];
    snprintf(name, sizeof name, "com.webos.media.client.%s", SC_APP_ID);
    media = attach(name);
    if (!media) {
      snprintf(name, sizeof name, "com.webos.media.client.%s.%d", SC_APP_ID, (int)getpid());
      media = attach(name);
    }
    if (media) snprintf(media_name, sizeof media_name, "%s", name);
  }
  return media ? media : bus;
}

static LSHandle *handle_for(const char *uri) {
  return strncmp(uri, "luna://com.webos.media/", 23) ? bus : media_handle();
}

int luna_call(const char *uri, const char *payload, LSFilterFunc reply, LSMessageToken *token) {
  LSError err;
  if (!luna_ready()) return 0;
  LSHandle *h = handle_for(uri);
  LSErrorInit(&err);
  int ok = reply ? LSCall(h, uri, payload, reply, NULL, token, &err)
                 : LSCallOneReply(h, uri, payload, NULL, NULL, NULL, &err);
  if (!ok) {
    snprintf(error, sizeof error, "%s: %s", uri, err.message ? err.message : "refused");
    LSErrorFree(&err);
  }
  return ok;
}

/* a call is cancelled on the handle that made it: the media one first, then the app's */
void luna_cancel(LSMessageToken token) {
  LSHandle *handles[2] = {media, bus};
  for (int i = 0; i < 2 && token; i++) {
    LSError err;
    LSErrorInit(&err);
    if (handles[i] && LSCallCancel(handles[i], token, &err)) return;
    LSErrorFree(&err);
  }
}

void luna_poll(void) {
  while (g_main_context_iteration(NULL, FALSE)) {
  }
}
