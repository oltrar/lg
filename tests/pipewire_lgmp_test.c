/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real PipeWire daemon -> real guest executable -> B7 LGMP client. Only the
 * portal's consent UI and compositor producer are fixtures. No QXL exists. */
#include "portal_test_server.h"
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/buffers.h>
#include <signal.h>
#include <sys/wait.h>
#include "kde_test_server.h"

#define WIDTH 320
#define HEIGHT 240
#define STRIDE (WIDTH * 4 + 16)
struct Producer
{
  struct pw_thread_loop * loop;
  struct pw_context * context;
  struct pw_core * core;
  struct pw_stream * stream;
  struct pw_registry * registry;
  struct pw_proxy * link;
  struct spa_hook streamListener, registryListener;
  struct spa_source * timer;
  uint32_t node;
};
static void produce(void * opaque)
{
  struct Producer * p = opaque;
  struct pw_buffer * b = pw_stream_dequeue_buffer(p->stream);
  if (!b) return;
  struct spa_data * data = &b->buffer->datas[0];
  assert(data->data && data->maxsize >= STRIDE * HEIGHT);
  memset(data->data, 0xcd, data->maxsize);
  for (unsigned y = 0; y < HEIGHT; ++y)
    for (unsigned x = 0; x < WIDTH * 4; ++x)
      ((uint8_t *)data->data)[y * STRIDE + x] = (x + y * 7) & 255;
  *data->chunk = (struct spa_chunk){.offset = 0, .size = STRIDE * HEIGHT, .stride = STRIDE};
  pw_stream_queue_buffer(p->stream, b);
}
static void tick(void * opaque, uint64_t count)
{
  pw_stream_trigger_process(((struct Producer *)opaque)->stream);
}
static void producerState(void * opaque, enum pw_stream_state old,
  enum pw_stream_state state, const char * error)
{
  struct Producer * p = opaque;
  fprintf(stderr, "Test producer: %s -> %s (%s)\n", pw_stream_state_as_string(old),
    pw_stream_state_as_string(state), error ? error : "ok");
  assert(state != PW_STREAM_STATE_ERROR);
  if (state == PW_STREAM_STATE_PAUSED) p->node = pw_stream_get_node_id(p->stream);
  struct timespec first = {.tv_nsec = 1}, interval = {.tv_nsec = 40000000};
  pw_loop_update_timer(pw_thread_loop_get_loop(p->loop), p->timer,
    state == PW_STREAM_STATE_STREAMING ? &first : NULL,
    state == PW_STREAM_STATE_STREAMING ? &interval : NULL, false);
  pw_thread_loop_signal(p->loop, false);
}
static void producerFormat(void * opaque, uint32_t id, const struct spa_pod * param)
{
  if (id != SPA_PARAM_Format || !param) return;
  struct Producer * p = opaque;
  uint8_t bytes[512];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
  const struct spa_pod * buffers = spa_pod_builder_add_object(&builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 2, 16),
    SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
    SPA_PARAM_BUFFERS_size, SPA_POD_Int(STRIDE * HEIGHT),
    SPA_PARAM_BUFFERS_stride, SPA_POD_Int(STRIDE),
    SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_MemFd));
  assert(pw_stream_update_params(p->stream, &buffers, 1) >= 0);
}
static const struct pw_stream_events producerEvents = {
  PW_VERSION_STREAM_EVENTS, .state_changed = producerState,
  .param_changed = producerFormat, .process = produce
};
static void global(void * opaque, uint32_t id, uint32_t permissions,
  const char * type, uint32_t version, const struct spa_dict * props)
{
  struct Producer * p = opaque;
  if (strcmp(type, PW_TYPE_INTERFACE_Node) || !props) return;
  const char * name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  fprintf(stderr, "Test registry node %u: %s\n", id, name ? name : "unnamed");
  if (!name || strcmp(name, "looking-glass-capture")) return;
  char output[32], input[32];
  snprintf(output, sizeof(output), "%u", p->node);
  snprintf(input, sizeof(input), "%u", id);
  struct pw_properties * settings = pw_properties_new(
    PW_KEY_LINK_OUTPUT_NODE, output, PW_KEY_LINK_INPUT_NODE, input, NULL);
  p->link = pw_core_create_object(p->core, "link-factory", PW_TYPE_INTERFACE_Link,
    PW_VERSION_LINK, &settings->dict, 0);
  assert(p->link);
  pw_properties_free(settings);
}
static const struct pw_registry_events registryEvents = {
  PW_VERSION_REGISTRY_EVENTS, .global = global
};
static void startProducer(struct Producer * p)
{
  pw_init(NULL, NULL);
  p->node = PW_ID_ANY;
  p->loop = pw_thread_loop_new("test-producer", NULL);
  assert(p->loop);
  p->context = pw_context_new(pw_thread_loop_get_loop(p->loop), NULL, 0);
  p->core = pw_context_connect(p->context, NULL, 0);
  assert(p->context && p->core);
  p->timer = pw_loop_add_timer(pw_thread_loop_get_loop(p->loop), tick, p);
  p->stream = pw_stream_new(p->core, "fixture-nvidia-output",
    pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", NULL));
  assert(p->stream && p->timer);
  pw_stream_add_listener(p->stream, &p->streamListener, &producerEvents, p);
  uint8_t bytes[512];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
  const struct spa_pod * format = spa_pod_builder_add_object(&builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
    SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRA),
    SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&SPA_RECTANGLE(WIDTH, HEIGHT)),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)),
    SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_Fraction(&SPA_FRACTION(25, 1)));
  assert(pw_stream_connect(p->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
    PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS, &format, 1) >= 0);
  p->registry = pw_core_get_registry(p->core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(p->registry, &p->registryListener, &registryEvents, p);
  assert(pw_thread_loop_start(p->loop) >= 0);
  pw_thread_loop_lock(p->loop);
  struct timespec deadline;
  pw_thread_loop_get_time(p->loop, &deadline, 5 * SPA_NSEC_PER_SEC);
  while (p->node == PW_ID_ANY)
    assert(pw_thread_loop_timed_wait_full(p->loop, &deadline) >= 0);
  pw_thread_loop_unlock(p->loop);
}
int main(int argc, char ** argv)
{
  assert(argc == 5 || argc == 6); /* optional native KDE path */
  bool native = argc == 6;
  g_test_init(&argc, &argv, NULL);
  g_autoptr(GTestDBus) bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(bus);
  /* GTestDBus deliberately clears XDG_RUNTIME_DIR along with display env. */
  g_setenv("XDG_RUNTIME_DIR", argv[4], TRUE);
  g_autofree char * socketPath = g_build_filename(argv[4], "pipewire-0", NULL);
  struct Producer producer = {0};
  startProducer(&producer);
  struct Server server = {.address = g_test_dbus_get_bus_address(bus),
    .pipewireSocket = socketPath, .node = producer.node};
  g_mutex_init(&server.mutex); g_cond_init(&server.condition);
  GThread * thread = g_thread_new("portal-fixture", serve, &server);
  struct KdeServer kdeServer = {.node = producer.node, .refresh = 165000};
  if (native) startKdeServer(&kdeServer, false);
  g_mutex_lock(&server.mutex);
  gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
  while (!server.ready) assert(g_cond_wait_until(&server.condition, &server.mutex, deadline));
  g_mutex_unlock(&server.mutex);
  g_autofree char * shmOption = g_strdup_printf("app:shmFile=%s", argv[3]);
  char * command[] = {argv[1], shmOption, "app:capture=pipewire", "pipewire:restore=no",
    native ? "pipewire:kdeOutput=HDMI-A-1" : "pipewire:kdeOutput=", NULL};
  GPid host;
  assert(g_spawn_async(NULL, command, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
    NULL, NULL, &host, NULL));
  /* Exercise the real B7 initial no-subscriber idle transition. */
  g_usleep(2 * G_TIME_SPAN_SECOND);
  assert(kill(host, 0) == 0);
  for (unsigned attempt = 0; attempt < 3; ++attempt)
  {
    g_autofree char * rawPath = g_strdup_printf("%s/frame-%u.raw", argv[4], attempt);
    char * check[] = {argv[2], argv[3], "15", rawPath, NULL};
    int status;
    assert(g_spawn_sync(NULL, check, NULL, 0, NULL, NULL, NULL, NULL, &status, NULL));
    if (!g_spawn_check_wait_status(status, NULL))
    {
      int hostStatus;
      if (waitpid(host, &hostStatus, WNOHANG) == host)
        fprintf(stderr, "Guest host exited: signal=%d exit=%d\n",
          WIFSIGNALED(hostStatus) ? WTERMSIG(hostStatus) : 0,
          WIFEXITED(hostStatus) ? WEXITSTATUS(hostStatus) : -1);
      assert(!"frame verification failed");
    }
    g_autofree char * raw = NULL;
    gsize length;
    assert(g_file_get_contents(rawPath, &raw, &length, NULL) && length == WIDTH * HEIGHT * 4);
    for (unsigned y = 0; y < HEIGHT; ++y)
      for (unsigned x = 0; x < WIDTH * 4; ++x)
        assert((uint8_t)raw[(y * WIDTH * 4) + x] == ((x + y * 7) & 255));
    g_usleep(500000); /* last client leaves -> pause -> next client resumes */
    assert(g_atomic_int_get(&server.createCount) == (native ? 0 : 1));
    if (native) assert(atomic_load(&kdeServer.opened) == 1 && atomic_load(&kdeServer.capturedHdmi));
    assert(g_atomic_int_get(&server.closeCount) == 0);
  }
  kill(host, SIGINT);
  int status;
  assert(waitpid(host, &status, 0) == host && WIFEXITED(status) && WEXITSTATUS(status) == 0x10);
  g_spawn_close_pid(host);
  assert(g_atomic_int_get(&server.closeCount) == (native ? 0 : 1));
  if (native) stopKdeServer(&kdeServer);
  g_main_context_invoke(server.context, stop, &server); g_thread_join(thread);
  g_cond_clear(&server.condition); g_mutex_clear(&server.mutex);
  pw_thread_loop_stop(producer.loop);
  if (producer.link) pw_proxy_destroy(producer.link);
  pw_proxy_destroy((struct pw_proxy *)producer.registry);
  pw_stream_destroy(producer.stream); pw_core_disconnect(producer.core);
  pw_context_destroy(producer.context); pw_thread_loop_destroy(producer.loop);
  pw_deinit();
  g_test_dbus_down(bus);
  puts("PASS: real PipeWire MemFd -> guest host -> complete B7 LGMP pixels, initial idle and three reconnects, one monitor session, no QXL");
  return 0;
}
