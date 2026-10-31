constexpr int Twice(int value) {
  if consteval {
    return value * 2;
  } else {
    return value + value;
  }
}

int main(int argc, char**) {
  static_assert(Twice(21) == 42);
  return Twice(20 + argc) == 42 ? 0 : 1;
}
