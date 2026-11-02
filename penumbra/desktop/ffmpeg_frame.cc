#include "penumbra/desktop/ffmpeg_frame.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/desktop/decode_errors.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/samplefmt.h"
}

namespace penumbra::desktop {
namespace {

struct VideoLayout {
  PixelFormat format = PixelFormat::kUnknown;
  size_t planes = 0;
  bool full_range = false;
};

VideoLayout LayoutOf(int pixel_format) {
  switch (pixel_format) {
    case AV_PIX_FMT_YUV420P:
      return {.format = PixelFormat::kI420, .planes = 3, .full_range = false};
    case AV_PIX_FMT_YUVJ420P:
      return {.format = PixelFormat::kI420, .planes = 3, .full_range = true};
    case AV_PIX_FMT_YUV420P10LE:
      return {.format = PixelFormat::kI010, .planes = 3, .full_range = false};
    case AV_PIX_FMT_NV12:
      return {.format = PixelFormat::kNv12, .planes = 2, .full_range = false};
    case AV_PIX_FMT_P010LE:
      return {.format = PixelFormat::kP010, .planes = 2, .full_range = false};
    default:
      return {};
  }
}

SampleFormat SampleFormatOf(int format) {
  switch (format) {
    case AV_SAMPLE_FMT_U8:
      return SampleFormat::kU8;
    case AV_SAMPLE_FMT_S16:
      return SampleFormat::kS16;
    case AV_SAMPLE_FMT_S32:
      return SampleFormat::kS32;
    case AV_SAMPLE_FMT_FLT:
      return SampleFormat::kF32;
    case AV_SAMPLE_FMT_U8P:
      return SampleFormat::kU8Planar;
    case AV_SAMPLE_FMT_S16P:
      return SampleFormat::kS16Planar;
    case AV_SAMPLE_FMT_S32P:
      return SampleFormat::kS32Planar;
    case AV_SAMPLE_FMT_FLTP:
      return SampleFormat::kF32Planar;
    default:
      return SampleFormat::kUnknown;
  }
}

std::shared_ptr<const void> ShareFrame(AvFramePtr frame) {
  return std::shared_ptr<AVFrame>(frame.release(), AvFrameDeleter{});
}

MediaTime TimeOf(int64_t value, Rational time_base) {
  if (value == AV_NOPTS_VALUE) {
    return MediaTime::Unknown();
  }
  const auto time = ToMediaTime(
      Timestamp{.value = value, .time_base = time_base}, Rounding::kNearest);
  return time.value_or(MediaTime::Unknown());
}

TimeDelta DurationOf(int64_t value, Rational time_base) {
  if (value <= 0) {
    return TimeDelta::Unknown();
  }
  const auto duration = ToTimeDelta(
      Timestamp{.value = value, .time_base = time_base}, Rounding::kNearest);
  return duration.value_or(TimeDelta::Unknown());
}

ColorSpace ColorOf(const AVFrame& frame, bool full_range) {
  const auto code = [](int value) {
    return value >= 0 && value <= 255 ? static_cast<uint8_t>(value)
                                      : ColorSpace::kUnspecified;
  };
  return {.primaries = code(frame.color_primaries),
          .transfer = code(frame.color_trc),
          .matrix = code(frame.colorspace),
          .full_range = full_range || frame.color_range == AVCOL_RANGE_JPEG};
}

}

void AvFrameDeleter::operator()(AVFrame* frame) const { av_frame_free(&frame); }

AvFramePtr AllocateAvFrame() { return AvFramePtr(av_frame_alloc()); }

bool IsCorruptFrame(const AVFrame& frame) {
  return (frame.flags & AV_FRAME_FLAG_CORRUPT) != 0 ||
         frame.decode_error_flags != 0;
}

absl::StatusOr<DecodedVideoFrame> WrapVideoFrame(AvFramePtr frame,
                                                 Rational time_base,
                                                 Rational frame_rate) {
  const VideoLayout layout = LayoutOf(frame->format);
  if (layout.format == PixelFormat::kUnknown) {
    const char* name =
        av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
    return CodecUnsupportedError(
        absl::StrCat("pixel format ", name == nullptr ? "unknown" : name,
                     " has no frame descriptor"));
  }
  DecodedVideoFrame output;
  output.width = frame->width;
  output.height = frame->height;
  output.format = layout.format;
  const int64_t pts =
      frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
  output.presentation_time = TimeOf(pts, time_base);
  output.duration = DurationOf(frame->duration, time_base);
  if (frame->duration <= 0 && frame_rate.is_positive()) {
    const auto base =
        Rational::Create(frame_rate.denominator(), frame_rate.numerator());
    if (base.has_value()) {
      output.duration = DurationOf(1, *base);
    }
  }
  output.color = ColorOf(*frame, layout.full_range);
  const std::span<uint8_t*> data(frame->data);
  const std::span<int> strides(frame->linesize);
  for (size_t plane = 0; plane < layout.planes; ++plane) {
    output.planes.at(plane) = {.data = data[plane], .stride = strides[plane]};
  }
  output.storage = ShareFrame(std::move(frame));
  return output;
}

absl::StatusOr<DecodedAudioBuffer> WrapAudioFrame(AvFramePtr frame,
                                                  Rational time_base,
                                                  MediaTime fallback_time) {
  const SampleFormat format = SampleFormatOf(frame->format);
  if (format == SampleFormat::kUnknown) {
    return CodecUnsupportedError("sample format has no buffer descriptor");
  }
  const int channels = frame->ch_layout.nb_channels;
  const bool planar =
      av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format)) != 0;
  const auto planes = planar ? static_cast<size_t>(channels) : size_t{1};
  if (channels <= 0 || planes > DecodedAudioBuffer::kMaxPlanes) {
    return CodecUnsupportedError(
        absl::StrCat("an audio frame with ", channels, " channels"));
  }
  DecodedAudioBuffer output;
  output.frame_count = frame->nb_samples;
  output.sample_rate = frame->sample_rate;
  output.channel_count = channels;
  output.format = format;
  output.presentation_time = frame->pts != AV_NOPTS_VALUE
                                 ? TimeOf(frame->pts, time_base)
                                 : fallback_time;
  const std::span<uint8_t*> data(frame->extended_data, planes);
  for (size_t plane = 0; plane < planes; ++plane) {
    output.planes.at(plane) = data[plane];
  }
  output.storage = ShareFrame(std::move(frame));
  return output;
}

}
