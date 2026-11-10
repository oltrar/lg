/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
/* Test actual backend frame publication without a compositor/daemon. Link with
 * function/data sections and dead-code elimination to omit unused live APIs. */
#include "../overlay/host/platform/Linux/capture/pipewire/src/pipewire.c"
#include <assert.h>
#include <stdio.h>

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
  puts("PASS: actual backend metadata, full/truncated publication and bounds");
  return 0;
}
