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
#include "libavutil/channel_layout.h"
#include "libavutil/downmix_info.h"
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
  if (frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
    output.channel_mask =
        av_channel_layout_subset(&frame->ch_layout, UINT64_MAX);
  }
  for (int channel = 0; channel < channels; ++channel) {
    switch (av_channel_layout_channel_from_index(&frame->ch_layout, channel)) {
      case AV_CHAN_FRONT_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kFrontLeft;
        break;
      case AV_CHAN_FRONT_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kFrontRight;
        break;
      case AV_CHAN_FRONT_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kFrontCenter;
        break;
      case AV_CHAN_LOW_FREQUENCY:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kLowFrequency;
        break;
      case AV_CHAN_BACK_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kBackLeft;
        break;
      case AV_CHAN_BACK_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kBackRight;
        break;
      case AV_CHAN_BACK_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kBackCenter;
        break;
      case AV_CHAN_SIDE_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kSideLeft;
        break;
      case AV_CHAN_SIDE_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kSideRight;
        break;
      case AV_CHAN_FRONT_LEFT_OF_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kFrontLeftOfCenter;
        break;
      case AV_CHAN_FRONT_RIGHT_OF_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kFrontRightOfCenter;
        break;
      case AV_CHAN_TOP_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopCenter;
        break;
      case AV_CHAN_TOP_FRONT_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopFrontLeft;
        break;
      case AV_CHAN_TOP_FRONT_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopFrontCenter;
        break;
      case AV_CHAN_TOP_FRONT_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopFrontRight;
        break;
      case AV_CHAN_TOP_BACK_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopBackLeft;
        break;
      case AV_CHAN_TOP_BACK_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopBackCenter;
        break;
      case AV_CHAN_TOP_BACK_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopBackRight;
        break;
      case AV_CHAN_WIDE_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kWideLeft;
        break;
      case AV_CHAN_WIDE_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kWideRight;
        break;
      case AV_CHAN_SURROUND_DIRECT_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kSurroundDirectLeft;
        break;
      case AV_CHAN_SURROUND_DIRECT_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kSurroundDirectRight;
        break;
      case AV_CHAN_LOW_FREQUENCY_2:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kLowFrequency2;
        break;
      case AV_CHAN_TOP_SIDE_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopSideLeft;
        break;
      case AV_CHAN_TOP_SIDE_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kTopSideRight;
        break;
      case AV_CHAN_BOTTOM_FRONT_CENTER:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kBottomFrontCenter;
        break;
      case AV_CHAN_BOTTOM_FRONT_LEFT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kBottomFrontLeft;
        break;
      case AV_CHAN_BOTTOM_FRONT_RIGHT:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kBottomFrontRight;
        break;
      default:
        output.channel_layout.at(static_cast<size_t>(channel)) =
            AudioChannel::kUnknown;
        break;
    }
  }
  output.format = format;
  const AVFrameSideData* downmix_side_data =
      av_frame_get_side_data(frame.get(), AV_FRAME_DATA_DOWNMIX_INFO);
  if (downmix_side_data != nullptr &&
      downmix_side_data->size >= sizeof(AVDownmixInfo)) {
    const auto* downmix =
        reinterpret_cast<const AVDownmixInfo*>(downmix_side_data->data);
    output.has_downmix_metadata = true;
    const bool matrix_downmix =
        downmix->preferred_downmix_type == AV_DOWNMIX_TYPE_LTRT ||
        downmix->preferred_downmix_type == AV_DOWNMIX_TYPE_DPLII;
    output.center_mix_level =
        static_cast<float>(matrix_downmix ? downmix->center_mix_level_ltrt
                                          : downmix->center_mix_level);
    output.surround_mix_level =
        static_cast<float>(matrix_downmix ? downmix->surround_mix_level_ltrt
                                          : downmix->surround_mix_level);
    output.lfe_mix_level = static_cast<float>(downmix->lfe_mix_level);
  }
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
