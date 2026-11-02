#include "umbra/codec/av1.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/video_geometry.h"

namespace umbra::codec {
namespace {

constexpr std::uint32_t kMaxProfile = 2;
constexpr int kMaxLeb128Bytes = 8;
constexpr std::uint32_t kBt709 = 1;
constexpr std::uint32_t kSrgbTransfer = 13;
constexpr std::uint32_t kIdentityMatrix = 0;
constexpr std::uint32_t kMaxLevelWithoutTier = 7;

struct Obu {
  std::uint8_t type = 0;
  bool has_size_field = false;
  Bytes payload;
  std::size_t total_size = 0;
};

absl::StatusOr<std::uint64_t> ReadLeb128(RecordReader& reader) {
  std::uint64_t value = 0;
  for (int i = 0; i < kMaxLeb128Bytes; ++i) {
    ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t byte, reader.U8());
    value |= static_cast<std::uint64_t>(byte & 0x7fU) << (7 * i);
    if ((byte & 0x80U) == 0) {
      return value;
    }
  }
  return Corrupt("AV1 OBU size field is too long");
}

absl::StatusOr<Obu> ReadObu(Bytes data) {
  RecordReader reader(data, "AV1 OBU");
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t header, reader.U8());
  if ((header & 0x80U) != 0) {
    return Corrupt("AV1 OBU forbidden bit is set");
  }
  Obu obu;
  obu.type = (header >> 3U) & 0x0fU;
  obu.has_size_field = (header & 2U) != 0;
  if ((header & 4U) != 0) {
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(1));
  }
  std::uint64_t size = reader.remaining();
  if (obu.has_size_field) {
    ALUMBRA_ASSIGN_OR_RETURN(size, ReadLeb128(reader));
  }
  if (size > reader.remaining()) {
    return Corrupt("AV1 OBU payload is truncated");
  }
  ALUMBRA_ASSIGN_OR_RETURN(obu.payload, reader.Take(size));
  obu.total_size = reader.position();
  return obu;
}

void SkipUvlc(BitstreamReader& bits) {
  int leading_zeros = 0;
  while (bits.Ok() && !bits.Flag()) {
    ++leading_zeros;
  }
  if (leading_zeros < 32) {
    bits.Skip(static_cast<std::size_t>(leading_zeros));
  }
}

struct OperatingPointContext {
  bool decoder_model_info_present = false;
  std::size_t buffer_delay_length = 0;
};

OperatingPointContext ReadTimingInfo(BitstreamReader& bits) {
  OperatingPointContext context;
  if (!bits.Flag()) {
    return context;
  }
  bits.Skip(64);
  if (bits.Flag()) {
    SkipUvlc(bits);
  }
  context.decoder_model_info_present = bits.Flag();
  if (context.decoder_model_info_present) {
    context.buffer_delay_length = static_cast<std::size_t>(bits.Bits(5)) + 1;
    bits.Skip(32 + 5 + 5);
  }
  return context;
}

void ReadOperatingPoint(BitstreamReader& bits,
                        const OperatingPointContext& context,
                        bool display_delay, bool first,
                        Av1SequenceHeader& header) {
  bits.Skip(12);
  const std::uint32_t level = bits.Bits(5);
  const bool tier = level > kMaxLevelWithoutTier && bits.Flag();
  if (first) {
    header.level = static_cast<std::uint8_t>(level);
    header.high_tier = tier;
  }
  if (context.decoder_model_info_present && bits.Flag()) {
    bits.Skip((2 * context.buffer_delay_length) + 1);
  }
  if (display_delay && bits.Flag()) {
    bits.Skip(4);
  }
}

void ReadOperatingPoints(BitstreamReader& bits,
                         const OperatingPointContext& context,
                         Av1SequenceHeader& header) {
  const bool display_delay = bits.Flag();
  const std::uint32_t count = bits.Bits(5);
  for (std::uint32_t i = 0; i <= count && bits.Ok(); ++i) {
    ReadOperatingPoint(bits, context, display_delay, i == 0, header);
  }
}

