#include <cstddef>
#include <exception>
#include <iostream>
#include <span>
#include <string>

#include "simulator.h"

int main(int argc, char** argv) {
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  const bool compare = argc == 6 && std::string(arguments[5]) == "--compare";
  if ((argc != 5 && !compare) || std::string(arguments[1]) != "--trace" ||
      std::string(arguments[3]) != "--format") {
    std::cerr << "usage: abr_simulator --trace FILE --format json|markdown "
                 "[--compare]\n";
    return 2;
  }
  try {
    const auto trace = umbra::abr::simulation::ReadTrace(arguments[2]);
    const std::string format = arguments[4];
    if (format != "json" && format != "markdown") {
      std::cerr << "format must be json or markdown\n";
      return 2;
    }
    const auto baseline = umbra::abr::simulation::Run(trace);
    const auto conservative =
        umbra::abr::simulation::Run(trace, "throughput_conservative");
    if (format == "json") {
      if (compare) {
        std::string first = umbra::abr::simulation::RenderJson(baseline);
        first.pop_back();
        std::string second = umbra::abr::simulation::RenderJson(conservative);
        second.pop_back();
        std::cout << '[' << first << ',' << second << "]\n";
      } else {
        std::cout << umbra::abr::simulation::RenderJson(baseline);
      }
    } else if (compare) {
      const std::string first =
          umbra::abr::simulation::RenderMarkdown(baseline);
      const std::string second =
          umbra::abr::simulation::RenderMarkdown(conservative);
      const std::size_t row_start =
          second.find('\n', second.find('\n') + 1) + 1;
      std::cout << first << second.substr(row_start);
    } else {
      std::cout << umbra::abr::simulation::RenderMarkdown(baseline);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
