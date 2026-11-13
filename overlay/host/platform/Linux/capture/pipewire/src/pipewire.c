/**
 * Looking Glass
 * Copyright (C) 2017-2025 The Looking Glass Authors
 * Copyright (C) 2026 Linux Wayland integration contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * B7 PipeWire adaptation: raw SDR pixels over the unchanged B7 LGMP transport.
 * An owned snapshot separates producer lifetime from shared-memory publication.
 */
#include "portal.h"
#include "frame_layout.h"
#include "interface/capture.h"
#include "common/debug.h"
#include "common/option.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/pod/builder.h>
#include <spa/param/video/format-utils.h>

struct pipewire
{
  struct Portal * portal;
  char * sessionHandle;
  struct pw_thread_loop * loop;
  struct pw_context * context;
  struct pw_core * core;
  struct pw_stream * stream;
  struct spa_hook coreListener, streamListener;
  struct pw_buffer * pending;
  /* Live state is protected by the PipeWire loop lock. */
  bool started, stop, failed, hasFormat, connected, warnedBuffer;
  unsigned width, height, formatVer;
  CaptureFormat format;
  /* Only the Looking Glass capture thread touches the snapshot. */
  uint8_t * snapshot;
  size_t capacity;
  unsigned readyWidth, readyHeight, readyVersion, dataHeight;
  CaptureFormat readyFormat;
};
static struct pipewire * this;
static bool pipewire_deinit(void);

static bool fpsValidator(struct Option * opt, const char ** error)
{
  if (opt->value.x_int >= 1 && opt->value.x_int <= 360)
    return true;
  *error = "PipeWire fps must be between 1 and 360";
  return false;
}
static void pipewire_initOptions(void)
{
  struct Option options[] = {
    {.module = "pipewire", .name = "fps",
     .description = "Requested maximum capture rate (compositor may limit it)",
     .type = OPTION_TYPE_INT, .value.x_int = 60, .validator = fpsValidator},
    {.module = "pipewire", .name = "restore",
     .description = "Reuse the saved monitor selection (no requests a fresh chooser)",
     .type = OPTION_TYPE_BOOL, .value.x_bool = true}, {0}
  };
  option_register(options);
}
static const char * pipewire_getName(void)
{
  return "PipeWire (B7 Wayland integration, SDR)";
}
static bool pipewire_create(CaptureGetPointerBuffer getPointerBufferFn,
  CapturePostPointerBuffer postPointerBufferFn, unsigned frameBuffers)
{
  pw_init(NULL, NULL);
  this = calloc(1, sizeof(*this));
  return this != NULL;
}
static void discardPending(void)
{
  if (this->pending)
  {
    pw_stream_queue_buffer(this->stream, this->pending);
    this->pending = NULL;
  }
}
static void coreError(void * opaque, uint32_t id, int seq, int res,
  const char * message)
{
  DEBUG_ERROR("PipeWire core error %d: %s", res, message);
  this->failed = true;
  pw_thread_loop_signal(this->loop, false);
}
static const struct pw_core_events coreEvents = {
  PW_VERSION_CORE_EVENTS, .error = coreError
};
static void streamProcess(void * opaque)
{
  /* RT_PROCESS is not enabled: callbacks run with the loop lock held. */
  struct pw_buffer * buffer;
  while ((buffer = pw_stream_dequeue_buffer(this->stream)))
  {
    discardPending();
    this->pending = buffer;
  }
  pw_thread_loop_signal(this->loop, false);
}
static void streamRemoveBuffer(void * opaque, struct pw_buffer * buffer)
{
  if (this->pending == buffer)
    this->pending = NULL;
}
static void streamState(void * opaque, enum pw_stream_state oldState,
  enum pw_stream_state state, const char * error)
{
  DEBUG_INFO("PipeWire: %s -> %s", pw_stream_state_as_string(oldState),
    pw_stream_state_as_string(state));
  if (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING)
    this->connected = true;
  if (state == PW_STREAM_STATE_ERROR ||
      (state == PW_STREAM_STATE_UNCONNECTED && this->connected && !this->stop))
  {
    DEBUG_ERROR("PipeWire stream closed: %s", error ? error : "session ended");
    this->failed = true;
  }
  pw_thread_loop_signal(this->loop, false);
}
static void streamFormat(void * opaque, uint32_t id, const struct spa_pod * param)
{
  if (id != SPA_PARAM_Format)
    return;
  discardPending();
  this->hasFormat = false;
  if (!param)
    return;
  struct spa_video_info_raw info = {0};
  if (spa_format_video_raw_parse(param, &info) < 0 ||
      !info.size.width || !info.size.height ||
      info.size.width > 8192 || info.size.height > 8192)
    goto unsupported;
  switch (info.format)
  {
    case SPA_VIDEO_FORMAT_BGRA:
    case SPA_VIDEO_FORMAT_BGRx: this->format = CAPTURE_FMT_BGRA; break;
    case SPA_VIDEO_FORMAT_RGBA:
    case SPA_VIDEO_FORMAT_RGBx: this->format = CAPTURE_FMT_RGBA; break;
    default: goto unsupported;
  }
  this->width = info.size.width;
  this->height = info.size.height;
  ++this->formatVer;
  this->hasFormat = true;
  uint8_t storage[512];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
  const struct spa_pod * buffers = spa_pod_builder_add_object(&builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_dataType,
      SPA_POD_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)));
  if (pw_stream_update_params(this->stream, &buffers, 1) < 0)
    goto unsupported;
  DEBUG_INFO("Raw SDR capture: %ux%u, format %u", this->width,
    this->height, info.format);
  pw_thread_loop_signal(this->loop, false);
  return;
