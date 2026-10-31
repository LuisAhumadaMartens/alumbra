#include <stacktrace>

int main() { return std::stacktrace::current().empty() ? 1 : 0; }
