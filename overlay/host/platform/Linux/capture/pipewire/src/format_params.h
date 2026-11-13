/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <spa/buffer/buffer.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/pod/builder.h>

static inline const struct spa_pod * lgPipewireFormat(
  struct spa_pod_builder * builder, unsigned fps)
{
  return spa_pod_builder_add_object(builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
    /* Enum values contain the preferred default AND the allowed alternatives.
     * Repeat BGRA in the alternatives or a BGRA-only producer is rejected. */
    SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5,
      SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA,
      SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx),
    SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
      &SPA_RECTANGLE(1920, 1080), &SPA_RECTANGLE(1, 1), &SPA_RECTANGLE(8192, 8192)),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
      &SPA_FRACTION(0, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(fps, 1)),
    SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(
      &SPA_FRACTION(fps, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(fps, 1)));
}

static inline const struct spa_pod * lgPipewireBuffers(struct spa_pod_builder * builder)
{
  return spa_pod_builder_add_object(builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_dataType,
      SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)));
}