unsupported:
  DEBUG_ERROR("Unsupported PipeWire format or geometry; use an SDR output");
  this->failed = true;
  pw_thread_loop_signal(this->loop, false);
}
static const struct pw_stream_events streamEvents = {
  PW_VERSION_STREAM_EVENTS, .state_changed = streamState,
  .param_changed = streamFormat, .process = streamProcess,
  .remove_buffer = streamRemoveBuffer
};
static bool pipewire_init(void * ivshmemBase, unsigned * alignSize)
{
  const char * stage = "opening the desktop portal";
  this->stop = this->failed = this->hasFormat = this->connected = false;
  this->warnedBuffer = false;
  this->readyWidth = this->readyHeight = 0;
  this->portal = portal_create();
  if (!this->portal)
    goto fail;
  stage = "ScreenCast.CreateSession";
  if (!portal_createScreenCastSession(this->portal, &this->sessionHandle))
    goto fail;
  stage = "ScreenCast.SelectSources";
  if (!portal_selectSource(this->portal, this->sessionHandle,
        option_get_bool("pipewire", "restore")))
    goto fail;
  stage = "ScreenCast.Start / monitor selection";
  uint32_t node = portal_getPipewireNode(this->portal, this->sessionHandle);
  if (!node)
    goto fail;
  stage = "ScreenCast.OpenPipeWireRemote";
  int fd = portal_openPipewireRemote(this->portal, this->sessionHandle);
  if (fd < 0)
    goto fail;
  stage = "creating the PipeWire thread loop";
  this->loop = pw_thread_loop_new("looking-glass-capture", NULL);
  if (!this->loop)
  {
    DEBUG_ERROR("pw_thread_loop_new: %s", strerror(errno));
    close(fd);
    goto fail;
  }
  stage = "creating the PipeWire context";
  this->context = pw_context_new(pw_thread_loop_get_loop(this->loop), NULL, 0);
  if (!this->context)
  {
    DEBUG_ERROR("pw_context_new: %s", strerror(errno));
    close(fd);
    goto fail;
  }
  stage = "starting the PipeWire thread loop";
  int status = pw_thread_loop_start(this->loop);
  if (status < 0)
  {
    DEBUG_ERROR("pw_thread_loop_start: %s", strerror(-status));
    close(fd);
    goto fail;
  }
  this->started = true;
  pw_thread_loop_lock(this->loop);
  stage = "connecting to the portal PipeWire remote";
  this->core = pw_context_connect_fd(this->context, fd, NULL, 0);
  if (!this->core)
  {
    DEBUG_ERROR("pw_context_connect_fd: %s", strerror(errno));
    goto failLocked;
  }
  pw_core_add_listener(this->core, &this->coreListener, &coreEvents, NULL);
  stage = "creating the PipeWire capture stream";
  this->stream = pw_stream_new(this->core, "Looking Glass Linux Guest",
    pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
      "Capture", PW_KEY_MEDIA_ROLE, "Screen", NULL));
  if (!this->stream)
  {
    DEBUG_ERROR("pw_stream_new: %s", strerror(errno));
    goto failLocked;
  }
  pw_stream_add_listener(this->stream, &this->streamListener, &streamEvents, NULL);
  uint8_t storage[1024];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
  const unsigned fps = option_get_int("pipewire", "fps");
  const struct spa_pod * format = spa_pod_builder_add_object(&builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
    SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(4,
      SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA,
      SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx),
    SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
      &SPA_RECTANGLE(1920, 1080), &SPA_RECTANGLE(1, 1), &SPA_RECTANGLE(8192, 8192)),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
      &SPA_FRACTION(fps, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(fps, 1)));
  stage = "connecting the PipeWire capture stream";
  status = pw_stream_connect(this->stream, PW_DIRECTION_INPUT, node,
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, &format, 1);
  if (status < 0)
  {
    DEBUG_ERROR("pw_stream_connect: %s", strerror(-status));
    goto failLocked;
  }
  stage = "negotiating the raw video format";
  struct timespec deadline;
  pw_thread_loop_get_time(this->loop, &deadline, 15 * SPA_NSEC_PER_SEC);
  while (!this->hasFormat && !this->failed)
    if (pw_thread_loop_timed_wait_full(this->loop, &deadline) < 0)
    {
      DEBUG_ERROR("Timed out waiting for a PipeWire video format");
      goto failLocked;
    }
  if (this->failed)
    goto failLocked;
  pw_thread_loop_unlock(this->loop);
  return true;
