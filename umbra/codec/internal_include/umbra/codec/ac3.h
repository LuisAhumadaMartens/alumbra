#ifndef UMBRA_CODEC_AC3_H_
#define UMBRA_CODEC_AC3_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

struct Ac3Config {
  std::uint8_t fscod = 0;
  std::uint8_t bsid = 0;
  std::uint8_t bsmod = 0;
  std::uint8_t acmod = 0;
  bool lfeon = false;
  std::uint8_t bit_rate_code = 0;

  friend bool operator==(const Ac3Config&, const Ac3Config&) = default;
};

struct Eac3Substream {
  std::uint8_t fscod = 0;
  std::uint8_t bsid = 0;
  bool asvc = false;
  std::uint8_t bsmod = 0;
  std::uint8_t acmod = 0;
  bool lfeon = false;
  std::uint8_t num_dependent_substreams = 0;
  std::uint16_t chan_loc = 0;

  friend bool operator==(const Eac3Substream&, const Eac3Substream&) = default;
};

struct Eac3Config {
  std::uint16_t data_rate = 0;
  std::vector<Eac3Substream> substreams;
  bool joc = false;
  std::uint8_t joc_complexity_index = 0;

  friend bool operator==(const Eac3Config&, const Eac3Config&) = default;
};

[[nodiscard]] absl::StatusOr<Ac3Config> ParseDac3(Bytes data);

[[nodiscard]] absl::StatusOr<Eac3Config> ParseDec3(Bytes data);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeDac3(
    const Ac3Config& config);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeDec3(
    const Eac3Config& config);

[[nodiscard]] Ac3Codec ToCodec(const Ac3Config& config);

[[nodiscard]] Eac3Codec ToCodec(const Eac3Config& config);

[[nodiscard]] penumbra::AudioInfo ToAudioInfo(const Ac3Config& config);

[[nodiscard]] penumbra::AudioInfo ToAudioInfo(const Eac3Config& config);

}

#endif
