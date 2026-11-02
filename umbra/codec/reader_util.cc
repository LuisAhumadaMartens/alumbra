#include "umbra/codec/reader_util.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/error_catalogue.h"

namespace umbra::codec {
namespace {

std::string Describe(base::ErrorCode code) {
  for (const base::ErrorCatalogueEntry& entry : base::kErrorCatalogue) {
    if (entry.code == code) {
      return absl::StrCat("ALUMBRA-", static_cast<std::uint32_t>(code), ": ",
                          entry.description);
    }
  }
  return "ALUMBRA: unknown error";
}

}

absl::Status Corrupt(std::string_view what) {
  return absl::DataLossError(
      absl::StrCat(Describe(base::ErrorCode::kMediaCorrupt), ": ", what));
}

absl::Status Unsupported(std::string_view what) {
  return absl::UnimplementedError(
      absl::StrCat(Describe(base::ErrorCode::kCodecUnsupported), ": ", what));
}

absl::Status InvalidCodecString(std::string_view what) {
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid codec string: ", what));
}

absl::Status Within(absl::Status status, std::string_view what) {
  if (status.ok()) {
    return status;
  }
  return Corrupt(absl::StrCat(what, ": ", status.message()));
}

void BitstreamReader::Latch(const absl::Status& status) {
  if (status_.ok() && !status.ok()) {
    status_ = Within(status, what_);
  }
}

void BitstreamReader::Fail(std::string_view reason) {
  Latch(absl::OutOfRangeError(reason));
}

std::uint64_t BitstreamReader::Bits64(int count) {
  if (!Ok()) {
    return 0;
  }
  const absl::StatusOr<std::uint64_t> value = reader_.ReadBits(count);
  Latch(value.status());
  return value.ok() ? *value : 0;
}

std::uint32_t BitstreamReader::Bits(int count) {
  return static_cast<std::uint32_t>(Bits64(count));
}

bool BitstreamReader::Flag() { return Bits64(1) != 0; }

void BitstreamReader::Skip(std::size_t count) {
  if (Ok()) {
    Latch(reader_.SkipBits(count));
  }
}

std::uint32_t BitstreamReader::Ue() {
  if (!Ok()) {
    return 0;
  }
  const absl::StatusOr<std::uint32_t> value = reader_.ReadExpGolomb();
  Latch(value.status());
  return value.ok() ? *value : 0;
}

std::int32_t BitstreamReader::Se() {
  if (!Ok()) {
    return 0;
  }
  const absl::StatusOr<std::int32_t> value = reader_.ReadSignedExpGolomb();
  Latch(value.status());
  return value.ok() ? *value : 0;
}

std::uint32_t BitstreamReader::BitsAtMost(int count, std::uint32_t limit) {
  const std::uint32_t value = Bits(count);
  if (value > limit) {
    Fail(absl::StrCat("value ", value, " exceeds ", limit));
    return 0;
  }
  return value;
}

std::uint32_t BitstreamReader::UeAtMost(std::uint32_t limit) {
  const std::uint32_t value = Ue();
  if (value > limit) {
    Fail(absl::StrCat("value ", value, " exceeds ", limit));
    return 0;
  }
  return value;
}

}
