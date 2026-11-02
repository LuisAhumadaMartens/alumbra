#include "umbra/codec/avc.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
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
constexpr std::uint32_t kMaxSizeInMacroblocks = 1023;
constexpr std::uint32_t kMaxCrop = 65535;
constexpr std::size_t kMaxSpsCount = 31;
constexpr std::size_t kMaxPpsCount = 255;
constexpr std::int32_t kMaxScalingDelta = 127;
constexpr std::uint8_t kNalTypeMask = 0x1f;

void SkipScalingList(BitstreamReader& bits, int size) {
  int last = 8;
  int next = 8;
  for (int i = 0; i < size && bits.Ok(); ++i) {
    if (next != 0) {
      const std::int32_t delta = bits.Se();
      if (delta < -128 || delta > kMaxScalingDelta) {
        bits.Fail("scaling list delta is out of range");
      }
      next = (last + delta + 256) % 256;
    }
    last = next == 0 ? last : next;
  }
}

void ReadChromaInfo(BitstreamReader& bits, H264Sps& sps) {
  sps.chroma_format_idc = static_cast<std::uint8_t>(bits.UeAtMost(3));
  if (sps.chroma_format_idc == 3) {
    sps.separate_colour_plane = bits.Flag();
  }
  sps.bit_depth_luma = static_cast<std::uint8_t>(8 + bits.UeAtMost(6));
  sps.bit_depth_chroma = static_cast<std::uint8_t>(8 + bits.UeAtMost(6));
  bits.Skip(1);
  if (!bits.Flag()) {
    return;
  }
  const int lists = sps.chroma_format_idc == 3 ? 12 : 8;
  for (int i = 0; i < lists; ++i) {
    if (bits.Flag()) {
      SkipScalingList(bits, i < 6 ? 16 : 64);
    }
  }
}

void ReadPictureOrderCount(BitstreamReader& bits) {
  static_cast<void>(bits.UeAtMost(12));
  const std::uint32_t type = bits.UeAtMost(2);
  if (type == 0) {
    static_cast<void>(bits.UeAtMost(12));
  } else if (type == 1) {
    bits.Skip(1);
    static_cast<void>(bits.Se());
    static_cast<void>(bits.Se());
    const std::uint32_t cycle = bits.UeAtMost(255);
    for (std::uint32_t i = 0; i < cycle; ++i) {
      static_cast<void>(bits.Se());
    }
  }
}

std::int64_t CropUnit(const H264Sps& sps, bool horizontal) {
  const int array_type = sps.separate_colour_plane ? 0 : sps.chroma_format_idc;
  const std::int64_t field_factor = sps.frame_mbs_only ? 1 : 2;
  if (array_type == 0) {
    return horizontal ? 1 : field_factor;
  }
  if (horizontal) {
    return array_type == 3 ? 1 : 2;
  }
  return (array_type == 1 ? 2 : 1) * field_factor;
}

void ApplyCrop(BitstreamReader& bits, H264Sps& sps, const CropRect& raw) {
  const std::int64_t unit_x = CropUnit(sps, true);
  const std::int64_t unit_y = CropUnit(sps, false);
  const std::int64_t horizontal = (raw.left + raw.right) * unit_x;
  const std::int64_t vertical = (raw.top + raw.bottom) * unit_y;
  if (horizontal >= sps.coded_width || vertical >= sps.coded_height) {
    bits.Fail("cropping removes the whole picture");
    return;
  }
  sps.crop = {.left = static_cast<std::uint32_t>(raw.left * unit_x),
              .right = static_cast<std::uint32_t>(raw.right * unit_x),
              .top = static_cast<std::uint32_t>(raw.top * unit_y),
              .bottom = static_cast<std::uint32_t>(raw.bottom * unit_y)};
  sps.visible_width = static_cast<std::int32_t>(sps.coded_width - horizontal);
  sps.visible_height = static_cast<std::int32_t>(sps.coded_height - vertical);
}

