#include <memory>

void Create(int** out) { *out = new int(42); }

int main() {
  std::unique_ptr<int> value;
  Create(std::out_ptr(value));
  return value && *value == 42 ? 0 : 1;
}
