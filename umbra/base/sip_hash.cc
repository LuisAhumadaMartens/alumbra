#include "umbra/base/sip_hash.h"

#include <bit>
#include <cstddef>

namespace umbra::base::internal {
namespace {

void Round(std::array<std::uint64_t, 4>& v) {
  v[0] += v[1];
  v[1] = std::rotl(v[1], 13) ^ v[0];
  v[0] = std::rotl(v[0], 32);
  v[2] += v[3];
  v[3] = std::rotl(v[3], 16) ^ v[2];
  v[0] += v[3];
  v[3] = std::rotl(v[3], 21) ^ v[0];
  v[2] += v[1];
  v[1] = std::rotl(v[1], 17) ^ v[2];
  v[2] = std::rotl(v[2], 32);
}

}

std::uint64_t SipHash24(std::string_view input,
                        std::array<std::uint64_t, 2> key) {
  std::array<std::uint64_t, 4> v{
      0x736f6d6570736575ULL ^ key[0], 0x646f72616e646f6dULL ^ key[1],
      0x6c7967656e657261ULL ^ key[0], 0x7465646279746573ULL ^ key[1]};
  std::size_t offset = 0;
  while (input.size() - offset >= 8) {
    std::uint64_t word = 0;
    for (int i = 0; i < 8; ++i) {
      word |= static_cast<std::uint64_t>(
                  static_cast<unsigned char>(input[offset + i]))
              << (8 * i);
    }
    v[3] ^= word;
    Round(v);
    Round(v);
    v[0] ^= word;
    offset += 8;
  }
  std::uint64_t tail = static_cast<std::uint64_t>(input.size() & 255) << 56;
  for (std::size_t i = 0; i < input.size() - offset; ++i) {
    tail |= static_cast<std::uint64_t>(
                static_cast<unsigned char>(input[offset + i]))
            << (8 * i);
  }
  v[3] ^= tail;
  Round(v);
  Round(v);
  v[0] ^= tail;
  v[2] ^= 255;
  for (int i = 0; i < 4; ++i) {
    Round(v);
  }
  return v[0] ^ v[1] ^ v[2] ^ v[3];
}

}
