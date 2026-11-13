/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
/* Test actual backend frame publication without a compositor/daemon. Link with
 * function/data sections and dead-code elimination to omit unused live APIs. */
#include "../overlay/host/platform/Linux/capture/pipewire/src/pipewire.c"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

static uint8_t copied[64];
static size_t copiedSize;
static bool writeResult = true;
static bool captureWrite(FrameBuffer * frame, const void * restrict source,
  size_t size)
{
  assert(size <= sizeof(copied));
  memcpy(copied, source, size);
  copiedSize = size;
  return writeResult;
}
bool (*framebuffer_write)(FrameBuffer *, const void * restrict, size_t) = captureWrite;

int main(void)
{
  uint8_t pixels[48];
  for (unsigned i = 0; i < sizeof(pixels); ++i) pixels[i] = (uint8_t)i;
  struct pipewire state = {
    .snapshot = pixels, .capacity = sizeof(pixels),
    .readyWidth = 4, .readyHeight = 3, .readyVersion = 7,
    .readyFormat = CAPTURE_FMT_RGBA,
    .width = 1920, .height = 1080, .formatVer = 99
  };
  this = &state;
  CaptureFrame frame = {0};
  assert(pipewire_waitFrame(0, &frame, 48) == CAPTURE_RESULT_OK);
  assert(frame.dataHeight == 3 && !frame.truncated);
  assert(frame.formatVer == 7 && frame.format == CAPTURE_FMT_RGBA);
  assert(frame.screenWidth == 4 && frame.screenHeight == 3);
  assert(frame.pitch == 16 && frame.stride == 4);
  assert(!frame.hdr && !frame.hdrPQ && frame.damageRectsCount == 0);
  assert(pipewire_getFrame(0, NULL, 48) == CAPTURE_RESULT_OK);
  assert(copiedSize == 48 && memcmp(copied, pixels, 48) == 0);

  memset(copied, 0xa5, sizeof(copied));
  assert(pipewire_waitFrame(0, &frame, 47) == CAPTURE_RESULT_OK);
  assert(frame.dataHeight == 2 && frame.truncated);
  assert(frame.frameHeight == 3 && frame.screenHeight == 3);
  assert(pipewire_getFrame(0, NULL, 47) == CAPTURE_RESULT_OK);
  assert(copiedSize == 32 && copied[32] == 0xa5);
  copiedSize = 0;
  assert(pipewire_getFrame(0, NULL, 31) == CAPTURE_RESULT_ERROR);
  assert(copiedSize == 0);
  assert(pipewire_waitFrame(0, &frame, 15) == CAPTURE_RESULT_ERROR);
  writeResult = false;
  assert(pipewire_getFrame(0, NULL, 48) == CAPTURE_RESULT_ERROR);
  state.snapshot = NULL;
  assert(pipewire_getFrame(0, NULL, 48) == CAPTURE_RESULT_ERROR);

  /* Production ingestion from a real mapped MemFd with offset/padded rows. */
  int fd = memfd_create("lg-frame-test", MFD_CLOEXEC);
  assert(fd >= 0 && ftruncate(fd, 64) == 0);
  uint8_t * mapped = mmap(NULL, 64, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  assert(mapped != MAP_FAILED);
  uint8_t expected[48];
  memset(mapped, 0xa5, 64);
  for (unsigned y = 0; y < 3; ++y)
    for (unsigned x = 0; x < 16; ++x)
      mapped[4 + y * 20 + x] = expected[y * 16 + x] = y * 16 + x;
  struct spa_chunk chunk = {.offset = 4, .size = 56, .stride = 20};
  struct spa_data data = {.type = SPA_DATA_MemFd, .fd = fd, .data = mapped,
    .maxsize = 64, .chunk = &chunk};
  struct spa_buffer source = {.n_datas = 1, .datas = &data};
  struct pipewire mappedState = {.width = 4, .height = 3,
    .formatVer = 8, .format = CAPTURE_FMT_BGRA};
  this = &mappedState;
  assert(copyBuffer(&source) == CAPTURE_RESULT_OK);
  assert((uintptr_t)mappedState.snapshot % 32 == 0);
  assert(mappedState.loggedCopy && memcmp(mappedState.snapshot, expected, 48) == 0);
  memset(mapped, 0xff, 64); /* Producer can reuse its buffer after the copy. */
  assert(memcmp(mappedState.snapshot, expected, 48) == 0);
  writeResult = true;
  assert(pipewire_waitFrame(0, &frame, 48) == CAPTURE_RESULT_OK);
  assert(pipewire_getFrame(0, NULL, 48) == CAPTURE_RESULT_OK);
  assert(frame.format == CAPTURE_FMT_BGRA && frame.formatVer == 8);
  assert(memcmp(copied, expected, 48) == 0 && mappedState.loggedWrite);
  data.type = SPA_DATA_DmaBuf;
  assert(copyBuffer(&source) == CAPTURE_RESULT_TIMEOUT);
  data.type = SPA_DATA_MemFd;
  data.data = NULL;
  assert(copyBuffer(&source) == CAPTURE_RESULT_TIMEOUT);
  data.data = mapped;
  chunk.size = 16;
  assert(copyBuffer(&source) == CAPTURE_RESULT_TIMEOUT);
  chunk.size = 56;
  chunk.flags = SPA_CHUNK_FLAG_CORRUPTED;
  assert(copyBuffer(&source) == CAPTURE_RESULT_TIMEOUT);
  source.n_datas = 0;
  assert(copyBuffer(&source) == CAPTURE_RESULT_TIMEOUT);
  assert(mappedState.rejectedBuffers == 5);
  assert(memcmp(mappedState.snapshot, expected, 48) == 0);
  free(mappedState.snapshot);
  assert(munmap(mapped, 64) == 0 && close(fd) == 0);
  puts("PASS: real MemFd ingestion/ownership, metadata, IVSHMEM publication, truncation and rejected buffers");
  return 0;
}
