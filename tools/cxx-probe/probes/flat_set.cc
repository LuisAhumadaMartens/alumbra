#include <flat_set>

int main() {
  std::flat_set<int> set{3, 1, 2, 3};
  return set.size() == 3 && *set.begin() == 1 && set.contains(2) ? 0 : 1;
}
