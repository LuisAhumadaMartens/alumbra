#include <utility>

int Classify(int value) {
  switch (value) {
    case 0:
      return 10;
    case 1:
      return 20;
    default:
      std::unreachable();
  }
}

int main(int argc, char**) { return Classify(argc) == 20 ? 0 : 1; }
