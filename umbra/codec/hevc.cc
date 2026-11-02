#include "umbra/codec/hevc.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/nal_unit.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/video_geometry.h"

namespace umbra::codec {
namespace {

constexpr std::uint32_t kExtendedSar = 255;
constexpr std::uint32_t kMaxDimension = 32768;
constexpr std::uint32_t kMaxSubLayersMinus1 = 6;
constexpr std::uint32_t kMaxShortTermSets = 64;
constexpr std::uint32_t kMaxPicsPerSet = 16;
constexpr std::uint32_t kMaxLongTermPics = 32;
constexpr std::size_t kSubLayerProfileBits = 88;
constexpr std::size_t kSubLayerLevelBits = 8;
constexpr std::size_t kMaxArrayCount = 255;
constexpr std::size_t kMaxUnitsPerArray = 65535;

absl::Status CheckNalHeader(Bytes nal_unit, std::uint8_t expected) {
  if (nal_unit.size() < 3) {
    return Corrupt("HEVC NAL unit is too short");
  }
  const std::uint8_t type = (nal_unit[0] >> 1U) & 0x3fU;
  if ((nal_unit[0] & 0x80U) != 0 || type != expected ||
      (nal_unit[1] & 7U) == 0) {
    return Corrupt(
        absl::StrCat("NAL unit is not HEVC type ", static_cast<int>(expected)));
  }
  return absl::OkStatus();
}

void SkipSubLayerProfileTierLevel(BitstreamReader& bits,
                                  std::uint32_t sub_layers_minus1) {
  std::vector<bool> profile_present;
  std::vector<bool> level_present;
  for (std::uint32_t i = 0; i < sub_layers_minus1; ++i) {
    profile_present.push_back(bits.Flag());
    level_present.push_back(bits.Flag());
  }
  if (sub_layers_minus1 > 0) {
    bits.Skip(2 * static_cast<std::size_t>(8 - sub_layers_minus1));
  }
  for (std::uint32_t i = 0; i < sub_layers_minus1; ++i) {
    if (profile_present.at(i)) {
      bits.Skip(kSubLayerProfileBits);
    }
    if (level_present.at(i)) {
      bits.Skip(kSubLayerLevelBits);
    }
  }
}

HevcProfileTierLevel ReadProfileTierLevel(BitstreamReader& bits,
                                          std::uint32_t sub_layers_minus1) {
  HevcProfileTierLevel ptl;
  ptl.profile_space = static_cast<std::uint8_t>(bits.Bits(2));
  ptl.high_tier = bits.Flag();
  ptl.profile_idc = static_cast<std::uint8_t>(bits.Bits(5));
  ptl.compatibility_flags = bits.Bits(32);
  const std::uint64_t constraints = bits.Bits64(48);
  ptl.level_idc = static_cast<std::uint8_t>(bits.Bits(8));
  for (std::size_t i = 0; i < ptl.constraint_flags.size(); ++i) {
    ptl.constraint_flags.at(i) =
        static_cast<std::uint8_t>(constraints >> (40U - (8U * i)));
  }
  SkipSubLayerProfileTierLevel(bits, sub_layers_minus1);
  return ptl;
}

void SkipScalingListData(BitstreamReader& bits) {
  for (int size_id = 0; size_id < 4; ++size_id) {
    const int step = size_id == 3 ? 3 : 1;
    for (int matrix_id = 0; matrix_id < 6; matrix_id += step) {
      if (!bits.Flag()) {
        static_cast<void>(bits.Ue());
        continue;
      }
      if (size_id > 1) {
        static_cast<void>(bits.Se());
      }
      const int coefficients = std::min(64, 1 << (4 + (size_id << 1)));
      for (int i = 0; i < coefficients; ++i) {
        static_cast<void>(bits.Se());
      }
    }
  }
}

std::uint32_t ReadInterRefPicSet(BitstreamReader& bits,
                                 std::uint32_t reference_deltas) {
  bits.Skip(1);
  static_cast<void>(bits.UeAtMost(kMaxDimension - 1));
  std::uint32_t count = 0;
  for (std::uint32_t j = 0; j <= reference_deltas; ++j) {
    const bool used = bits.Flag();
    const bool use_delta = used || bits.Flag();
    count += use_delta ? 1U : 0U;
  }
  return count;
}

std::uint32_t ReadIntraRefPicSet(BitstreamReader& bits) {
  const std::uint32_t negative = bits.UeAtMost(kMaxPicsPerSet);
  const std::uint32_t positive = bits.UeAtMost(kMaxPicsPerSet);
  for (std::uint32_t i = 0; i < negative + positive; ++i) {
    static_cast<void>(bits.Ue());
    bits.Skip(1);
  }
  return negative + positive;
}

void SkipShortTermRefPicSets(BitstreamReader& bits, std::uint32_t count) {
  std::vector<std::uint32_t> deltas;
  for (std::uint32_t index = 0; index < count; ++index) {
    const bool inter = index != 0 && bits.Flag();
    deltas.push_back(inter ? ReadInterRefPicSet(bits, deltas.at(index - 1))
                           : ReadIntraRefPicSet(bits));
  }
}

void ReadAspectRatio(BitstreamReader& bits, HevcVui& vui) {
  if (!bits.Flag()) {
    return;
  }
  const std::uint32_t indicator = bits.Bits(8);
  if (indicator != kExtendedSar) {
    vui.sample_aspect_ratio = AspectRatioFromIndicator(indicator);
    return;
  }
  const std::uint32_t width = bits.Bits(16);
  const std::uint32_t height = bits.Bits(16);
  if (width > 0 && height > 0) {
    vui.sample_aspect_ratio = penumbra::Rational::Create(width, height)
                                  .value_or(penumbra::Rational::Of(1, 1));
  }
}

void ReadVideoSignal(BitstreamReader& bits, HevcVui& vui) {
  vui.video_signal_present = bits.Flag();
  if (!vui.video_signal_present) {
    return;
  }
  bits.Skip(3);
  vui.full_range = bits.Flag();
  if (bits.Flag()) {
    vui.colour_primaries = static_cast<std::uint8_t>(bits.Bits(8));
    vui.transfer_characteristics = static_cast<std::uint8_t>(bits.Bits(8));
    vui.matrix_coefficients = static_cast<std::uint8_t>(bits.Bits(8));
  }
}

void ReadTiming(BitstreamReader& bits, HevcVui& vui) {
  if (!bits.Flag()) {
    return;
  }
  const std::uint32_t units = bits.Bits(32);
  const std::uint32_t scale = bits.Bits(32);
  if (units > 0 && scale > 0) {
    vui.frame_rate = penumbra::Rational::Create(
        static_cast<std::int64_t>(scale), static_cast<std::int64_t>(units));
  }
}

void ReadVui(BitstreamReader& bits, HevcVui& vui) {
  ReadAspectRatio(bits, vui);
  if (bits.Flag()) {
    bits.Skip(1);
  }
  ReadVideoSignal(bits, vui);
  if (bits.Flag()) {
    static_cast<void>(bits.Ue());
    static_cast<void>(bits.Ue());
  }
  bits.Skip(3);
  if (bits.Flag()) {
    for (int i = 0; i < 4; ++i) {
      static_cast<void>(bits.Ue());
    }
  }
  ReadTiming(bits, vui);
}

void ApplyConformanceWindow(BitstreamReader& bits, HevcSps& sps,
                            const CropRect& raw) {
  const int array_type = sps.separate_colour_plane ? 0 : sps.chroma_format_idc;
  const std::int64_t unit_x = (array_type == 1 || array_type == 2) ? 2 : 1;
  const std::int64_t unit_y = array_type == 1 ? 2 : 1;
  const std::int64_t horizontal = (raw.left + raw.right) * unit_x;
  const std::int64_t vertical = (raw.top + raw.bottom) * unit_y;
  if (horizontal >= sps.coded_width || vertical >= sps.coded_height) {
    bits.Fail("conformance window removes the whole picture");
    return;
  }
  sps.crop = {.left = static_cast<std::uint32_t>(raw.left * unit_x),
              .right = static_cast<std::uint32_t>(raw.right * unit_x),
              .top = static_cast<std::uint32_t>(raw.top * unit_y),
              .bottom = static_cast<std::uint32_t>(raw.bottom * unit_y)};
  sps.visible_width = static_cast<std::int32_t>(sps.coded_width - horizontal);
  sps.visible_height = static_cast<std::int32_t>(sps.coded_height - vertical);
}

void ReadSpsSize(BitstreamReader& bits, HevcSps& sps) {
  sps.chroma_format_idc = static_cast<std::uint8_t>(bits.UeAtMost(3));
  if (sps.chroma_format_idc == 3) {
    sps.separate_colour_plane = bits.Flag();
  }
  const std::uint32_t width = bits.UeAtMost(kMaxDimension);
  const std::uint32_t height = bits.UeAtMost(kMaxDimension);
  if (width == 0 || height == 0) {
    bits.Fail("empty picture");
  }
  sps.coded_width = static_cast<std::int32_t>(width);
  sps.coded_height = static_cast<std::int32_t>(height);
  sps.visible_width = sps.coded_width;
  sps.visible_height = sps.coded_height;
  if (bits.Flag()) {
    CropRect raw;
    raw.left = bits.UeAtMost(kMaxDimension);
    raw.right = bits.UeAtMost(kMaxDimension);
    raw.top = bits.UeAtMost(kMaxDimension);
    raw.bottom = bits.UeAtMost(kMaxDimension);
    ApplyConformanceWindow(bits, sps, raw);
  }
  sps.bit_depth_luma = static_cast<std::uint8_t>(8 + bits.UeAtMost(8));
  sps.bit_depth_chroma = static_cast<std::uint8_t>(8 + bits.UeAtMost(8));
}

void SkipSubLayerOrdering(BitstreamReader& bits, std::uint32_t sub_layers) {
  const bool present = bits.Flag();
  for (std::uint32_t i = present ? 0 : sub_layers - 1; i < sub_layers; ++i) {
    for (int field = 0; field < 3; ++field) {
      static_cast<void>(bits.Ue());
    }
  }
}

void SkipPcm(BitstreamReader& bits) {
  if (!bits.Flag()) {
    return;
  }
  bits.Skip(8);
  static_cast<void>(bits.Ue());
  static_cast<void>(bits.Ue());
  bits.Skip(1);
}

std::uint32_t SkipCodingTools(BitstreamReader& bits, std::uint32_t sub_layers) {
  const std::uint32_t poc_bits = bits.UeAtMost(12) + 4;
  SkipSubLayerOrdering(bits, sub_layers);
  for (int field = 0; field < 6; ++field) {
    static_cast<void>(bits.Ue());
  }
  if (bits.Flag() && bits.Flag()) {
    SkipScalingListData(bits);
  }
  bits.Skip(2);
  SkipPcm(bits);
  return poc_bits;
}

void SkipReferenceStructure(BitstreamReader& bits, std::uint32_t poc_bits) {
  SkipShortTermRefPicSets(bits, bits.UeAtMost(kMaxShortTermSets));
  if (bits.Flag()) {
    const std::uint32_t count = bits.UeAtMost(kMaxLongTermPics);
    for (std::uint32_t i = 0; i < count; ++i) {
      bits.Skip(static_cast<std::size_t>(poc_bits) + 1);
    }
  }
  bits.Skip(2);
}

HevcCodec CodecFrom(const HevcProfileTierLevel& ptl,
                    std::string_view sample_entry) {
  return {.sample_entry = std::string(sample_entry),
          .profile_space = ptl.profile_space,
          .profile_idc = ptl.profile_idc,
          .compatibility_flags = ptl.compatibility_flags,
          .high_tier = ptl.high_tier,
          .level_idc = ptl.level_idc,
          .constraint_flags = ptl.constraint_flags};
}

absl::Status ReadArrays(RecordReader& reader, HevcConfig& config) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t count, reader.U8());
  for (std::uint8_t i = 0; i < count; ++i) {
    HevcNalArray array;
    ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t header, reader.U8());
    array.array_completeness = (header & 0x80U) != 0;
    array.nal_unit_type = header & 0x3fU;
    ALUMBRA_ASSIGN_OR_RETURN(const std::uint16_t units, reader.U16());
    for (std::uint16_t j = 0; j < units; ++j) {
      ALUMBRA_ASSIGN_OR_RETURN(const std::uint16_t size, reader.U16());
      if (size == 0) {
        return Corrupt("hvcC contains an empty NAL unit");
      }
      ALUMBRA_ASSIGN_OR_RETURN(const Bytes unit, reader.Take(size));
      array.units.emplace_back(unit.begin(), unit.end());
    }
    config.arrays.push_back(std::move(array));
  }
  return absl::OkStatus();
}

