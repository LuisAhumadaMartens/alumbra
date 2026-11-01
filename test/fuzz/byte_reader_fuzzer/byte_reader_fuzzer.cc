#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fuzz_target.h"
#include "umbra/base/bit_reader.h"
#include "umbra/base/byte_reader.h"

namespace {

using Bytes = std::span<const std::uint8_t>;

constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kMaxExpGolombLeadingZeros = 31;

void Check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

std::uint64_t ReferenceBits(Bytes data, std::size_t position,
                            std::size_t count) {
  std::uint64_t value = 0;
  for (std::size_t bit = position; bit < position + count; ++bit) {
    const unsigned byte = data[bit / kBitsPerByte];
    value = (value << 1U) |
            ((byte >> (kBitsPerByte - 1 - (bit % kBitsPerByte))) & 1U);
  }
  return value;
}

template <typename T>
absl::StatusOr<std::uint64_t> Widen(const absl::StatusOr<T>& value) {
  if (!value.ok()) {
    return value.status();
  }
  return static_cast<std::uint64_t>(*value);
}

void CheckUnsigned(const absl::StatusOr<std::uint64_t>& value, Bytes data,
                   std::size_t before, std::size_t after, std::size_t width) {
  if (width == 0 || width > sizeof(std::uint64_t)) {
    Check(value.status().code() == absl::StatusCode::kInvalidArgument);
    Check(after == before);
  } else if (width > data.size() - before) {
    Check(value.status().code() == absl::StatusCode::kOutOfRange);
    Check(after == before);
  } else {
    Check(value.ok());
    Check(*value ==
          ReferenceBits(data, before * kBitsPerByte, width * kBitsPerByte));
    Check(after == before + width);
  }
}

void CheckAdvance(const absl::Status& status, std::size_t available,
                  std::size_t before, std::size_t after, std::size_t count) {
  if (count > available) {
    Check(status.code() == absl::StatusCode::kOutOfRange);
    Check(after == before);
  } else {
    Check(status.ok());
    Check(after == before + count);
  }
}

void FuzzByteReader(Bytes script, Bytes data) {
  umbra::base::ByteReader reader(data);
  for (std::size_t i = 0; i + 1 < script.size(); i += 2) {
    const std::size_t before = reader.position();
    const std::size_t argument = script[i + 1];
    switch (script[i] % 8) {
      case 0: {
        const absl::StatusOr<std::uint64_t> value = Widen(reader.ReadU8());
        CheckUnsigned(value, data, before, reader.position(), 1);
        break;
      }
      case 1: {
        const absl::StatusOr<std::uint64_t> value = Widen(reader.ReadU16());
        CheckUnsigned(value, data, before, reader.position(), 2);
        break;
      }
      case 2: {
        const absl::StatusOr<std::uint64_t> value = Widen(reader.ReadU24());
        CheckUnsigned(value, data, before, reader.position(), 3);
        break;
      }
      case 3: {
        const absl::StatusOr<std::uint64_t> value = Widen(reader.ReadU32());
        CheckUnsigned(value, data, before, reader.position(), 4);
        break;
      }
      case 4: {
        const absl::StatusOr<std::uint64_t> value = reader.ReadU64();
        CheckUnsigned(value, data, before, reader.position(), 8);
        break;
      }
      case 5: {
        const std::size_t width = argument % 10;
        const absl::StatusOr<std::uint64_t> value = reader.ReadUnsigned(width);
        CheckUnsigned(value, data, before, reader.position(), width);
        break;
      }
      case 6: {
        const absl::StatusOr<Bytes> bytes = reader.ReadBytes(argument);
        CheckAdvance(bytes.status(), data.size() - before, before,
                     reader.position(), argument);
        if (bytes.ok()) {
          Check(bytes->data() == data.subspan(before).data());
          Check(bytes->size() == argument);
        }
        break;
      }
      default: {
        const absl::Status status = reader.Skip(argument);
        CheckAdvance(status, data.size() - before, before, reader.position(),
                     argument);
        break;
      }
    }
    Check(reader.size() == data.size());
    Check(reader.position() + reader.remaining() == data.size());
  }
}

struct ExpGolomb {
  absl::StatusCode code = absl::StatusCode::kOk;
  std::uint32_t value = 0;
  std::size_t length = 0;
};

ExpGolomb ReferenceExpGolomb(Bytes data, std::size_t position) {
  const std::size_t bits = data.size() * kBitsPerByte;
  std::size_t zeros = 0;
  while (position + zeros < bits &&
         ReferenceBits(data, position + zeros, 1) == 0) {
    ++zeros;
    if (zeros > kMaxExpGolombLeadingZeros) {
      return {.code = absl::StatusCode::kInvalidArgument};
    }
  }
  const std::size_t length = (2 * zeros) + 1;
  if (length > bits - position) {
    return {.code = absl::StatusCode::kOutOfRange};
  }
  const std::uint64_t suffix = ReferenceBits(data, position + zeros + 1, zeros);
  return {.value = static_cast<std::uint32_t>((std::uint64_t{1} << zeros) - 1 +
                                              suffix),
          .length = length};
}

void CheckExpGolomb(absl::StatusCode code, std::size_t before,
                    std::size_t after, const ExpGolomb& expected) {
  Check(code == expected.code);
  Check(after ==
        (code == absl::StatusCode::kOk ? before + expected.length : before));
}

void FuzzBitReader(Bytes script, Bytes data) {
  umbra::base::BitReader reader(data);
  const std::size_t bits = data.size() * kBitsPerByte;
  for (std::size_t i = 0; i + 1 < script.size(); i += 2) {
    const std::size_t before = reader.position();
    const std::size_t argument = script[i + 1];
    switch (script[i] % 6) {
      case 0: {
        const std::size_t count = argument % 70;
        const absl::StatusOr<std::uint64_t> value =
            reader.ReadBits(static_cast<int>(count));
        if (count > 64) {
          Check(value.status().code() == absl::StatusCode::kInvalidArgument);
          Check(reader.position() == before);
        } else {
          CheckAdvance(value.status(), bits - before, before, reader.position(),
                       count);
          Check(!value.ok() || *value == ReferenceBits(data, before, count));
        }
        break;
      }
      case 1: {
        const absl::StatusOr<bool> flag = reader.ReadFlag();
        CheckAdvance(flag.status(), bits - before, before, reader.position(),
                     1);
        Check(!flag.ok() || *flag == (ReferenceBits(data, before, 1) == 1));
        break;
      }
      case 2: {
        const absl::Status status = reader.SkipBits(argument);
        CheckAdvance(status, bits - before, before, reader.position(),
                     argument);
        break;
      }
      case 3:
        reader.SkipToByteBoundary();
        Check(reader.is_byte_aligned());
        Check(reader.position() >= before &&
              reader.position() - before < kBitsPerByte);
        Check(reader.position() <= bits);
        break;
      case 4: {
        const ExpGolomb expected = ReferenceExpGolomb(data, before);
        const absl::StatusOr<std::uint32_t> value = reader.ReadExpGolomb();
        CheckExpGolomb(value.status().code(), before, reader.position(),
                       expected);
        Check(!value.ok() || *value == expected.value);
        break;
      }
      default: {
        const ExpGolomb expected = ReferenceExpGolomb(data, before);
        const absl::StatusOr<std::int32_t> value = reader.ReadSignedExpGolomb();
        CheckExpGolomb(value.status().code(), before, reader.position(),
                       expected);
        const std::int64_t magnitude =
            (static_cast<std::int64_t>(expected.value) + 1) / 2;
        Check(!value.ok() ||
              *value == (expected.value % 2 == 1 ? magnitude : -magnitude));
        break;
      }
    }
    Check(reader.is_byte_aligned() == (reader.position() % kBitsPerByte == 0));
    Check(reader.position() + reader.remaining() == bits);
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const Bytes input(data, size);
  if (input.empty()) {
    return 0;
  }
  const std::size_t script_size =
      std::min<std::size_t>(input.front(), input.size() - 1);
  const Bytes script = input.subspan(1, script_size);
  const Bytes payload = input.subspan(1 + script_size);
  FuzzByteReader(script, payload);
  FuzzBitReader(script, payload);
  return 0;
}
