#include <ranges>
#include <vector>

int main() {
  auto squares =
      std::views::iota(1, 4) |
      std::views::transform([](int value) { return value * value; }) |
      std::ranges::to<std::vector>();
  return squares == std::vector<int>{1, 4, 9} ? 0 : 1;
}
