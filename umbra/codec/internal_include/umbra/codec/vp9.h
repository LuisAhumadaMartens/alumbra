#ifndef UMBRA_CODEC_VP9_H_
#define UMBRA_CODEC_VP9_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

struct Vp9Config {
  std::uint8_t profile = 0;
  std::uint8_t level = 0;
  std::uint8_t bit_depth = 8;
  std::uint8_t chroma_subsampling = 1;
  bool full_range = false;
  std::uint8_t colour_primaries = 1;
  std::uint8_t transfer_characteristics = 1;
  std::uint8_t matrix_coefficients = 1;

  friend bool operator==(const Vp9Config&, const Vp9Config&) = default;
};

[[nodiscard]] absl::StatusOr<Vp9Config> ParseVpcC(Bytes full_box_payload);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeVpcC(
    const Vp9Config& config);

[[nodiscard]] Vp9Codec ToCodec(const Vp9Config& config);

[[nodiscard]] penumbra::ColorSpace ToColorSpace(const Vp9Config& config);

}

#endif