void ReadGeometry(BitstreamReader& bits, H264Sps& sps) {
  sps.max_num_ref_frames = bits.Ue();
  bits.Skip(1);
  const std::uint32_t width = bits.UeAtMost(kMaxSizeInMacroblocks);
  const std::uint32_t height = bits.UeAtMost(kMaxSizeInMacroblocks);
  sps.frame_mbs_only = bits.Flag();
  if (!sps.frame_mbs_only) {
    bits.Skip(1);
  }
  bits.Skip(1);
  const std::uint32_t field_factor = sps.frame_mbs_only ? 1 : 2;
  sps.coded_width = static_cast<std::int32_t>((width + 1) * 16);
  sps.coded_height =
      static_cast<std::int32_t>((height + 1) * 16 * field_factor);
  sps.visible_width = sps.coded_width;
  sps.visible_height = sps.coded_height;
  if (!bits.Flag()) {
    return;
  }
  CropRect raw;
  raw.left = bits.UeAtMost(kMaxCrop);
  raw.right = bits.UeAtMost(kMaxCrop);
  raw.top = bits.UeAtMost(kMaxCrop);
  raw.bottom = bits.UeAtMost(kMaxCrop);
  ApplyCrop(bits, sps, raw);
}

void ReadAspectRatio(BitstreamReader& bits, H264Vui& vui) {
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

void ReadVideoSignal(BitstreamReader& bits, H264Vui& vui) {
  vui.video_signal_present = bits.Flag();
  if (!vui.video_signal_present) {
    return;
  }
  bits.Skip(3);
  vui.full_range = bits.Flag();
  vui.colour_description_present = bits.Flag();
  if (vui.colour_description_present) {
    vui.colour_primaries = static_cast<std::uint8_t>(bits.Bits(8));
    vui.transfer_characteristics = static_cast<std::uint8_t>(bits.Bits(8));
    vui.matrix_coefficients = static_cast<std::uint8_t>(bits.Bits(8));
  }
}

void ReadTiming(BitstreamReader& bits, H264Vui& vui) {
  if (!bits.Flag()) {
    return;
  }
  const std::uint32_t units = bits.Bits(32);
  const std::uint32_t scale = bits.Bits(32);
  if (units > 0 && scale > 0) {
    vui.frame_rate = penumbra::Rational::Create(
        static_cast<std::int64_t>(scale), 2 * static_cast<std::int64_t>(units));
  }
}

void ReadVui(BitstreamReader& bits, H264Vui& vui) {
  ReadAspectRatio(bits, vui);
  if (bits.Flag()) {
    bits.Skip(1);
  }
  ReadVideoSignal(bits, vui);
  if (bits.Flag()) {
    static_cast<void>(bits.Ue());
    static_cast<void>(bits.Ue());
  }
  ReadTiming(bits, vui);
}

absl::StatusOr<std::vector<std::uint8_t>> ReadSpsPayload(Bytes nal_unit) {
  if (nal_unit.size() < 4) {
    return Corrupt("H.264 SPS is too short");
  }
  if ((nal_unit[0] & 0x80U) != 0 ||
      (nal_unit[0] & kNalTypeMask) != kH264SpsNalType) {
    return Corrupt("NAL unit is not an H.264 SPS");
  }
  return RemoveEmulationPrevention(nal_unit.subspan(1));
}

absl::StatusOr<std::vector<std::uint8_t>> ReadSizedUnit(RecordReader& reader) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint16_t size, reader.U16());
  if (size == 0) {
    return Corrupt("avcC contains an empty parameter set");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const Bytes unit, reader.Take(size));
  return std::vector<std::uint8_t>(unit.begin(), unit.end());
}

absl::Status ReadUnits(RecordReader& reader, std::size_t count,
                       std::vector<std::vector<std::uint8_t>>& units) {
  for (std::size_t i = 0; i < count; ++i) {
    ALUMBRA_ASSIGN_OR_RETURN(std::vector<std::uint8_t> unit,
                             ReadSizedUnit(reader));
    units.push_back(std::move(unit));
  }
  return absl::OkStatus();
}

