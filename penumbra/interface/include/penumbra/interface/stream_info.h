#ifndef PENUMBRA_INTERFACE_STREAM_INFO_H_
#define PENUMBRA_INTERFACE_STREAM_INFO_H_

#include <cstdint>
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

enum class ChromaSiting : uint8_t {
  kUnspecified,
  kLeft,
  kCenter,
  kTopLeft,
};

struct ColorSpace {
  static constexpr uint8_t kUnspecified = 2;

  uint8_t primaries = kUnspecified;
  uint8_t transfer = kUnspecified;
  uint8_t matrix = kUnspecified;
  bool full_range = false;
  ChromaSiting chroma_siting = ChromaSiting::kUnspecified;

  friend constexpr bool operator==(const ColorSpace&,
                                   const ColorSpace&) = default;
};

struct VideoInfo {
  int32_t coded_width = 0;
  int32_t coded_height = 0;
  int32_t display_width = 0;
  int32_t display_height = 0;
  Rational sample_aspect_ratio = Rational::Of(1, 1);
  Rational frame_rate;
  ColorSpace color;
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
