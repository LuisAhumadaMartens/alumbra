#include <format>
#include <string>

int main() { return std::format("{:.2f}", 3.14159) == "3.14" ? 0 : 1; }