absl::Status ReadAvcExtension(RecordReader& reader, AvcConfig& config) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t chroma, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t luma, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t chroma_depth, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t count, reader.U8());
  config.chroma_format = chroma & 3U;
  config.bit_depth_luma = static_cast<std::uint8_t>((luma & 7U) + 8);
  config.bit_depth_chroma = static_cast<std::uint8_t>((chroma_depth & 7U) + 8);
  return ReadUnits(reader, count, config.sps_ext);
}

absl::Status AppendUnits(const std::vector<std::vector<std::uint8_t>>& units,
                         std::vector<std::uint8_t>& output) {
  for (const std::vector<std::uint8_t>& unit : units) {
    if (unit.empty() || unit.size() > 0xffff) {
      return absl::InvalidArgumentError("avcC parameter set size");
    }
    output.push_back(static_cast<std::uint8_t>(unit.size() >> 8U));
    output.push_back(static_cast<std::uint8_t>(unit.size()));
    output.insert(output.end(), unit.begin(), unit.end());
  }
  return absl::OkStatus();
}

bool HasUnit(const std::vector<Bytes>& list, Bytes unit) {
  return std::ranges::any_of(
      list, [unit](Bytes known) { return std::ranges::equal(known, unit); });
}

bool ExtensionComplete(const AvcConfig& config) {
  return H264ProfileHasChromaInfo(config.profile_idc) &&
         config.chroma_format.has_value() &&
         config.bit_depth_luma.has_value() &&
         config.bit_depth_chroma.has_value();
}

}

bool H264ProfileHasChromaInfo(std::uint8_t profile_idc) {
  switch (profile_idc) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
      return true;
    default:
      return false;
  }
}

absl::StatusOr<H264Sps> ParseH264Sps(Bytes nal_unit) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<std::uint8_t> payload,
                           ReadSpsPayload(nal_unit));
  BitstreamReader bits(payload, "H.264 SPS");
  H264Sps sps;
  sps.profile_idc = static_cast<std::uint8_t>(bits.Bits(8));
  sps.constraint_flags = static_cast<std::uint8_t>(bits.Bits(8));
  sps.level_idc = static_cast<std::uint8_t>(bits.Bits(8));
  sps.sps_id = bits.UeAtMost(31);
  if (H264ProfileHasChromaInfo(sps.profile_idc)) {
    ReadChromaInfo(bits, sps);
  }
  ReadPictureOrderCount(bits);
  ReadGeometry(bits, sps);
  if (bits.Flag()) {
    ReadVui(bits, sps.vui);
  }
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  return sps;
}

