#include <functional>
#include <memory>
#include <utility>

int main() {
  auto value = std::make_unique<int>(42);
  std::move_only_function<int()> get = [value = std::move(value)] {
    return *value;
  };
  return get() == 42 ? 0 : 1;
}
