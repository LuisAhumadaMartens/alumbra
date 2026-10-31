#include <flat_map>
#include <string>

int main() {
  std::flat_map<std::string, int> map;
  map["b"] = 2;
  map.emplace("a", 1);
  return map.begin()->first == "a" && map.at("b") == 2 && map.size() == 2 ? 0
                                                                          : 1;
}
