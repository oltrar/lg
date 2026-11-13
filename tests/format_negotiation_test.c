/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <spa/buffer/buffer.h>
#include <spa/pod/filter.h>
#include "../overlay/host/platform/Linux/capture/pipewire/src/format_params.h"

int main(void)
{
  const uint32_t formats[] = {SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA,
    SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx};
  for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
  {
    unsigned char sourceBytes[1024], sinkBytes[2048], resultBytes[4096];
    struct spa_pod_builder source = SPA_POD_BUILDER_INIT(sourceBytes, sizeof(sourceBytes));
    struct spa_pod_builder sink = SPA_POD_BUILDER_INIT(sinkBytes, sizeof(sinkBytes));
    struct spa_pod_builder output = SPA_POD_BUILDER_INIT(resultBytes, sizeof(resultBytes));
    /* A producer with exactly one raw format, fixed output geometry, and
     * variable frame timing, like a compositor's CPU-readable screen stream. */
    const struct spa_pod * producer = spa_pod_builder_add_object(&source,
      SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
      SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
      SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_VIDEO_format, SPA_POD_Id(formats[i]),
      SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&SPA_RECTANGLE(2560, 1440)),
      SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)),
      SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(
        &SPA_FRACTION(144, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(144, 1)));
    const struct spa_pod * consumer = lgPipewireFormat(&sink, 60);
    struct spa_pod * result = NULL;
    assert(spa_pod_filter(&output, &result, producer, consumer) == 0);
    assert(spa_pod_fixate(result) == 0);
    struct spa_video_info_raw info = {0};
    assert(spa_format_video_raw_parse(result, &info) >= 0);
    assert(info.format == formats[i] && info.size.width == 2560 && info.size.height == 1440);
    assert(info.framerate.num == 0 && info.max_framerate.num <= 60 * info.max_framerate.denom);
    output = SPA_POD_BUILDER_INIT(resultBytes, sizeof(resultBytes));
    assert(spa_pod_filter(&output, &result, consumer, producer) == 0);
  }

  /* Negotiate each CPU-readable buffer type independently, in both directions. */
  const int types[] = {SPA_DATA_MemFd, SPA_DATA_MemPtr};
  for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
  {
    unsigned char a[512], b[512], c[1024];
    struct spa_pod_builder ab = SPA_POD_BUILDER_INIT(a, sizeof(a));
    struct spa_pod_builder bb = SPA_POD_BUILDER_INIT(b, sizeof(b));
    struct spa_pod_builder cb = SPA_POD_BUILDER_INIT(c, sizeof(c));
    const struct spa_pod * consumer = lgPipewireBuffers(&ab);
    const struct spa_pod * producer = spa_pod_builder_add_object(&bb,
      SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
      SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << types[i]));
    struct spa_pod * result = NULL;
    assert(spa_pod_filter(&cb, &result, producer, consumer) == 0);
    assert(spa_pod_fixate(result) == 0);
    const struct spa_pod_prop * prop = spa_pod_find_prop(result, NULL, SPA_PARAM_BUFFERS_dataType);
    assert(prop);
    int32_t type = 0;
    assert(spa_pod_parse_object(result, SPA_TYPE_OBJECT_ParamBuffers, NULL,
      SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(&type)) >= 0);
    assert(type == (1 << types[i]));
    cb = SPA_POD_BUILDER_INIT(c, sizeof(c));
    assert(spa_pod_filter(&cb, &result, consumer, producer) == 0);
  }
  puts("PASS: real SPA negotiation with BGRA-only/RGBA/BGRx/RGBx producers, rate limits and CPU buffer types");
  return 0;
}
