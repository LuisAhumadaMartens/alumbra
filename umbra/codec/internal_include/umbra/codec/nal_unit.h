#ifndef UMBRA_CODEC_NAL_UNIT_H_
#define UMBRA_CODEC_NAL_UNIT_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

[[nodiscard]] std::vector<std::uint8_t> RemoveEmulationPrevention(Bytes data);

[[nodiscard]] absl::StatusOr<std::vector<Bytes>> SplitAnnexB(Bytes data);

[[nodiscard]] absl::StatusOr<std::vector<Bytes>> SplitLengthPrefixed(
    Bytes data, std::size_t length_size);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> AnnexBToLengthPrefixed(
    Bytes data, std::size_t length_size);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> LengthPrefixedToAnnexB(
    Bytes data, std::size_t length_size);

}

#endif