void ReadFrameSize(BitstreamReader& bits, Av1SequenceHeader& header) {
  const int width_bits = static_cast<int>(bits.Bits(4)) + 1;
  const int height_bits = static_cast<int>(bits.Bits(4)) + 1;
  header.max_frame_width = bits.Bits(width_bits) + 1;
  header.max_frame_height = bits.Bits(height_bits) + 1;
}

void SkipInterTools(BitstreamReader& bits) {
  bits.Skip(4);
  const bool order_hint = bits.Flag();
  if (order_hint) {
    bits.Skip(2);
  }
  const bool choose_screen = bits.Flag();
  const bool force_screen = choose_screen || bits.Flag();
  if (force_screen && !bits.Flag()) {
    bits.Skip(1);
  }
  if (order_hint) {
    bits.Skip(3);
  }
}

void ReadBitDepth(BitstreamReader& bits, Av1SequenceHeader& header) {
  const bool high = bits.Flag();
  header.bit_depth = high ? 10 : 8;
  if (header.profile == 2 && high && bits.Flag()) {
    header.bit_depth = 12;
  }
}

void ReadSubsampling(BitstreamReader& bits, Av1SequenceHeader& header) {
  if (header.profile == 0) {
    header.subsampling_x = true;
    header.subsampling_y = true;
  } else if (header.profile == 1) {
    header.subsampling_x = false;
    header.subsampling_y = false;
  } else if (header.bit_depth == 12) {
    header.subsampling_x = bits.Flag();
    header.subsampling_y = header.subsampling_x && bits.Flag();
  } else {
    header.subsampling_x = true;
    header.subsampling_y = false;
  }
  if (header.subsampling_x && header.subsampling_y) {
    header.chroma_sample_position = static_cast<std::uint8_t>(bits.Bits(2));
  }
}

void ReadColourDescription(BitstreamReader& bits, Av1SequenceHeader& header) {
  header.colour_description_present = bits.Flag();
  if (header.colour_description_present) {
    header.colour_primaries = static_cast<std::uint8_t>(bits.Bits(8));
    header.transfer_characteristics = static_cast<std::uint8_t>(bits.Bits(8));
    header.matrix_coefficients = static_cast<std::uint8_t>(bits.Bits(8));
  }
}

bool IsSrgbIdentity(const Av1SequenceHeader& header) {
  return header.colour_primaries == kBt709 &&
         header.transfer_characteristics == kSrgbTransfer &&
         header.matrix_coefficients == kIdentityMatrix;
}

void ReadColorConfig(BitstreamReader& bits, Av1SequenceHeader& header) {
  ReadBitDepth(bits, header);
  header.monochrome = header.profile != 1 && bits.Flag();
  ReadColourDescription(bits, header);
  if (header.monochrome) {
    header.full_range = bits.Flag();
    header.subsampling_x = true;
    header.subsampling_y = true;
    header.chroma_sample_position = 0;
  } else if (IsSrgbIdentity(header)) {
    header.full_range = true;
    header.subsampling_x = false;
    header.subsampling_y = false;
  } else {
    header.full_range = bits.Flag();
    ReadSubsampling(bits, header);
  }
}

void ReadFlagsBeforeColour(BitstreamReader& bits, Av1SequenceHeader& header) {
  if (!header.reduced_still_picture_header) {
    if (bits.Flag()) {
      bits.Skip(7);
    }
  }
  bits.Skip(3);
  if (!header.reduced_still_picture_header) {
    SkipInterTools(bits);
  }
  bits.Skip(3);
}

absl::StatusOr<Av1SequenceHeader> ParseSequenceHeaderPayload(Bytes payload) {
  BitstreamReader bits(payload, "AV1 sequence header");
  Av1SequenceHeader header;
  const std::uint32_t profile = bits.Bits(3);
  if (profile > kMaxProfile) {
    return Unsupported(absl::StrCat("AV1 profile ", profile));
  }
  header.profile = static_cast<std::uint8_t>(profile);
  header.still_picture = bits.Flag();
  header.reduced_still_picture_header = bits.Flag();
  if (header.reduced_still_picture_header) {
    header.level = static_cast<std::uint8_t>(bits.Bits(5));
  } else {
    const OperatingPointContext context = ReadTimingInfo(bits);
    ReadOperatingPoints(bits, context, header);
  }
  ReadFrameSize(bits, header);
  ReadFlagsBeforeColour(bits, header);
  ReadColorConfig(bits, header);
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  return header;
}

