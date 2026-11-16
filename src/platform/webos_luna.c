#include "webos_luna.h"

#include <glib.h>
#include <stdio.h>
#include <unistd.h>

static LSHandle *bus;
static char error[256], name[480];

const char *luna_error(void) { return error; }
const char *luna_name(void) { return name[0] ? name : "unnamed"; }

int luna_ready(void) {
  if (bus) return 1;
  LSError err;
  /* The media service answers clients named com.webos.media.client.*, which a native app's role
     allows; without that name some webOS 4 systems take a load and never reply. Then the app's own
     name, then an unnamed handle on systems that refuse both. A name still held by a run that did
     not close cleanly is tried again with the process number after it. */
  char names[3][160];
  snprintf(names[0], sizeof names[0], "com.webos.media.client.%s", SC_APP_ID);
  snprintf(names[1], sizeof names[1], "com.webos.media.client.%s.%d", SC_APP_ID, (int)getpid());
  snprintf(names[2], sizeof names[2], "%s", SC_APP_ID);
  for (int i = 0; i < 4 && !bus; i++) {
    LSErrorInit(&err);
    if (LSRegister(i < 3 ? names[i] : NULL, &bus, &err)) snprintf(name, sizeof name, "%s", i < 3 ? names[i] : "");
    else {
      snprintf(error, sizeof error, "Luna: %s", err.message ? err.message : "no bus");
      bus = NULL;
    }
    LSErrorFree(&err);
  }
  if (!bus) return 0;
  LSErrorInit(&err);
  if (!LSGmainContextAttach(bus, g_main_context_default(), &err)) {
    snprintf(error, sizeof error, "Luna: %s", err.message ? err.message : "no main loop");
    LSErrorFree(&err);
    return 0;
  }
  return 1;
}

int luna_call(const char *uri, const char *payload, LSFilterFunc reply, LSMessageToken *token) {
  LSError err;
  if (!luna_ready()) return 0;
  LSErrorInit(&err);
  int ok = reply ? LSCall(bus, uri, payload, reply, NULL, token, &err)
                 : LSCallOneReply(bus, uri, payload, NULL, NULL, NULL, &err);
  if (!ok) {
    snprintf(error, sizeof error, "%s: %s", uri, err.message ? err.message : "refused");
    LSErrorFree(&err);
  }
  return ok;
}

void luna_cancel(LSMessageToken token) {
  LSError err;
  LSErrorInit(&err);
  if (bus && token && !LSCallCancel(bus, token, &err)) LSErrorFree(&err);
}

void luna_poll(void) {
  while (g_main_context_iteration(NULL, FALSE)) {
  }
}
