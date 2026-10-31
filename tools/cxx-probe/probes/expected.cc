#include <expected>

std::expected<int, int> Parse(bool ok) {
  if (ok) {
    return 42;
  }
  return std::unexpected(7);
}

int main() {
  auto good = Parse(true);
  auto bad = Parse(false);
  auto doubled = good.transform([](int value) { return value * 2; });
  return good.value() == 42 && !bad && bad.error() == 7 && *doubled == 84 ? 0
                                                                          : 1;
}
