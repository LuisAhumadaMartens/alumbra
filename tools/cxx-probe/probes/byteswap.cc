#include <bit>
#include <cstdint>

int main(int argc, char**) {
  static_assert(std::byteswap(std::uint32_t{0x11223344}) == 0x44332211);
  auto value = static_cast<std::uint16_t>(0x1233 + argc);
  return std::byteswap(value) == 0x3412 ? 0 : 1;
}