void AppendBig(std::vector<std::uint8_t>& output, std::uint64_t value,
               int bytes) {
  for (int i = bytes - 1; i >= 0; --i) {
    output.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
  }
}

absl::Status AppendArrays(const HevcConfig& config,
                          std::vector<std::uint8_t>& output) {
  if (config.arrays.size() > kMaxArrayCount) {
    return absl::InvalidArgumentError("hvcC array count");
  }
  output.push_back(static_cast<std::uint8_t>(config.arrays.size()));
  for (const HevcNalArray& array : config.arrays) {
    if (array.units.size() > kMaxUnitsPerArray || array.nal_unit_type > 63) {
      return absl::InvalidArgumentError("hvcC array contents");
    }
    output.push_back(static_cast<std::uint8_t>(
        (array.array_completeness ? 0x80U : 0U) | array.nal_unit_type));
    AppendBig(output, array.units.size(), 2);
    for (const std::vector<std::uint8_t>& unit : array.units) {
      if (unit.empty() || unit.size() > 0xffff) {
        return absl::InvalidArgumentError("hvcC NAL unit size");
      }
      AppendBig(output, unit.size(), 2);
      output.insert(output.end(), unit.begin(), unit.end());
    }
  }
  return absl::OkStatus();
}

bool FieldsInRange(const HevcConfig& config) {
  const HevcProfileTierLevel& ptl = config.profile;
  return ptl.profile_space <= 3 && ptl.profile_idc <= 31 &&
         config.num_temporal_layers <= 7 && config.nal_length_size >= 1 &&
         config.nal_length_size <= 4 &&
         config.min_spatial_segmentation_idc <= 0xfff &&
         config.parallelism_type <= 3 && config.chroma_format <= 3 &&
         config.bit_depth_luma >= 8 && config.bit_depth_luma <= 16 &&
         config.bit_depth_chroma >= 8 && config.bit_depth_chroma <= 16 &&
         config.constant_frame_rate <= 3;
}