failLocked:
  pw_thread_loop_unlock(this->loop);
fail:
  DEBUG_ERROR("PipeWire capture initialization failed during %s", stage);
  DEBUG_INFO("For a removed/wrong monitor, run bash run-guest.sh --choose-display from an accessible guest desktop");
  pipewire_deinit();
  return false;
}
static void pipewire_stop(void)
{
  if (!this->loop || !this->started)
    return;
  pw_thread_loop_lock(this->loop);
  this->stop = true;
  discardPending();
  pw_thread_loop_signal(this->loop, false);
  pw_thread_loop_unlock(this->loop);
}
static bool pipewire_deinit(void)
{
  pipewire_stop();
  /* Stop outside the lock; no callbacks can run after stop returns. */
  if (this->started)
    pw_thread_loop_stop(this->loop);
  this->started = false;
  if (this->stream) pw_stream_destroy(this->stream);
  this->stream = NULL;
  this->pending = NULL;
  if (this->core) pw_core_disconnect(this->core);
  this->core = NULL;
  if (this->context) pw_context_destroy(this->context);
  this->context = NULL;
  if (this->loop) pw_thread_loop_destroy(this->loop);
  this->loop = NULL;
  if (this->portal)
  {
    portal_destroySession(this->portal, &this->sessionHandle);
    portal_free(this->portal);
  }
  this->portal = NULL;
  return true;
}
static void pipewire_free(void)
{
  pipewire_deinit();
  free(this->snapshot);
  free(this);
  this = NULL;
  pw_deinit();
}
static CaptureResult pipewire_capture(unsigned index, FrameBuffer * target)
{
  pw_thread_loop_lock(this->loop);
  struct timespec deadline;
  pw_thread_loop_get_time(this->loop, &deadline, SPA_NSEC_PER_SEC);
  while ((!this->pending || !this->hasFormat) && !this->stop && !this->failed)
    if (pw_thread_loop_timed_wait_full(this->loop, &deadline) < 0)
    {
      pw_thread_loop_unlock(this->loop);
      return CAPTURE_RESULT_TIMEOUT;
    }
  CaptureResult result = CAPTURE_RESULT_TIMEOUT;
  if (this->stop || this->failed)
  {
    result = this->failed ? CAPTURE_RESULT_ERROR : CAPTURE_RESULT_REINIT;
    goto done;
  }
  struct spa_buffer * buffer = this->pending->buffer;
  struct spa_meta_header * header = spa_buffer_find_meta_data(buffer,
    SPA_META_Header, sizeof(*header));
  if (!buffer->n_datas || (header && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED)))
    goto done;
  struct spa_data * data = &buffer->datas[0];
  struct LGFrameLayout layout;
  if (!data->data || !data->chunk ||
      (data->type != SPA_DATA_MemPtr && data->type != SPA_DATA_MemFd) ||
      (data->chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) ||
      !lgFrameLayout(&layout, this->width, this->height, data->maxsize,
        data->chunk->offset, data->chunk->size, data->chunk->stride))
  {
    if (!this->warnedBuffer)
      DEBUG_WARN("Dropping unmapped, empty or unsupported PipeWire buffer layout");
    this->warnedBuffer = true;
    goto done;
  }
  if (layout.packedSize > this->capacity)
  {
    uint8_t * memory = realloc(this->snapshot, layout.packedSize);
    if (!memory)
    {
      result = CAPTURE_RESULT_ERROR;
      goto done;
    }
    this->snapshot = memory;
    this->capacity = layout.packedSize;
  }
  /* Copy before returning the buffer; geometry cannot change under this lock. */
  lgFramePack(this->snapshot, data->data, &layout);
  this->readyWidth = this->width;
  this->readyHeight = this->height;
  this->readyVersion = this->formatVer;
  this->readyFormat = this->format;
  this->warnedBuffer = false;
  result = CAPTURE_RESULT_OK;
