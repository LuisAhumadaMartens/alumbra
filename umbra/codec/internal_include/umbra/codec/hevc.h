#ifndef UMBRA_CODEC_HEVC_H_
#define UMBRA_CODEC_HEVC_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/video_geometry.h"

namespace umbra::codec {

inline constexpr std::uint8_t kHevcVpsNalType = 32;
inline constexpr std::uint8_t kHevcSpsNalType = 33;
inline constexpr std::uint8_t kHevcPpsNalType = 34;

struct HevcProfileTierLevel {
  std::uint8_t profile_space = 0;
  bool high_tier = false;
  std::uint8_t profile_idc = 0;
  std::uint32_t compatibility_flags = 0;
  std::array<std::uint8_t, 6> constraint_flags{};
  std::uint8_t level_idc = 0;
};

struct HevcVui {
  penumbra::Rational sample_aspect_ratio = penumbra::Rational::Of(1, 1);
  bool video_signal_present = false;
  bool full_range = false;
  std::uint8_t colour_primaries = penumbra::ColorSpace::kUnspecified;
  std::uint8_t transfer_characteristics = penumbra::ColorSpace::kUnspecified;
  std::uint8_t matrix_coefficients = penumbra::ColorSpace::kUnspecified;
  std::optional<penumbra::Rational> frame_rate;
};

struct HevcVps {
  std::uint8_t vps_id = 0;
  std::uint8_t max_sub_layers = 1;
  HevcProfileTierLevel profile;
};

struct HevcSps {
  std::uint8_t vps_id = 0;
  std::uint8_t max_sub_layers = 1;
  bool temporal_id_nesting = false;
  HevcProfileTierLevel profile;
  std::uint32_t sps_id = 0;
  std::uint8_t chroma_format_idc = 1;
  bool separate_colour_plane = false;
  std::int32_t coded_width = 0;
  std::int32_t coded_height = 0;
  CropRect crop;
  std::int32_t visible_width = 0;
  std::int32_t visible_height = 0;
  std::uint8_t bit_depth_luma = 8;
  std::uint8_t bit_depth_chroma = 8;
  HevcVui vui;
};

struct HevcNalArray {
  bool array_completeness = false;
  std::uint8_t nal_unit_type = 0;
  std::vector<std::vector<std::uint8_t>> units;
};

struct HevcConfig {
  HevcProfileTierLevel profile;
  std::uint16_t min_spatial_segmentation_idc = 0;
  std::uint8_t parallelism_type = 0;
  std::uint8_t chroma_format = 1;
  std::uint8_t bit_depth_luma = 8;
  std::uint8_t bit_depth_chroma = 8;
  std::uint16_t average_frame_rate = 0;
  std::uint8_t constant_frame_rate = 0;
  std::uint8_t num_temporal_layers = 1;
  bool temporal_id_nested = false;
  std::uint8_t nal_length_size = 4;
  std::vector<HevcNalArray> arrays;
};

[[nodiscard]] absl::StatusOr<HevcVps> ParseHevcVps(Bytes nal_unit);

[[nodiscard]] absl::StatusOr<HevcSps> ParseHevcSps(Bytes nal_unit);

[[nodiscard]] absl::StatusOr<HevcConfig> ParseHvcC(Bytes record);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeHvcC(
    const HevcConfig& config);

[[nodiscard]] absl::StatusOr<HevcConfig> HevcConfigFromAnnexB(Bytes data);

[[nodiscard]] HevcCodec ToCodec(const HevcConfig& config,
                                std::string_view sample_entry = "hvc1");

[[nodiscard]] HevcCodec ToCodec(const HevcSps& sps,
                                std::string_view sample_entry = "hvc1");

[[nodiscard]] penumbra::VideoInfo ToVideoInfo(const HevcSps& sps);

}

#endif