bool HasUnit(const HevcNalArray& array, Bytes unit) {
  return std::ranges::any_of(array.units,
                             [unit](const std::vector<std::uint8_t>& known) {
                               return std::ranges::equal(known, unit);
                             });
}

void AddUnique(HevcNalArray& array, Bytes unit) {
  if (!HasUnit(array, unit)) {
    array.units.emplace_back(unit.begin(), unit.end());
  }
}

}

absl::StatusOr<HevcVps> ParseHevcVps(Bytes nal_unit) {
  ALUMBRA_RETURN_IF_ERROR(CheckNalHeader(nal_unit, kHevcVpsNalType));
  const std::vector<std::uint8_t> payload =
      RemoveEmulationPrevention(nal_unit.subspan(2));
  BitstreamReader bits(payload, "HEVC VPS");
  HevcVps vps;
  vps.vps_id = static_cast<std::uint8_t>(bits.Bits(4));
  bits.Skip(8);
  const std::uint32_t layers = bits.BitsAtMost(3, kMaxSubLayersMinus1);
  vps.max_sub_layers = static_cast<std::uint8_t>(layers + 1);
  bits.Skip(17);
  vps.profile = ReadProfileTierLevel(bits, layers);
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  return vps;
}

absl::StatusOr<HevcSps> ParseHevcSps(Bytes nal_unit) {
  ALUMBRA_RETURN_IF_ERROR(CheckNalHeader(nal_unit, kHevcSpsNalType));
  const std::vector<std::uint8_t> payload =
      RemoveEmulationPrevention(nal_unit.subspan(2));
  BitstreamReader bits(payload, "HEVC SPS");
  HevcSps sps;
  sps.vps_id = static_cast<std::uint8_t>(bits.Bits(4));
  const std::uint32_t layers = bits.BitsAtMost(3, kMaxSubLayersMinus1);
  sps.max_sub_layers = static_cast<std::uint8_t>(layers + 1);
  sps.temporal_id_nesting = bits.Flag();
  sps.profile = ReadProfileTierLevel(bits, layers);
  sps.sps_id = bits.UeAtMost(15);
  ReadSpsSize(bits, sps);
  const std::uint32_t poc_bits = SkipCodingTools(bits, sps.max_sub_layers);
  SkipReferenceStructure(bits, poc_bits);
  if (bits.Flag()) {
    ReadVui(bits, sps.vui);
  }
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  return sps;
}