std::uint8_t SamplePosition(bool subsampling_x, bool subsampling_y,
                            std::uint8_t position) {
  return subsampling_x && subsampling_y ? position
                                        : static_cast<std::uint8_t>(0);
}

std::uint8_t DefaultedColour(const Av1SequenceHeader& header,
                             std::uint8_t value) {
  return header.colour_description_present ? value
                                           : static_cast<std::uint8_t>(1);
}

bool ConfigInRange(const Av1Config& config) {
  return config.profile <= kMaxProfile && config.level <= 31 &&
         config.chroma_sample_position <= 3 &&
         (!config.twelve_bit || config.high_bitdepth) &&
         config.initial_presentation_delay_minus_one.value_or(0) <= 15;
}

}

absl::StatusOr<Av1SequenceHeader> ParseAv1SequenceHeaderObu(Bytes obu) {
  ALUMBRA_ASSIGN_OR_RETURN(const Obu parsed, ReadObu(obu));
  if (parsed.type != kAv1SequenceHeaderObu) {
    return Corrupt("OBU is not an AV1 sequence header");
  }
  return ParseSequenceHeaderPayload(parsed.payload);
}

absl::StatusOr<std::optional<Av1SequenceHeader>> FindAv1SequenceHeader(
    Bytes obus) {
  while (!obus.empty()) {
    ALUMBRA_ASSIGN_OR_RETURN(const Obu obu, ReadObu(obus));
    if (obu.type == kAv1SequenceHeaderObu) {
      ALUMBRA_ASSIGN_OR_RETURN(Av1SequenceHeader header,
                               ParseSequenceHeaderPayload(obu.payload));
      return std::optional<Av1SequenceHeader>(header);
    }
    obus = obus.subspan(obu.total_size);
  }
  return std::optional<Av1SequenceHeader>();
}

absl::StatusOr<Av1Config> ParseAv1C(Bytes record) {
  RecordReader reader(record, "av1C");
  Av1Config config;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t marker, reader.U8());
  if ((marker & 0x80U) == 0) {
    return Corrupt("av1C marker bit is not set");
  }
  if ((marker & 0x7fU) != 1) {
    return Unsupported(absl::StrCat("av1C version ", marker & 0x7fU));
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t levels, reader.U8());
  config.profile = levels >> 5U;
  config.level = levels & 0x1fU;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t flags, reader.U8());
  config.high_tier = (flags & 0x80U) != 0;
  config.high_bitdepth = (flags & 0x40U) != 0;
  config.twelve_bit = (flags & 0x20U) != 0;
  config.monochrome = (flags & 0x10U) != 0;
  config.subsampling_x = (flags & 0x08U) != 0;
  config.subsampling_y = (flags & 0x04U) != 0;
  config.chroma_sample_position = flags & 3U;
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t delay, reader.U8());
  if ((delay & 0x10U) != 0) {
    config.initial_presentation_delay_minus_one = delay & 0x0fU;
  }
  if (!ConfigInRange(config)) {
    return Corrupt("av1C field is out of range");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const Bytes rest, reader.Take(reader.remaining()));
  config.config_obus.assign(rest.begin(), rest.end());
  ALUMBRA_ASSIGN_OR_RETURN(config.sequence_header,
                           FindAv1SequenceHeader(config.config_obus));
  return config;
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeAv1C(
    const Av1Config& config) {
  if (!ConfigInRange(config)) {
    return absl::InvalidArgumentError("av1C field is out of range");
  }
  const std::uint8_t delay =
      config.initial_presentation_delay_minus_one.has_value()
          ? static_cast<std::uint8_t>(
                0x10U | *config.initial_presentation_delay_minus_one)
          : static_cast<std::uint8_t>(0);
  std::vector<std::uint8_t> output{
      0x81, static_cast<std::uint8_t>((config.profile << 5U) | config.level),
      static_cast<std::uint8_t>(
          (config.high_tier ? 0x80U : 0U) |
          (config.high_bitdepth ? 0x40U : 0U) |
          (config.twelve_bit ? 0x20U : 0U) | (config.monochrome ? 0x10U : 0U) |
          (config.subsampling_x ? 0x08U : 0U) |
          (config.subsampling_y ? 0x04U : 0U) | config.chroma_sample_position),
      delay};
  output.insert(output.end(), config.config_obus.begin(),
                config.config_obus.end());
  return output;
}

