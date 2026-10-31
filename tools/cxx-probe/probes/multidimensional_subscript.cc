#include <array>
#include <cstddef>

class Grid {
 public:
  int& operator[](std::size_t row, std::size_t column) {
    return cells_[(row * 2) + column];
  }

 private:
  std::array<int, 4> cells_{};
};

int main() {
  Grid grid;
  grid[1, 1] = 42;
  return grid[1, 1] == 42 ? 0 : 1;
}