done:
  discardPending();
  pw_thread_loop_unlock(this->loop);
  return result;
}
static CaptureResult pipewire_waitFrame(unsigned index, CaptureFrame * frame,
  const size_t maxFrameSize)
{
  const size_t pitch = (size_t)this->readyWidth * 4;
  if (!pitch || !this->readyHeight || maxFrameSize < pitch)
    return CAPTURE_RESULT_ERROR;
  this->dataHeight = maxFrameSize / pitch < this->readyHeight ?
    maxFrameSize / pitch : this->readyHeight;
  *frame = (CaptureFrame){
    .formatVer = this->readyVersion,
    .screenWidth = this->readyWidth, .screenHeight = this->readyHeight,
    .dataWidth = this->readyWidth, .dataHeight = this->dataHeight,
    .frameWidth = this->readyWidth, .frameHeight = this->readyHeight,
    .pitch = pitch, .stride = this->readyWidth,
    .format = this->readyFormat, .rotation = CAPTURE_ROT_0,
    .truncated = this->dataHeight < this->readyHeight,
    .hdr = false, .hdrPQ = false, .damageRectsCount = 0
  };
  return CAPTURE_RESULT_OK;
}
static CaptureResult pipewire_getFrame(unsigned index, FrameBuffer * frame,
  const size_t maxFrameSize)
{
  const size_t size = (size_t)this->readyWidth * 4 * this->dataHeight;
  if (size > maxFrameSize || !this->snapshot)
    return CAPTURE_RESULT_ERROR;
  return framebuffer_write(frame, this->snapshot, size) ?
    CAPTURE_RESULT_OK : CAPTURE_RESULT_ERROR;
}
struct CaptureInterface Capture_pipewire = {
  .shortName = "pipewire", .asyncCapture = false,
  .getName = pipewire_getName, .initOptions = pipewire_initOptions,
  .create = pipewire_create, .init = pipewire_init, .stop = pipewire_stop,
  .deinit = pipewire_deinit, .free = pipewire_free,
  .capture = pipewire_capture, .waitFrame = pipewire_waitFrame,
  .getFrame = pipewire_getFrame
};
