#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "simulator.h"

namespace {

namespace simulation = umbra::abr::simulation;

std::vector<simulation::Result> RunSuiteTraces(
    const std::filesystem::path& trace_directory) {
  std::vector<std::filesystem::path> paths;
  for (const auto& entry :
       std::filesystem::directory_iterator(trace_directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".csv") {
      paths.push_back(entry.path());
    }
  }
  std::ranges::sort(paths);
  if (paths.empty()) {
    throw std::runtime_error("synthetic trace suite is empty");
  }
  std::vector<simulation::Result> results;
  results.reserve(paths.size());
  for (const auto& path : paths) {
    results.push_back(simulation::Run(simulation::ReadTrace(path)));
  }
  return results;
}

int RegenerateBaseline(const std::vector<simulation::Result>& results,
                       const std::filesystem::path& baseline_path) {
  std::ofstream output(baseline_path, std::ios::trunc);
  if (!output) {
    throw std::runtime_error("could not write baseline: " +
                             baseline_path.string());
  }
  output.imbue(std::locale::classic());
  output << std::setprecision(12)
         << "trace,rebuffer_ratio,bitrate_efficiency,switches_per_minute,"
            "max_rebuffer_ratio,min_bitrate_efficiency,"
            "max_switches_per_minute\n";
  for (const auto& result : results) {
    const auto& metrics = result.metrics;
    output << result.trace_name << ',' << metrics.rebuffer_ratio << ','
           << metrics.bitrate_efficiency << ',' << metrics.switches_per_minute
           << ',' << metrics.rebuffer_ratio * 1.1 << ','
           << metrics.bitrate_efficiency * 0.9 << ','
           << metrics.switches_per_minute * 1.1 << '\n';
  }
  return output ? 0 : 1;
}

bool PrintEvaluations(const std::vector<simulation::Result>& results,
                      const std::vector<simulation::Baseline>& baselines) {
  if (baselines.size() != results.size()) {
    throw std::runtime_error(
        "baseline trace count does not match synthetic suite");
  }
  bool passed = true;
  std::cout
      << "| Trace | Rebuffer ratio | Maximum | Bitrate efficiency | Minimum "
         "| Switches/minute | Maximum | Result |\n"
         "|---|---:|---:|---:|---:|---:|---:|---|\n";
  for (const auto& result : results) {
    const auto baseline = std::ranges::find(baselines, result.trace_name,
                                            &simulation::Baseline::trace_name);
    if (baseline == baselines.end()) {
      throw std::runtime_error("baseline is missing trace: " +
                               result.trace_name);
    }
    const auto evaluation = simulation::Evaluate(result, *baseline);
    std::cout << simulation::RenderEvaluationMarkdown(evaluation);
    passed = passed && evaluation.passed;
  }
  return passed;
}

int RunSuite(std::span<char*> arguments) {
  if (arguments.size() < 5 || arguments.size() > 6 ||
      std::string(arguments[3]) != "--baseline") {
    std::cerr << "usage: abr_simulator --suite DIR --baseline FILE "
                 "[--regenerate-baseline]\n";
    return 2;
  }
  const std::filesystem::path trace_directory(arguments[2]);
  const std::filesystem::path baseline_path(arguments[4]);
  const bool regenerate = arguments.size() == 6 &&
                          std::string(arguments[5]) == "--regenerate-baseline";
  if (arguments.size() == 6 && !regenerate) {
    std::cerr << "usage: abr_simulator --suite DIR --baseline FILE "
                 "[--regenerate-baseline]\n";
    return 2;
  }
  const auto results = RunSuiteTraces(trace_directory);
  if (regenerate) {
    return RegenerateBaseline(results, baseline_path);
  }
  const auto baselines = simulation::ReadBaseline(baseline_path);
  const bool passed = PrintEvaluations(results, baselines);
  if (!passed) {
    std::cerr << "ABR simulation regression thresholds exceeded\n";
    return 1;
  }
  return 0;
}

int RunSingle(std::span<char*> arguments) {
  const bool compare =
      arguments.size() == 6 && std::string(arguments[5]) == "--compare";
  if ((arguments.size() != 5 && !compare) ||
      std::string(arguments[1]) != "--trace" ||
      std::string(arguments[3]) != "--format") {
    std::cerr << "usage: abr_simulator --trace FILE --format json|markdown "
                 "[--compare]\n";
    return 2;
  }
  const auto trace = simulation::ReadTrace(arguments[2]);
  const std::string format = arguments[4];
  if (format != "json" && format != "markdown") {
    std::cerr << "format must be json or markdown\n";
    return 2;
  }
  const auto baseline = simulation::Run(trace);
  const auto conservative = simulation::Run(trace, "throughput_conservative");
  if (format == "json") {
    if (compare) {
      std::string first = simulation::RenderJson(baseline);
      first.pop_back();
      std::string second = simulation::RenderJson(conservative);
      second.pop_back();
      std::cout << '[' << first << ',' << second << "]\n";
    } else {
      std::cout << simulation::RenderJson(baseline);
    }
  } else if (compare) {
    const std::string first = simulation::RenderMarkdown(baseline);
    const std::string second = simulation::RenderMarkdown(conservative);
    const std::size_t row_start = second.find('\n', second.find('\n') + 1) + 1;
    std::cout << first << second.substr(row_start);
  } else {
    std::cout << simulation::RenderMarkdown(baseline);
  }
  return 0;
}

}

int main(int argc, char** argv) {
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  try {
    if (argc >= 2 && std::string(arguments[1]) == "--suite") {
      return RunSuite(arguments);
    }
    return RunSingle(arguments);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
