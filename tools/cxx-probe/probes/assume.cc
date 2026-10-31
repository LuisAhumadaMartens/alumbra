int Halve(int value) {
  [[assume(value > 0)]];
  return value / 2;
}

int main(int argc, char**) { return Halve(83 + argc) == 42 ? 0 : 1; }
