#include <generator>

std::generator<int> Count(int limit) {
  for (int i = 0; i < limit; ++i) {
    co_yield i;
  }
}

int main() {
  int sum = 0;
  for (int value : Count(4)) {
    sum += value;
  }
  return sum == 6 ? 0 : 1;
}
