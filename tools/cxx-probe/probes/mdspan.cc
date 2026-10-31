#include <array>
#include <mdspan>

int main() {
  std::array<int, 6> storage{0, 1, 2, 3, 4, 5};
  std::mdspan matrix(storage.data(), 2, 3);
  return matrix[1, 2] == 5 && matrix.extent(1) == 3 ? 0 : 1;
}