absl::StatusOr<HevcConfig> ParseHvcC(Bytes record) {
  RecordReader reader(record, "hvcC");
  HevcConfig config;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t version, reader.U8());
  if (version != 1) {
    return Unsupported(
        absl::StrCat("hvcC version ", static_cast<int>(version)));
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t first, reader.U8());
  config.profile.profile_space = first >> 6U;
  config.profile.high_tier = (first & 0x20U) != 0;
  config.profile.profile_idc = first & 0x1fU;
  ALUMBRA_ASSIGN_OR_RETURN(config.profile.compatibility_flags, reader.U32());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint64_t constraints, reader.Unsigned(6));
  for (std::size_t i = 0; i < config.profile.constraint_flags.size(); ++i) {
    config.profile.constraint_flags.at(i) =
        static_cast<std::uint8_t>(constraints >> (40U - (8U * i)));
  }
  ALUMBRA_ASSIGN_OR_RETURN(config.profile.level_idc, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint16_t segmentation, reader.U16());
  config.min_spatial_segmentation_idc = segmentation & 0x0fffU;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t parallelism, reader.U8());
  config.parallelism_type = parallelism & 3U;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t chroma, reader.U8());
  config.chroma_format = chroma & 3U;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t luma, reader.U8());
  config.bit_depth_luma = static_cast<std::uint8_t>((luma & 7U) + 8);
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t chroma_depth, reader.U8());
  config.bit_depth_chroma = static_cast<std::uint8_t>((chroma_depth & 7U) + 8);
  ALUMBRA_ASSIGN_OR_RETURN(config.average_frame_rate, reader.U16());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t flags, reader.U8());
  config.constant_frame_rate = flags >> 6U;
  config.num_temporal_layers = (flags >> 3U) & 7U;
  config.temporal_id_nested = (flags & 4U) != 0;
  config.nal_length_size = static_cast<std::uint8_t>((flags & 3U) + 1);
  ALUMBRA_RETURN_IF_ERROR(ReadArrays(reader, config));
  return config;
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeHvcC(
    const HevcConfig& config) {
  if (!FieldsInRange(config)) {
    return absl::InvalidArgumentError("hvcC field out of range");
  }
  const HevcProfileTierLevel& ptl = config.profile;
  std::vector<std::uint8_t> output{1};
  output.push_back(static_cast<std::uint8_t>((ptl.profile_space << 6U) |
                                             (ptl.high_tier ? 0x20U : 0U) |
                                             ptl.profile_idc));
  AppendBig(output, ptl.compatibility_flags, 4);
  output.insert(output.end(), ptl.constraint_flags.begin(),
                ptl.constraint_flags.end());
  output.push_back(ptl.level_idc);
  AppendBig(output, 0xf000U | config.min_spatial_segmentation_idc, 2);
  output.push_back(static_cast<std::uint8_t>(0xfcU | config.parallelism_type));
  output.push_back(static_cast<std::uint8_t>(0xfcU | config.chroma_format));
  output.push_back(
      static_cast<std::uint8_t>(0xf8U | (config.bit_depth_luma - 8U)));
  output.push_back(
      static_cast<std::uint8_t>(0xf8U | (config.bit_depth_chroma - 8U)));
  AppendBig(output, config.average_frame_rate, 2);
  output.push_back(static_cast<std::uint8_t>(
      (config.constant_frame_rate << 6U) | (config.num_temporal_layers << 3U) |
      (config.temporal_id_nested ? 4U : 0U) | (config.nal_length_size - 1U)));
  ALUMBRA_RETURN_IF_ERROR(AppendArrays(config, output));
  return output;
}

