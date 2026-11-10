/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include "../overlay/host/platform/Linux/capture/pipewire/src/frame_layout.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t randomState = 42;
static uint32_t nextRandom(void)
{
  randomState = randomState * 1664525u + 1013904223u;
  return randomState;
}

int main(void)
{
  struct LGFrameLayout l;
  assert(lgFrameLayout(&l, 2, 2, 16, 0, 16, 8));
  assert(l.packedSize == 16 && l.offset == 0);
  assert(lgFrameLayout(&l, 2, 2, 16, 0, 16, 0));
  assert(!lgFrameLayout(&l, 0, 2, 16, 0, 16, 8));
  assert(!lgFrameLayout(&l, 2, 0, 16, 0, 16, 8));
  assert(!lgFrameLayout(&l, 2, 2, 0, 0, 16, 8));
  assert(!lgFrameLayout(&l, 2, 2, 16, 0, 0, 8));
  assert(!lgFrameLayout(&l, 2, 2, 16, 0, 15, 8));
  assert(!lgFrameLayout(&l, 2, 2, 16, 1, 16, 8));
  assert(!lgFrameLayout(&l, 2, 2, 16, 0, 16, 7));
  assert(!lgFrameLayout(&l, 2, 2, 16, 0, 16, -8));
  assert(!lgFrameLayout(&l, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    0, UINT32_MAX, INT32_MAX));
  assert(lgFrameLayout(&l, 2, 2, 16, 32, UINT32_MAX, 8));

  /* Exercise real row packing, including nonzero offsets, padding, exact
   * last-row bounds and output canaries. This catches tight-stride assumptions. */
  for (unsigned test = 0; test < 2000; ++test)
  {
    uint32_t width = nextRandom() % 128 + 1;
    uint32_t height = nextRandom() % 64 + 1;
    uint32_t pitch = width * 4 + nextRandom() % 64;
    uint32_t offset = nextRandom() % 64;
    uint32_t span = (height - 1) * pitch + width * 4;
    uint32_t allocation = offset + span;
    size_t packed = (size_t)width * height * 4;
    uint8_t * source = malloc(allocation);
    uint8_t * destination = malloc(packed + 2);
    assert(source && destination);
    memset(source, 0xdd, allocation);
    memset(destination, 0xa5, packed + 2);
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width * 4; ++x)
        source[offset + (size_t)y * pitch + x] = (uint8_t)(x + y * 17);
    assert(lgFrameLayout(&l, width, height, allocation, offset, span, pitch));
    assert(l.packedSize == packed);
    lgFramePack(destination + 1, source, &l);
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width * 4; ++x)
        assert(destination[1 + ((size_t)y * width * 4) + x] == (uint8_t)(x + y * 17));
    assert(destination[0] == 0xa5 && destination[packed + 1] == 0xa5);
    assert(!lgFrameLayout(&l, width, height, allocation, offset, span - 1, pitch));
    free(source);
    free(destination);
  }
  puts("PASS: layout edge cases and 2000 padded/offset frame copies");
  return 0;
}
