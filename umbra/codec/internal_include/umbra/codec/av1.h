#ifndef UMBRA_CODEC_AV1_H_
#define UMBRA_CODEC_AV1_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

inline constexpr std::uint8_t kAv1SequenceHeaderObu = 1;

struct Av1SequenceHeader {
  std::uint8_t profile = 0;
  bool still_picture = false;
  bool reduced_still_picture_header = false;
  std::uint8_t level = 0;
  bool high_tier = false;
  std::uint8_t bit_depth = 8;
  bool monochrome = false;
  bool subsampling_x = true;
  bool subsampling_y = true;
  std::uint8_t chroma_sample_position = 0;
  bool colour_description_present = false;
  std::uint8_t colour_primaries = penumbra::ColorSpace::kUnspecified;
  std::uint8_t transfer_characteristics = penumbra::ColorSpace::kUnspecified;
  std::uint8_t matrix_coefficients = penumbra::ColorSpace::kUnspecified;
  bool full_range = false;
  std::uint32_t max_frame_width = 0;
  std::uint32_t max_frame_height = 0;
};

struct Av1Config {
  std::uint8_t profile = 0;
  std::uint8_t level = 0;
  bool high_tier = false;
  bool high_bitdepth = false;
  bool twelve_bit = false;
  bool monochrome = false;
  bool subsampling_x = true;
  bool subsampling_y = true;
  std::uint8_t chroma_sample_position = 0;
  std::optional<std::uint8_t> initial_presentation_delay_minus_one;
  std::vector<std::uint8_t> config_obus;
  std::optional<Av1SequenceHeader> sequence_header;
};

[[nodiscard]] absl::StatusOr<Av1SequenceHeader> ParseAv1SequenceHeaderObu(
    Bytes obu);

[[nodiscard]] absl::StatusOr<std::optional<Av1SequenceHeader>>
FindAv1SequenceHeader(Bytes obus);

[[nodiscard]] absl::StatusOr<Av1Config> ParseAv1C(Bytes record);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeAv1C(
    const Av1Config& config);

[[nodiscard]] absl::StatusOr<Av1Config> Av1ConfigFromSequenceHeaderObu(
    Bytes obu);

[[nodiscard]] std::uint8_t BitDepthOf(const Av1Config& config);

[[nodiscard]] Av1Codec ToCodec(const Av1Config& config);

[[nodiscard]] Av1Codec ToCodec(const Av1SequenceHeader& header);

[[nodiscard]] penumbra::VideoInfo ToVideoInfo(const Av1SequenceHeader& header);

}

#endif