absl::StatusOr<HevcConfig> HevcConfigFromAnnexB(Bytes data) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<Bytes> units, SplitAnnexB(data));
  HevcNalArray vps{.array_completeness = true,
                   .nal_unit_type = kHevcVpsNalType};
  HevcNalArray sps{.array_completeness = true,
                   .nal_unit_type = kHevcSpsNalType};
  HevcNalArray pps{.array_completeness = true,
                   .nal_unit_type = kHevcPpsNalType};
  for (const Bytes unit : units) {
    const std::uint8_t type = (unit.front() >> 1U) & 0x3fU;
    if (type == kHevcVpsNalType) {
      AddUnique(vps, unit);
    } else if (type == kHevcSpsNalType) {
      AddUnique(sps, unit);
    } else if (type == kHevcPpsNalType) {
      AddUnique(pps, unit);
    }
  }
  if (sps.units.empty()) {
    return Corrupt("no HEVC SPS available");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const HevcSps first,
                           ParseHevcSps(sps.units.front()));
  HevcConfig config;
  config.profile = first.profile;
  config.chroma_format = first.chroma_format_idc;
  config.bit_depth_luma = first.bit_depth_luma;
  config.bit_depth_chroma = first.bit_depth_chroma;
  config.num_temporal_layers = first.max_sub_layers;
  config.temporal_id_nested = first.temporal_id_nesting;
  for (HevcNalArray* array : {&vps, &sps, &pps}) {
    if (!array->units.empty()) {
      config.arrays.push_back(std::move(*array));
    }
  }
  return config;
}

HevcCodec ToCodec(const HevcConfig& config, std::string_view sample_entry) {
  return CodecFrom(config.profile, sample_entry);
}

HevcCodec ToCodec(const HevcSps& sps, std::string_view sample_entry) {
  return CodecFrom(sps.profile, sample_entry);
}

penumbra::VideoInfo ToVideoInfo(const HevcSps& sps) {
  const penumbra::ColorSpace color{.primaries = sps.vui.colour_primaries,
                                   .transfer = sps.vui.transfer_characteristics,
                                   .matrix = sps.vui.matrix_coefficients,
                                   .full_range = sps.vui.full_range};
  penumbra::VideoInfo info =
      MakeVideoInfo(sps.coded_width, sps.coded_height, sps.visible_width,
                    sps.visible_height, sps.vui.sample_aspect_ratio, color);
  if (sps.vui.frame_rate.has_value()) {
    info.frame_rate = *sps.vui.frame_rate;
  }
  return info;
}

}
