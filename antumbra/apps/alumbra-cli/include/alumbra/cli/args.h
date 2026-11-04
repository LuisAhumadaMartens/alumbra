#ifndef ALUMBRA_CLI_ARGS_H_
#define ALUMBRA_CLI_ARGS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
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

using SettingValue = std::variant<bool, std::int64_t, double, std::string,
                                  std::vector<std::string>>;

struct SettingAssignment {
  std::string key;
  SettingValue value;
};

struct AssignmentParseResult {
  std::vector<SettingAssignment> assignments;
  std::string error;
};

[[nodiscard]] ParseResult ParseArguments(const std::vector<std::string>& args);
[[nodiscard]] std::string HelpText();
[[nodiscard]] std::string VersionText();
[[nodiscard]] std::string ListSettings();
[[nodiscard]] AssignmentParseResult ParseAssignments(const Options& options);
[[nodiscard]] std::string ValidateAssignments(const Options& options);

}

#endif
