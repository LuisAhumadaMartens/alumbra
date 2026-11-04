#ifndef ALUMBRA_CLI_ARGS_H_
#define ALUMBRA_CLI_ARGS_H_

#include <optional>
#include <string>
#include <vector>

namespace alumbra::cli {

struct Options {
  std::string source;
  std::vector<std::string> assignments;
  std::optional<double> start_seconds;
  std::string log_level = "info";
  std::string trace_categories;
  bool headless = false;
  bool list_settings = false;
  bool stats = false;
  bool version = false;
  bool help = false;
};

struct ParseResult {
  Options options;
  std::string error;
};

[[nodiscard]] ParseResult ParseArguments(const std::vector<std::string>& args);
[[nodiscard]] std::string HelpText();
[[nodiscard]] std::string VersionText();
[[nodiscard]] std::string ListSettings();
[[nodiscard]] std::string ValidateAssignments(const Options& options);

}

#endif
