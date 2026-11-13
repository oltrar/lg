/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Read an actual B7 frame from the SAME mapping as the guest host. No display,
 * SPICE connection, compositor implementation or codec is involved here. */
#include "common/ivshmem.h"
#include "common/KVMFR.h"
#include "common/framebuffer.h"
#include "common/debug.h"
#include <lgmp/client.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void debug_level(enum DebugLevel level, const char * file, unsigned line,
  const char * function, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}
static uint64_t milliseconds(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
int main(int argc, char ** argv)
{
  if (argc < 3 || argc > 4) return 2;
  char * end;
  long seconds = strtol(argv[2], &end, 10);
  if (*end || seconds < 1 || seconds > 180) return 2;
  struct IVSHMEM shm = {0};
  if (!ivshmemOpenDev(&shm, argv[1])) return 1;
  PLGMPClient client = NULL;
  PLGMPClientQueue queue = NULL;
  if (lgmpClientInit(shm.mem, shm.size, &client) != LGMP_OK) return 1;
  uint64_t deadline = milliseconds() + seconds * 1000;
  int result = 1;
  while (milliseconds() < deadline)
  {
    if (!queue)
    {
      uint32_t size;
      uint8_t * data;
      if (lgmpClientSessionInit(client, &size, &data, NULL) != LGMP_OK)
        goto wait;
      if (size < sizeof(KVMFR) || memcmp(((KVMFR *)data)->magic, KVMFR_MAGIC, 8) ||
          ((KVMFR *)data)->version != KVMFR_VERSION)
      {
        fprintf(stderr, "Shared memory does not contain a compatible B7 host.\n");
        break;
      }
      if (lgmpClientSubscribe(client, LGMP_Q_FRAME, &queue) != LGMP_OK) goto wait;
    }
    if (!lgmpClientSessionValid(client))
    {
      lgmpClientUnsubscribe(&queue);
      goto wait;
    }
    LGMPMessage message;
    if (lgmpClientProcess(queue, &message) != LGMP_OK) goto wait;
    const KVMFRFrame * frame = message.mem;
    size_t bytes = message.size >= sizeof(*frame) ? (size_t)frame->pitch * frame->dataHeight : 0;
    uintptr_t start = (uintptr_t)message.mem, base = (uintptr_t)shm.mem;
    if (message.size < sizeof(*frame) || start < base || start - base > shm.size ||
        message.size > shm.size - (start - base) || !bytes ||
        frame->offset < sizeof(*frame) || frame->offset > message.size ||
        sizeof(FrameBuffer) > message.size - frame->offset ||
        bytes > message.size - frame->offset - sizeof(FrameBuffer) ||
        !frame->dataWidth || frame->dataWidth > 8192 || !frame->dataHeight ||
        frame->dataHeight > 8192 || frame->pitch < (size_t)frame->dataWidth * 4 ||
        (frame->type != FRAME_TYPE_BGRA && frame->type != FRAME_TYPE_RGBA) ||
        (frame->flags & FRAME_FLAG_TRUNCATED))
    {
      fprintf(stderr, "Invalid/truncated B7 raw frame; check shared-memory size (4K needs 128 MiB).\n");
      lgmpClientMessageDone(queue);
      break;
    }
    const FrameBuffer * fb = (const FrameBuffer *)((const uint8_t *)frame + frame->offset);
    while (atomic_load_explicit(&fb->wp, memory_order_acquire) < bytes &&
           milliseconds() < deadline && lgmpClientSessionValid(client)) usleep(1000);
    if (atomic_load_explicit(&fb->wp, memory_order_acquire) >= bytes)
    {
      if (argc == 4)
      {
        FILE * output = fopen(argv[3], "wb");
        if (!output || fwrite(fb->data, 1, bytes, output) != bytes || fclose(output))
          break;
      }
      printf("RAW VIDEO VERIFIED: %ux%u, B7 frame %u, %zu bytes through shared memory.\n",
        frame->dataWidth, frame->dataHeight, frame->frameSerial, bytes);
      result = 0;
    }
    lgmpClientMessageDone(queue);
    break;
wait:
    usleep(10000);
  }
  if (result) fprintf(stderr, "No complete raw Looking Glass frame verified. Read the guest capture log; a QXL/SPICE picture is not this video path.\n");
  if (queue) lgmpClientUnsubscribe(&queue);
  lgmpClientFree(&client);
  ivshmemClose(&shm);
  return result;
}
