/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct LGFrameLayout
{
  size_t offset, sourcePitch, rowBytes, height, packedSize;
};

/* Reject negative strides and wrapped images. Offset is modulo maxsize. */
static inline bool lgFrameLayout(struct LGFrameLayout * out,
  uint32_t width, uint32_t height, uint32_t maxsize,
  uint32_t offset, uint32_t size, int32_t stride)
{
  if (!out || !width || !height || !maxsize || stride < 0)
    return false;
  const size_t row = (size_t)width * 4;
  if (row / 4 != width)
    return false;
  const size_t pitch = stride ? (size_t)stride : row;
  const size_t start = offset % maxsize;
  const size_t available = size < maxsize ? size : maxsize;
  if (pitch < row || height > SIZE_MAX / row ||
      height - 1 > (SIZE_MAX - row) / pitch)
    return false;
  const size_t span = ((size_t)height - 1) * pitch + row;
  if (span > available || span > maxsize - start)
    return false;
  *out = (struct LGFrameLayout){start, pitch, row, height, row * height};
  return true;
}

static inline void lgFramePack(uint8_t * dst, const uint8_t * src,
  const struct LGFrameLayout * layout)
{
  src += layout->offset;
  for (size_t y = 0; y < layout->height; ++y)
    memcpy(dst + y * layout->rowBytes, src + y * layout->sourcePitch,
      layout->rowBytes);
}
