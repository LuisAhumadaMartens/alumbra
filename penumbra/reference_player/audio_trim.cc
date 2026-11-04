#include "penumbra/reference_player/audio_trim.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::reference_player {
namespace {

size_t BytesPerAudioSample(SampleFormat format) {
  switch (format) {
    case SampleFormat::kU8:
    case SampleFormat::kU8Planar:
      return 1;
    case SampleFormat::kS16:
    case SampleFormat::kS16Planar:
      return 2;
    case SampleFormat::kS32:
    case SampleFormat::kF32:
    case SampleFormat::kS32Planar:
    case SampleFormat::kF32Planar:
      return 4;
    case SampleFormat::kUnknown:
      return 0;
  }
  return 0;
}

bool IsPlanarAudio(SampleFormat format) {
  return format == SampleFormat::kU8Planar ||
         format == SampleFormat::kS16Planar ||
         format == SampleFormat::kS32Planar ||
         format == SampleFormat::kF32Planar;
}

}

std::optional<DecodedAudioBuffer> TrimAudioBuffer(
    const DecodedAudioBuffer& source, MediaTime target, bool* reached_target) {
  *reached_target = false;
  if (source.frame_count <= 0 || source.sample_rate <= 0) {
    return std::nullopt;
  }
  if (!source.presentation_time.is_finite() || !target.is_finite()) {
    *reached_target = true;
    return source;
  }
  if (source.presentation_time >= target) {
    *reached_target = true;
    return source;
  }
  const auto sample_time_base =
      Rational::Create(1, static_cast<int64_t>(source.sample_rate));
  if (!sample_time_base.has_value()) {
    *reached_target = true;
    return source;
  }
  const std::optional<int64_t> delta = Rescale(
      target.InMicroseconds() - source.presentation_time.InMicroseconds(),
      kMicrosecondTimeBase, *sample_time_base, Rounding::kUp);
  if (!delta.has_value()) {
    *reached_target = true;
    return source;
  }
  if (*delta >= source.frame_count) {
    return std::nullopt;
  }
  if (*delta <= 0) {
    *reached_target = true;
    return source;
  }
  const size_t bytes_per_sample = BytesPerAudioSample(source.format);
  if (bytes_per_sample == 0 || source.channel_count <= 0) {
    *reached_target = true;
    return source;
  }
  DecodedAudioBuffer result = source;
  const auto frame_offset = static_cast<size_t>(*delta);
  if (IsPlanarAudio(source.format)) {
    int32_t channel = 0;
    std::ranges::for_each(result.planes, [&](const uint8_t*& plane) {
      if (channel < source.channel_count && plane != nullptr) {
        const auto total_bytes =
            static_cast<size_t>(source.frame_count) * bytes_per_sample;
        const auto byte_offset = frame_offset * bytes_per_sample;
        plane = std::span<const uint8_t>(plane, total_bytes)
                    .subspan(byte_offset)
                    .data();
      }
      ++channel;
    });
  } else if (result.planes[0] != nullptr) {
    const auto bytes_per_frame =
        bytes_per_sample * static_cast<size_t>(source.channel_count);
    const auto total_bytes =
        static_cast<size_t>(source.frame_count) * bytes_per_frame;
    const auto byte_offset = frame_offset * bytes_per_frame;
    result.planes[0] = std::span<const uint8_t>(result.planes[0], total_bytes)
                           .subspan(byte_offset)
                           .data();
  }
  result.frame_count -= static_cast<int32_t>(*delta);
  *reached_target = true;
  return result;
}

}
