#include <utility>

enum class Color : unsigned char { kRed = 3 };

int main() {
  static_assert(std::to_underlying(Color::kRed) == 3);
  return std::to_underlying(Color::kRed) == 3 ? 0 : 1;
}