absl::StatusOr<Av1Config> Av1ConfigFromSequenceHeaderObu(Bytes obu) {
  ALUMBRA_ASSIGN_OR_RETURN(const Obu parsed, ReadObu(obu));
  if (!parsed.has_size_field || parsed.total_size != obu.size()) {
    return Corrupt("AV1 configuration OBU must be a single sized OBU");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const Av1SequenceHeader header,
                           ParseAv1SequenceHeaderObu(obu));
  Av1Config config;
  config.profile = header.profile;
  config.level = header.level;
  config.high_tier = header.high_tier;
  config.high_bitdepth = header.bit_depth > 8;
  config.twelve_bit = header.bit_depth == 12;
  config.monochrome = header.monochrome;
  config.subsampling_x = header.subsampling_x;
  config.subsampling_y = header.subsampling_y;
  config.chroma_sample_position = header.chroma_sample_position;
  config.config_obus.assign(obu.begin(), obu.end());
  config.sequence_header = header;
  return config;
}

std::uint8_t BitDepthOf(const Av1Config& config) {
  if (config.twelve_bit) {
    return 12;
  }
  return config.high_bitdepth ? 10 : 8;
}

Av1Codec ToCodec(const Av1Config& config) {
  Av1Codec codec;
  codec.profile = config.profile;
  codec.level = config.level;
  codec.high_tier = config.high_tier;
  codec.bit_depth = BitDepthOf(config);
  codec.monochrome = config.monochrome;
  codec.subsampling_x = config.subsampling_x;
  codec.subsampling_y = config.subsampling_y;
  codec.chroma_sample_position =
      SamplePosition(config.subsampling_x, config.subsampling_y,
                     config.chroma_sample_position);
  if (config.sequence_header.has_value()) {
    const Av1SequenceHeader& header = *config.sequence_header;
    codec.colour_primaries = DefaultedColour(header, header.colour_primaries);
    codec.transfer_characteristics =
        DefaultedColour(header, header.transfer_characteristics);
    codec.matrix_coefficients =
        DefaultedColour(header, header.matrix_coefficients);
    codec.full_range = header.full_range;
  }
  return codec;
}

Av1Codec ToCodec(const Av1SequenceHeader& header) {
  return {.profile = header.profile,
          .level = header.level,
          .high_tier = header.high_tier,
          .bit_depth = header.bit_depth,
          .monochrome = header.monochrome,
          .subsampling_x = header.subsampling_x,
          .subsampling_y = header.subsampling_y,
          .chroma_sample_position =
              SamplePosition(header.subsampling_x, header.subsampling_y,
                             header.chroma_sample_position),
          .colour_primaries = DefaultedColour(header, header.colour_primaries),
          .transfer_characteristics =
              DefaultedColour(header, header.transfer_characteristics),
          .matrix_coefficients =
              DefaultedColour(header, header.matrix_coefficients),
          .full_range = header.full_range};
}

penumbra::VideoInfo ToVideoInfo(const Av1SequenceHeader& header) {
  const penumbra::ColorSpace color{.primaries = header.colour_primaries,
                                   .transfer = header.transfer_characteristics,
                                   .matrix = header.matrix_coefficients,
                                   .full_range = header.full_range};
  const auto width = static_cast<std::int32_t>(header.max_frame_width);
  const auto height = static_cast<std::int32_t>(header.max_frame_height);
  return MakeVideoInfo(width, height, width, height,
                       penumbra::Rational::Of(1, 1), color);
}

}
