#include <array>
#include <ranges>

int main() {
  std::array<long, 3> values{5, 6, 7};
  long sum = 0;
  for (auto [index, value] : std::views::enumerate(values)) {
    sum += index * value;
  }
  return sum == 20 ? 0 : 1;
}
