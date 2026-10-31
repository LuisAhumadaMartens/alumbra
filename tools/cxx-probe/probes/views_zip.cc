#include <array>
#include <ranges>

int main() {
  std::array<int, 3> left{1, 2, 3};
  std::array<int, 3> right{4, 5, 6};
  int sum = 0;
  for (auto [a, b] : std::views::zip(left, right)) {
    sum += a * b;
  }
  return sum == 32 ? 0 : 1;
}
