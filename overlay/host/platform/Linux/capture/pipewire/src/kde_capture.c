/* SPDX-License-Identifier: GPL-2.0-or-later */
/* KDE-specific native desktop integration. The compositor authorizes this
 * executable using its own per-application Wayland capability policy. Never
 * disable KWin permission checks. GNOME/other desktops keep the portal path. */
#include "kde_capture.h"
#include "zkde-screencast-client.h"
#include "common/debug.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

struct Output
{
  struct wl_output * object;
  uint32_t global;
  char name[256], description[512];
  int width, height, refresh;
};
struct KDECapture
{
  struct wl_display * display;
  struct wl_registry * registry;
  struct zkde_screencast_unstable_v1 * manager;
  struct zkde_screencast_stream_unstable_v1 * stream;
  struct Output outputs[32];
  unsigned count;
  uint32_t selected, node;
  bool alive;
};
static uint64_t nowMs(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
/* Single-threaded owner. prepare_read handles pending events without races;
 * every blocking wait has a deadline, including registry enumeration. */
static int dispatch(struct KDECapture * c, int timeout)
{
  while (wl_display_prepare_read(c->display) != 0)
    if (wl_display_dispatch_pending(c->display) < 0) return -1;
  int flush = wl_display_flush(c->display);
  if (flush < 0 && errno != EAGAIN) { wl_display_cancel_read(c->display); return -1; }
  struct pollfd fd = {.fd = wl_display_get_fd(c->display),
    .events = POLLIN | (flush < 0 ? POLLOUT : 0)};
  int status = poll(&fd, 1, timeout);
  if (status <= 0 || !(fd.revents & POLLIN))
  {
    wl_display_cancel_read(c->display);
    if (status < 0 && errno == EINTR) return 0;
    if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
    return status;
  }
  if (wl_display_read_events(c->display) < 0 ||
      wl_display_dispatch_pending(c->display) < 0) return -1;
  return 1;
}
static void synced(void * data, struct wl_callback * callback, uint32_t serial)
{
  *(bool *)data = true;
}
static const struct wl_callback_listener syncListener = {.done = synced};
static bool roundTrip(struct KDECapture * c)
{
  bool done = false;
  struct wl_callback * sync = wl_display_sync(c->display);
  if (!sync) return false;
  wl_callback_add_listener(sync, &syncListener, &done);
  uint64_t end = nowMs() + 5000;
  while (!done)
  {
    int left = (int)(end - nowMs());
    if (left <= 0 || dispatch(c, left) < 0) break;
  }
  wl_callback_destroy(sync);
  return done;
}
static void geometry(void * data, struct wl_output * output, int32_t x,
  int32_t y, int32_t width, int32_t height, int32_t subpixel,
  const char * make, const char * model, int32_t transform) {}
static void mode(void * data, struct wl_output * output, uint32_t flags,
  int32_t width, int32_t height, int32_t refresh)
{
  struct Output * o = data;
  if (flags & WL_OUTPUT_MODE_CURRENT)
    { o->width = width; o->height = height; o->refresh = refresh; }
}
static void outputDone(void * data, struct wl_output * output) {}
static void scale(void * data, struct wl_output * output, int32_t factor) {}
static void outputName(void * data, struct wl_output * output, const char * name)
{
  snprintf(((struct Output *)data)->name, sizeof(((struct Output *)data)->name), "%s", name);
}
static void description(void * data, struct wl_output * output, const char * text)
{
  struct Output * o = data;
  snprintf(o->description, sizeof(o->description), "%s", text);
  for (char * p = o->description; *p; ++p) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
}
static const struct wl_output_listener outputListener = {
  .geometry = geometry, .mode = mode, .done = outputDone, .scale = scale,
  .name = outputName, .description = description
};
static void global(void * data, struct wl_registry * registry, uint32_t name,
  const char * interface, uint32_t version)
{
  struct KDECapture * c = data;
  if (!strcmp(interface, "wl_output") && version >= 4 && c->count < 32)
  {
    struct Output * o = &c->outputs[c->count++];
    o->global = name;
    o->object = wl_registry_bind(registry, name, &wl_output_interface, 4);
    wl_output_add_listener(o->object, &outputListener, o);
  }
  else if (!strcmp(interface, "zkde_screencast_unstable_v1"))
    c->manager = wl_registry_bind(registry, name, &zkde_screencast_unstable_v1_interface, 1);
}
static void removed(void * data, struct wl_registry * registry, uint32_t name)
{
  struct KDECapture * c = data;
  if (c->selected == name) c->alive = false;
  for (unsigned i = 0; i < c->count; ++i)
    if (c->outputs[i].global == name) c->outputs[i].name[0] = '\0';
}
static const struct wl_registry_listener registryListener = {.global = global, .global_remove = removed};
static void closed(void * data, struct zkde_screencast_stream_unstable_v1 * stream)
{
  ((struct KDECapture *)data)->alive = false;
  DEBUG_WARN("KWin closed the selected output stream");
}
static void created(void * data, struct zkde_screencast_stream_unstable_v1 * stream, uint32_t node)
{
  ((struct KDECapture *)data)->node = node;
}
static void failed(void * data, struct zkde_screencast_stream_unstable_v1 * stream, const char * error)
{
  ((struct KDECapture *)data)->alive = false;
  DEBUG_ERROR("KWin capture failed: %s", error);
}
static const struct zkde_screencast_stream_unstable_v1_listener streamListener = {
  .closed = closed, .created = created, .failed = failed
};
static struct KDECapture * connectDisplay(void)
{
  struct KDECapture * c = calloc(1, sizeof(*c));
  if (!c) return NULL;
  c->alive = true;
  c->display = wl_display_connect(NULL);
  if (!c->display) goto fail;
  c->registry = wl_display_get_registry(c->display);
  wl_registry_add_listener(c->registry, &registryListener, c);
  if (!roundTrip(c) || !roundTrip(c)) goto fail;
  if (!c->manager)
  {
    DEBUG_ERROR("KWin native capture interface unavailable. Re-run install.sh to register this executable's KDE capability; do not disable compositor permission checks.");
    goto fail;
  }
  return c;
fail:
  DEBUG_ERROR("Cannot enumerate native KDE outputs in this Wayland session");
  lgKdeCapture_close(c);
  return NULL;
}
struct KDECapture * lgKdeCapture_open(const char * connector)
{
  struct KDECapture * c = connectDisplay();
  if (!c) return NULL;
  for (unsigned i = 0; i < c->count; ++i)
  {
    struct Output * o = &c->outputs[i];
    if (strcmp(connector, o->name)) continue;
    c->selected = o->global;
    c->stream = zkde_screencast_unstable_v1_stream_output(c->manager,
      o->object, ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_EMBEDDED);
    zkde_screencast_stream_unstable_v1_add_listener(c->stream, &streamListener, c);
    uint64_t end = nowMs() + 15000;
    while (!c->node && c->alive)
    {
      int left = (int)(end - nowMs());
      if (left <= 0 || dispatch(c, left) < 0) break;
    }
    if (c->node && c->alive)
    {
      DEBUG_INFO("KDE native monitor: %s, %dx%d @ %.3f Hz, PipeWire node %u", connector,
        o->width, o->height, o->refresh / 1000.0, c->node);
      return c;
    }
    break;
  }
  DEBUG_ERROR("Cannot capture the saved KDE connector %s; enable it in Display Configuration or choose again with install.sh", connector);
  lgKdeCapture_close(c);
  return NULL;
}
uint32_t lgKdeCapture_node(struct KDECapture * c) { return c->node; }
unsigned lgKdeCapture_refresh(struct KDECapture * c)
{
  for (unsigned i = 0; i < c->count; ++i)
    if (c->outputs[i].global == c->selected && c->outputs[i].refresh > 0)
      return (unsigned)c->outputs[i].refresh;
  return 0;
}
bool lgKdeCapture_alive(struct KDECapture * c)
{
  if (dispatch(c, 0) < 0) c->alive = false;
  return c->alive;
}
void lgKdeCapture_close(struct KDECapture * c)
{
  if (!c) return;
  if (c->stream) zkde_screencast_stream_unstable_v1_close(c->stream);
  if (c->manager) zkde_screencast_unstable_v1_destroy(c->manager);
  for (unsigned i = 0; i < c->count; ++i)
    if (c->outputs[i].object) wl_output_release(c->outputs[i].object);
  if (c->registry) wl_registry_destroy(c->registry);
  if (c->display) { wl_display_flush(c->display); wl_display_disconnect(c->display); }
  free(c);
}
int lgKdeCapture_list(void)
{
  struct KDECapture * c = connectDisplay();
  if (!c) return 1;
  unsigned count = 0;
  for (unsigned i = 0; i < c->count; ++i)
  {
    struct Output * o = &c->outputs[i];
    if (!o->name[0]) continue;
    printf("%s\t%dx%d\t%.3f Hz\t%s\n", o->name, o->width, o->height,
      o->refresh / 1000.0, o->description);
    ++count;
  }
  lgKdeCapture_close(c);
  return count ? 0 : 1;
}
