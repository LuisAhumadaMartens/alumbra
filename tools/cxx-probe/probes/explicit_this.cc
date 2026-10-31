#include <utility>

struct Counter {
  int value = 0;

  template <typename Self>
  auto&& Get(this Self&& self) {
    return std::forward<Self>(self).value;
  }
};

int main() {
  Counter counter{41};
  ++counter.Get();
  return counter.Get() == 42 ? 0 : 1;
}
