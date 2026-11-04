#ifndef PENUMBRA_INTERFACE_STREAM_INFO_H_
#define PENUMBRA_INTERFACE_STREAM_INFO_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"

namespace penumbra {

enum class StreamType : uint8_t {
  kVideo,
  kAudio,
  kText,
};

enum class CodecId : uint8_t {
  kUnknown,
  kH264,
  kHevc,
  kVp9,
  kAv1,
  kAac,
  kOpus,
  kAc3,
  kEac3,
  kFlac,
  kMp3,
  kVp8,
  kVorbis,
  kTheora,
};

enum class NalUnitFraming : uint8_t {
  kUnspecified,
  kAnnexB,
  kLengthPrefixed,
};

enum class AacFraming : uint8_t {
  kUnspecified,
  kRaw,
  kAdts,
};

enum class SampleFormat : uint8_t {
  kUnknown,
  kU8,
  kS16,
  kS32,
  kF32,
  kU8Planar,
  kS16Planar,
  kS32Planar,
  kF32Planar,
};

enum class AudioChannel : uint8_t {
  kUnknown,
  kFrontLeft,
  kFrontRight,
  kFrontCenter,
  kLowFrequency,
  kBackLeft,
  kBackRight,
  kFrontLeftOfCenter,
  kFrontRightOfCenter,
  kBackCenter,
  kSideLeft,
  kSideRight,
  kTopCenter,
  kTopFrontLeft,
  kTopFrontCenter,
  kTopFrontRight,
  kTopBackLeft,
  kTopBackCenter,
  kTopBackRight,
  kWideLeft,
  kWideRight,
  kSurroundDirectLeft,
  kSurroundDirectRight,
  kLowFrequency2,
  kTopSideLeft,
  kTopSideRight,
  kBottomFrontCenter,
  kBottomFrontLeft,
  kBottomFrontRight,
};

enum class ChromaSiting : uint8_t {
  kUnspecified,
  kLeft,
  kCenter,
  kTopLeft,
};

struct MasteringDisplayMetadata {
  std::array<uint16_t, 3> display_primaries_x = {};
  std::array<uint16_t, 3> display_primaries_y = {};
  uint16_t white_point_x = 0;
  uint16_t white_point_y = 0;
  uint32_t max_luminance = 0;
  uint32_t min_luminance = 0;

  friend constexpr bool operator==(const MasteringDisplayMetadata&,
                                   const MasteringDisplayMetadata&) = default;
};

struct HdrStaticMetadata {
  std::optional<MasteringDisplayMetadata> mastering_display;
  std::optional<uint16_t> max_content_light_level;
  std::optional<uint16_t> max_frame_average_light_level;

  friend constexpr bool operator==(const HdrStaticMetadata&,
                                   const HdrStaticMetadata&) = default;
};

struct DolbyVisionConfig {
  uint8_t profile = 0;
  uint8_t level = 0;
  uint8_t base_layer_compatibility_id = 0;

  friend constexpr bool operator==(const DolbyVisionConfig&,
                                   const DolbyVisionConfig&) = default;
};

struct ColorSpace {
  static constexpr uint8_t kUnspecified = 2;

  uint8_t primaries = kUnspecified;
  uint8_t transfer = kUnspecified;
  uint8_t matrix = kUnspecified;
  bool full_range = false;
  bool range_specified = false;
  ChromaSiting chroma_siting = ChromaSiting::kUnspecified;

  friend constexpr bool operator==(const ColorSpace&,
                                   const ColorSpace&) = default;
};

using ColourInfo = ColorSpace;

struct VideoInfo {
  int32_t coded_width = 0;
  int32_t coded_height = 0;
  int32_t display_width = 0;
  int32_t display_height = 0;
  Rational sample_aspect_ratio = Rational::Of(1, 1);
  Rational frame_rate;
  ColorSpace color;
  HdrStaticMetadata hdr_static_metadata;
  std::optional<DolbyVisionConfig> dolby_vision;
};

struct AudioInfo {
  int32_t sample_rate = 0;
  int32_t channel_count = 0;
  uint64_t channel_mask = 0;
  SampleFormat sample_format = SampleFormat::kUnknown;
};

struct TextInfo {};

struct StreamInfo {
  uint32_t track_id = 0;
  CodecId codec = CodecId::kUnknown;
  std::string codec_string;
  SharedBytes codec_config;
  NalUnitFraming nal_unit_framing = NalUnitFraming::kUnspecified;
  uint8_t nal_length_size = 0;
  AacFraming aac_framing = AacFraming::kUnspecified;
  Rational time_base;
  std::string language;
  std::variant<VideoInfo, AudioInfo, TextInfo> format;

  [[nodiscard]] StreamType type() const {
    if (std::holds_alternative<AudioInfo>(format)) {
      return StreamType::kAudio;
    }
    if (std::holds_alternative<TextInfo>(format)) {
      return StreamType::kText;
    }
    return StreamType::kVideo;
  }
};

constexpr std::string_view StreamTypeName(StreamType type) {
  switch (type) {
    case StreamType::kVideo:
      return "video";
    case StreamType::kAudio:
      return "audio";
    case StreamType::kText:
      return "text";
  }
  return "unknown";
}

constexpr std::string_view CodecName(CodecId codec) {
  switch (codec) {
    case CodecId::kUnknown:
      return "unknown";
    case CodecId::kH264:
      return "h264";
    case CodecId::kHevc:
      return "hevc";
    case CodecId::kVp9:
      return "vp9";
    case CodecId::kAv1:
      return "av1";
    case CodecId::kAac:
      return "aac";
    case CodecId::kOpus:
      return "opus";
    case CodecId::kAc3:
      return "ac3";
    case CodecId::kEac3:
      return "eac3";
    case CodecId::kFlac:
      return "flac";
    case CodecId::kMp3:
      return "mp3";
    case CodecId::kVp8:
      return "vp8";
    case CodecId::kVorbis:
      return "vorbis";
    case CodecId::kTheora:
      return "theora";
  }
  return "unknown";
}

template <typename Sink>
void AbslStringify(Sink& sink, StreamType type) {
  sink.Append(StreamTypeName(type));
}

template <typename Sink>
void AbslStringify(Sink& sink, CodecId codec) {
  sink.Append(CodecName(codec));
}

}

#endif
