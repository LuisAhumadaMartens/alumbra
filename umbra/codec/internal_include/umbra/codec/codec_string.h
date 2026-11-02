#ifndef UMBRA_CODEC_CODEC_STRING_H_
#define UMBRA_CODEC_CODEC_STRING_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::codec {

struct AvcCodec {
  std::string sample_entry = "avc1";
  std::uint8_t profile_idc = 0;
  std::uint8_t constraint_flags = 0;
  std::uint8_t level_idc = 0;

  friend bool operator==(const AvcCodec&, const AvcCodec&) = default;
};

struct HevcCodec {
  std::string sample_entry = "hvc1";
  std::uint8_t profile_space = 0;
  std::uint8_t profile_idc = 0;
  std::uint32_t compatibility_flags = 0;
  bool high_tier = false;
  std::uint8_t level_idc = 0;
  std::array<std::uint8_t, 6> constraint_flags{};

  friend bool operator==(const HevcCodec&, const HevcCodec&) = default;
};

struct Vp9Codec {
  std::uint8_t profile = 0;
  std::uint8_t level = 0;
  std::uint8_t bit_depth = 8;
  bool has_optional_fields = false;
  std::uint8_t chroma_subsampling = 1;
  std::uint8_t colour_primaries = 1;
  std::uint8_t transfer_characteristics = 1;
  std::uint8_t matrix_coefficients = 1;
  bool full_range = false;

  friend bool operator==(const Vp9Codec&, const Vp9Codec&) = default;
};

struct Av1Codec {
  std::uint8_t profile = 0;
  std::uint8_t level = 0;
  bool high_tier = false;
  std::uint8_t bit_depth = 8;
  bool monochrome = false;
  bool subsampling_x = true;
  bool subsampling_y = true;
  std::uint8_t chroma_sample_position = 0;
  std::uint8_t colour_primaries = 1;
  std::uint8_t transfer_characteristics = 1;
  std::uint8_t matrix_coefficients = 1;
  bool full_range = false;

  friend bool operator==(const Av1Codec&, const Av1Codec&) = default;
};

struct Mp4aCodec {
  std::uint8_t object_type_indication = 0x40;
  std::optional<std::uint8_t> audio_object_type;

  friend bool operator==(const Mp4aCodec&, const Mp4aCodec&) = default;
};

struct OpusCodec {
  friend bool operator==(const OpusCodec&, const OpusCodec&) = default;
};

struct FlacCodec {
  friend bool operator==(const FlacCodec&, const FlacCodec&) = default;
};

struct Ac3Codec {
  friend bool operator==(const Ac3Codec&, const Ac3Codec&) = default;
};

struct Eac3Codec {
  friend bool operator==(const Eac3Codec&, const Eac3Codec&) = default;
};

struct DolbyVisionCodec {
  std::string sample_entry = "dvh1";
  std::uint8_t profile = 0;
  std::uint8_t level = 0;

  friend bool operator==(const DolbyVisionCodec&,
                         const DolbyVisionCodec&) = default;
};

using CodecDescription =
    std::variant<AvcCodec, HevcCodec, Vp9Codec, Av1Codec, Mp4aCodec, OpusCodec,
                 FlacCodec, Ac3Codec, Eac3Codec, DolbyVisionCodec>;

[[nodiscard]] std::string FormatCodecString(const CodecDescription& codec);

[[nodiscard]] absl::StatusOr<CodecDescription> ParseCodecString(
    std::string_view text);

[[nodiscard]] absl::StatusOr<std::vector<CodecDescription>> ParseCodecList(
    std::string_view attribute);

[[nodiscard]] penumbra::CodecId CodecIdOf(const CodecDescription& codec);

}

#endif
