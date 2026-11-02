#ifndef TEST_UMBRA_CODEC_TEST_UTIL_H_
#define TEST_UMBRA_CODEC_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace umbra::codec::test_data {

inline std::vector<std::uint8_t> FromHex(std::string_view hex) {
  std::vector<std::uint8_t> bytes;
  const auto digit = [](char c) {
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
  };
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(
        static_cast<std::uint8_t>((digit(hex[i]) << 4) | digit(hex[i + 1])));
  }
  return bytes;
}

class BitWriter {
 public:
  BitWriter& Put(std::uint64_t value, int bits) {
    for (int i = bits - 1; i >= 0; --i) {
      PutBit(static_cast<unsigned>((value >> i) & 1U));
    }
    return *this;
  }

  BitWriter& Flag(bool value) { return Put(value ? 1 : 0, 1); }

  BitWriter& Align() {
    while (used_ != 0) {
      PutBit(0);
    }
    return *this;
  }

  BitWriter& Ue(std::uint32_t value) {
    const std::uint64_t coded = static_cast<std::uint64_t>(value) + 1;
    int length = 0;
    while ((coded >> (length + 1)) != 0) {
      ++length;
    }
    Put(0, length);
    return Put(coded, length + 1);
  }

  BitWriter& Se(std::int32_t value) {
    return Ue(value > 0 ? static_cast<std::uint32_t>((2 * value) - 1)
                        : static_cast<std::uint32_t>(-2 * value));
  }

  std::vector<std::uint8_t> Rbsp() {
    Put(1, 1);
    while (used_ != 0) {
      Put(0, 1);
    }
    return bytes_;
  }

 private:
  void PutBit(unsigned bit) {
    if (used_ == 0) {
      bytes_.push_back(0);
    }
    bytes_.back() =
        static_cast<std::uint8_t>(bytes_.back() | (bit << (7 - used_)));
    used_ = (used_ + 1) % 8;
  }

  std::vector<std::uint8_t> bytes_;
  int used_ = 0;
};

inline std::vector<std::uint8_t> WithEmulationPrevention(
    const std::vector<std::uint8_t>& rbsp) {
  std::vector<std::uint8_t> output;
  int zeros = 0;
  for (const std::uint8_t byte : rbsp) {
    if (zeros >= 2 && byte <= 3) {
      output.push_back(3);
      zeros = 0;
    }
    output.push_back(byte);
    zeros = byte == 0 ? zeros + 1 : 0;
  }
  return output;
}

}

#endif
