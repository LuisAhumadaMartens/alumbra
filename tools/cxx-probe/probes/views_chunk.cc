#include <array>
#include <ranges>

int main() {
  std::array<int, 5> values{1, 2, 3, 4, 5};
  return std::ranges::distance(values | std::views::chunk(2)) == 3 ? 0 : 1;
}
