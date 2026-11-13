/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "zkde-screencast-server.h"
#include <wayland-server.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
struct KdeServer;
struct FakeOutput { struct KdeServer * server; const char * name; int x; };
struct KdeServer
{
  struct wl_display * display;
  struct FakeOutput outputs[2];
  pthread_t thread;
  atomic_bool stop, capturedHdmi;
  atomic_int opened, closed;
  uint32_t node;
  int refresh;
};
static void destroyResource(struct wl_client * client, struct wl_resource * resource)
{
  wl_resource_destroy(resource);
}
static const struct wl_output_interface outputImpl = {.release = destroyResource};
static void bindOutput(struct wl_client * client, void * data, uint32_t version, uint32_t id)
{
  struct FakeOutput * output = data;
  struct wl_resource * resource = wl_resource_create(client, &wl_output_interface, 4, id);
  wl_resource_set_implementation(resource, &outputImpl, output, NULL);
  wl_output_send_geometry(resource, output->x, 0, 300, 200, WL_OUTPUT_SUBPIXEL_UNKNOWN,
    "Fixture", output->name, WL_OUTPUT_TRANSFORM_NORMAL);
  wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, 320, 240,
    output->server->refresh ? output->server->refresh : 60000);
  wl_output_send_scale(resource, 1);
  wl_output_send_name(resource, output->name);
  wl_output_send_description(resource, output->name);
  wl_output_send_done(resource);
}
static const struct zkde_screencast_stream_unstable_v1_interface streamImpl = {.close = destroyResource};
static void streamDestroyed(struct wl_resource * resource)
{
  struct KdeServer * s = wl_resource_get_user_data(resource);
  atomic_fetch_add(&s->closed, 1);
}
static void streamOutput(struct wl_client * client, struct wl_resource * resource,
  uint32_t id, struct wl_resource * outputResource, uint32_t cursor)
{
  struct KdeServer * s = wl_resource_get_user_data(resource);
  struct FakeOutput * output = wl_resource_get_user_data(outputResource);
  assert(cursor == ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_EMBEDDED);
  atomic_store(&s->capturedHdmi, !strcmp(output->name, "HDMI-A-1"));
  atomic_fetch_add(&s->opened, 1);
  struct wl_resource * stream = wl_resource_create(client, &zkde_screencast_stream_unstable_v1_interface, 1, id);
  wl_resource_set_implementation(stream, &streamImpl, s, streamDestroyed);
  zkde_screencast_stream_unstable_v1_send_created(stream, s->node);
}
static const struct zkde_screencast_unstable_v1_interface managerImpl = {
  .stream_output = streamOutput, .destroy = destroyResource
};
static void bindManager(struct wl_client * client, void * data, uint32_t version, uint32_t id)
{
  struct wl_resource * resource = wl_resource_create(client, &zkde_screencast_unstable_v1_interface, 1, id);
  wl_resource_set_implementation(resource, &managerImpl, data, NULL);
}
static void * serverThread(void * data)
{
  struct KdeServer * s = data;
  while (!atomic_load(&s->stop))
  {
    assert(wl_event_loop_dispatch(wl_display_get_event_loop(s->display), 10) >= 0);
    wl_display_flush_clients(s->display);
  }
  wl_display_destroy_clients(s->display);
  return NULL;
}
static void startKdeServer(struct KdeServer * s, bool qxl)
{
  s->display = wl_display_create();
  assert(s->display);
  const char * socketName = wl_display_add_socket_auto(s->display);
  assert(socketName && setenv("WAYLAND_DISPLAY", socketName, 1) == 0);
  s->outputs[0] = (struct FakeOutput){.server = s, .name = "HDMI-A-1", .x = qxl ? 1920 : 0};
  assert(wl_global_create(s->display, &wl_output_interface, 4, &s->outputs[0], bindOutput));
  if (qxl)
  {
    s->outputs[1] = (struct FakeOutput){.server = s, .name = "Virtual-1", .x = 0};
    assert(wl_global_create(s->display, &wl_output_interface, 4, &s->outputs[1], bindOutput));
  }
  assert(wl_global_create(s->display, &zkde_screencast_unstable_v1_interface, 1, s, bindManager));
  assert(pthread_create(&s->thread, NULL, serverThread, s) == 0);
}
static void stopKdeServer(struct KdeServer * s)
{
  atomic_store(&s->stop, true);
  assert(pthread_join(s->thread, NULL) == 0);
  wl_display_destroy(s->display);
}
