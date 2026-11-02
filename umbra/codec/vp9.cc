#include "umbra/codec/vp9.h"

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

bool ValidBitDepth(std::uint8_t depth) {
  return depth == 8 || depth == 10 || depth == 12;
}

absl::Status Validate(const Vp9Config& config) {
  if (config.profile > 3 || !ValidBitDepth(config.bit_depth) ||
      config.chroma_subsampling > 3) {
    return Corrupt("vpcC field is out of range");
  }
  return absl::OkStatus();
}

}

absl::StatusOr<Vp9Config> ParseVpcC(Bytes full_box_payload) {
  RecordReader reader(full_box_payload, "vpcC");
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t header, reader.U32());
  const std::uint32_t version = header >> 24U;
  if (version != 1) {
    return Unsupported(absl::StrCat("vpcC version ", version));
  }
  Vp9Config config;
  ALUMBRA_ASSIGN_OR_RETURN(config.profile, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.level, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t packed, reader.U8());
  config.bit_depth = packed >> 4U;
  config.chroma_subsampling = (packed >> 1U) & 7U;
  config.full_range = (packed & 1U) != 0;
  ALUMBRA_ASSIGN_OR_RETURN(config.colour_primaries, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.transfer_characteristics, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.matrix_coefficients, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint16_t init_size, reader.U16());
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(init_size));
  ALUMBRA_RETURN_IF_ERROR(Validate(config));
  return config;
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeVpcC(
    const Vp9Config& config) {
  if (config.profile > 3 || !ValidBitDepth(config.bit_depth) ||
      config.chroma_subsampling > 3) {
    return absl::InvalidArgumentError("vpcC field is out of range");
  }
  return std::vector<std::uint8_t>{
      1,
      0,
      0,
      0,
      config.profile,
      config.level,
      static_cast<std::uint8_t>((config.bit_depth << 4U) |
                                (config.chroma_subsampling << 1U) |
                                (config.full_range ? 1U : 0U)),
      config.colour_primaries,
      config.transfer_characteristics,
      config.matrix_coefficients,
      0,
      0};
}

Vp9Codec ToCodec(const Vp9Config& config) {
  return {.profile = config.profile,
          .level = config.level,
          .bit_depth = config.bit_depth,
          .has_optional_fields = true,
          .chroma_subsampling = config.chroma_subsampling,
          .colour_primaries = config.colour_primaries,
          .transfer_characteristics = config.transfer_characteristics,
          .matrix_coefficients = config.matrix_coefficients,
          .full_range = config.full_range};
}

penumbra::ColorSpace ToColorSpace(const Vp9Config& config) {
  return {.primaries = config.colour_primaries,
          .transfer = config.transfer_characteristics,
          .matrix = config.matrix_coefficients,
          .full_range = config.full_range};
}

}