absl::StatusOr<AvcConfig> ParseAvcC(Bytes record) {
  RecordReader reader(record, "avcC");
  AvcConfig config;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t version, reader.U8());
  if (version != 1) {
    return Unsupported(
        absl::StrCat("avcC version ", static_cast<int>(version)));
  }
  ALUMBRA_ASSIGN_OR_RETURN(config.profile_idc, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.profile_compatibility, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.level_idc, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t length_flags, reader.U8());
  config.nal_length_size = static_cast<std::uint8_t>((length_flags & 3U) + 1);
  if (config.nal_length_size == 3) {
    return Corrupt("avcC NAL length size of 3 bytes");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t sps_flags, reader.U8());
  ALUMBRA_RETURN_IF_ERROR(ReadUnits(reader, sps_flags & 0x1fU, config.sps));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t pps_count, reader.U8());
  ALUMBRA_RETURN_IF_ERROR(ReadUnits(reader, pps_count, config.pps));
  if (H264ProfileHasChromaInfo(config.profile_idc) && reader.remaining() >= 4) {
    ALUMBRA_RETURN_IF_ERROR(ReadAvcExtension(reader, config));
  }
  return config;
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeAvcC(
    const AvcConfig& config) {
  if (config.nal_length_size != 1 && config.nal_length_size != 2 &&
      config.nal_length_size != 4) {
    return absl::InvalidArgumentError("avcC NAL length size");
  }
  if (config.sps.size() > kMaxSpsCount || config.pps.size() > kMaxPpsCount ||
      config.sps_ext.size() > kMaxPpsCount) {
    return absl::InvalidArgumentError("avcC parameter set count");
  }
  std::vector<std::uint8_t> output{
      1,
      config.profile_idc,
      config.profile_compatibility,
      config.level_idc,
      static_cast<std::uint8_t>(0xfcU | (config.nal_length_size - 1U)),
      static_cast<std::uint8_t>(0xe0U | config.sps.size())};
  ALUMBRA_RETURN_IF_ERROR(AppendUnits(config.sps, output));
  output.push_back(static_cast<std::uint8_t>(config.pps.size()));
  ALUMBRA_RETURN_IF_ERROR(AppendUnits(config.pps, output));
  if (ExtensionComplete(config)) {
    output.push_back(
        static_cast<std::uint8_t>(0xfcU | config.chroma_format.value_or(1)));
    output.push_back(static_cast<std::uint8_t>(
        0xf8U | (config.bit_depth_luma.value_or(8) - 8U)));
    output.push_back(static_cast<std::uint8_t>(
        0xf8U | (config.bit_depth_chroma.value_or(8) - 8U)));
    output.push_back(static_cast<std::uint8_t>(config.sps_ext.size()));
    ALUMBRA_RETURN_IF_ERROR(AppendUnits(config.sps_ext, output));
  }
  return output;
}

absl::StatusOr<AvcConfig> AvcConfigFromParameterSets(
    const std::vector<Bytes>& sps, const std::vector<Bytes>& pps) {
  if (sps.empty()) {
    return Corrupt("no H.264 SPS available");
  }
  if (sps.size() > kMaxSpsCount || pps.size() > kMaxPpsCount) {
    return Corrupt("too many H.264 parameter sets");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const H264Sps first, ParseH264Sps(sps.front()));
  AvcConfig config;
  config.profile_idc = first.profile_idc;
  config.profile_compatibility = first.constraint_flags;
  config.level_idc = first.level_idc;
  if (H264ProfileHasChromaInfo(first.profile_idc)) {
    config.chroma_format = first.chroma_format_idc;
    config.bit_depth_luma = first.bit_depth_luma;
    config.bit_depth_chroma = first.bit_depth_chroma;
  }
  for (const Bytes unit : sps) {
    config.sps.emplace_back(unit.begin(), unit.end());
  }
  for (const Bytes unit : pps) {
    config.pps.emplace_back(unit.begin(), unit.end());
  }
  return config;
}

absl::StatusOr<AvcConfig> AvcConfigFromAnnexB(Bytes data) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<Bytes> units, SplitAnnexB(data));
  std::vector<Bytes> sps;
  std::vector<Bytes> pps;
  for (const Bytes unit : units) {
    const std::uint8_t type = unit.front() & kNalTypeMask;
    if (type == kH264SpsNalType && !HasUnit(sps, unit)) {
      sps.push_back(unit);
    } else if (type == kH264PpsNalType && !HasUnit(pps, unit)) {
      pps.push_back(unit);
    }
  }
  return AvcConfigFromParameterSets(sps, pps);
}

AvcCodec ToCodec(const AvcConfig& config) {
  return {.sample_entry = "avc1",
          .profile_idc = config.profile_idc,
          .constraint_flags = config.profile_compatibility,
          .level_idc = config.level_idc};
}

AvcCodec ToCodec(const H264Sps& sps) {
  return {.sample_entry = "avc1",
          .profile_idc = sps.profile_idc,
          .constraint_flags = sps.constraint_flags,
          .level_idc = sps.level_idc};
}

penumbra::VideoInfo ToVideoInfo(const H264Sps& sps) {
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
