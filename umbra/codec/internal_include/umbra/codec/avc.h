#ifndef UMBRA_CODEC_AVC_H_
#define UMBRA_CODEC_AVC_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/video_geometry.h"

namespace umbra::codec {

inline constexpr std::uint8_t kH264SpsNalType = 7;
inline constexpr std::uint8_t kH264PpsNalType = 8;

struct H264Vui {
  penumbra::Rational sample_aspect_ratio = penumbra::Rational::Of(1, 1);
  bool video_signal_present = false;
  bool colour_description_present = false;
  bool full_range = false;
  std::uint8_t colour_primaries = penumbra::ColorSpace::kUnspecified;
  std::uint8_t transfer_characteristics = penumbra::ColorSpace::kUnspecified;
  std::uint8_t matrix_coefficients = penumbra::ColorSpace::kUnspecified;
  std::optional<penumbra::Rational> frame_rate;
};

struct H264Sps {
  std::uint8_t profile_idc = 0;
  std::uint8_t constraint_flags = 0;
  std::uint8_t level_idc = 0;
  std::uint32_t sps_id = 0;
  std::uint8_t chroma_format_idc = 1;
  bool separate_colour_plane = false;
  std::uint8_t bit_depth_luma = 8;
  std::uint8_t bit_depth_chroma = 8;
  std::uint32_t max_num_ref_frames = 0;
  bool frame_mbs_only = true;
  std::int32_t coded_width = 0;
  std::int32_t coded_height = 0;
  CropRect crop;
  std::int32_t visible_width = 0;
  std::int32_t visible_height = 0;
  H264Vui vui;
};

struct AvcConfig {
  std::uint8_t profile_idc = 0;
  std::uint8_t profile_compatibility = 0;
  std::uint8_t level_idc = 0;
  std::uint8_t nal_length_size = 4;
  std::vector<std::vector<std::uint8_t>> sps;
  std::vector<std::vector<std::uint8_t>> pps;
  std::optional<std::uint8_t> chroma_format;
  std::optional<std::uint8_t> bit_depth_luma;
  std::optional<std::uint8_t> bit_depth_chroma;
  std::vector<std::vector<std::uint8_t>> sps_ext;
};

[[nodiscard]] bool H264ProfileHasChromaInfo(std::uint8_t profile_idc);

[[nodiscard]] absl::StatusOr<H264Sps> ParseH264Sps(Bytes nal_unit);

[[nodiscard]] absl::StatusOr<AvcConfig> ParseAvcC(Bytes record);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeAvcC(
    const AvcConfig& config);

[[nodiscard]] absl::StatusOr<AvcConfig> AvcConfigFromParameterSets(
    const std::vector<Bytes>& sps, const std::vector<Bytes>& pps);

[[nodiscard]] absl::StatusOr<AvcConfig> AvcConfigFromAnnexB(Bytes data);

[[nodiscard]] AvcCodec ToCodec(const AvcConfig& config);

[[nodiscard]] AvcCodec ToCodec(const H264Sps& sps);

[[nodiscard]] penumbra::VideoInfo ToVideoInfo(const H264Sps& sps);

}

#endif
