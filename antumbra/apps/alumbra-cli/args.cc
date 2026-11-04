#include "alumbra/cli/args.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace alumbra::cli {
namespace {

struct Setting {
  std::string type;
  std::string default_value;
};

std::map<std::string, Setting> ReadSettings() {
  std::ifstream input(ALUMBRA_CLI_SETTINGS_INDEX);
  std::map<std::string, Setting> settings;
  std::string line;
  std::string key;
  while (std::getline(input, line)) {
    if (line.size() > 6 && line.starts_with("    \"") &&
        line.find("\": {") != std::string::npos) {
      const std::size_t start = line.find('\"') + 1;
      key = line.substr(start, line.find('\"', start) - start);
      settings[key] = {};
    } else if (!key.empty() && line.starts_with("      \"type\": ")) {
      const std::size_t start = line.find('\"', 12) + 1;
      settings[key].type = line.substr(start, line.find('\"', start) - start);
    } else if (!key.empty() && line.starts_with("      \"default\": ")) {
      settings[key].default_value = line.substr(17);
      if (!settings[key].default_value.empty() &&
          settings[key].default_value.back() == ',') {
        settings[key].default_value.pop_back();
      }
      if (line.find(R"("default": ")") != std::string::npos) {
        const std::size_t start = line.find('\"', 15) + 1;
        const std::size_t end = line.rfind('\"');
        settings[key].default_value = line.substr(start, end - start);
      }
    } else if (line.starts_with("    },")) {
      key.clear();
    }
  }
  return settings;
}

std::string ExpectedType(std::string_view type) {
  if (type == "boolean") {
    return "a boolean";
  }
  if (type == "integer" || type == "duration" || type == "byte_size") {
    if (type == "duration") {
      return "a duration in integer milliseconds";
    }
    if (type == "byte_size") {
      return "an integer byte size";
    }
    return "an integer";
  }
  if (type == "double") {
    return "a number";
  }
  if (type == "enum") {
    return "an enum value";
  }
  if (type == "string_list") {
    return "a comma-separated list";
  }
  return "a string";
}

bool IsInteger(std::string_view value) {
  std::int64_t parsed = 0;
  const auto result = std::from_chars(value.begin(), value.end(), parsed);
  return !value.empty() && result.ec == std::errc{} &&
         result.ptr == value.end();
}

bool IsNumber(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  double parsed = 0.0;
  const auto result = std::from_chars(value.begin(), value.end(), parsed);
  return result.ec == std::errc{} && result.ptr == value.end() &&
         std::isfinite(parsed);
}

bool IsValid(std::string_view type, std::string_view value) {
  if (type == "boolean") {
    return value == "true" || value == "false";
  }
  if (type == "integer" || type == "duration" || type == "byte_size") {
    return IsInteger(value);
  }
  if (type == "double") {
    return IsNumber(value);
  }
  return true;
}

std::optional<SettingValue> ParseSettingValue(std::string_view type,
                                              std::string_view value) {
  if (!IsValid(type, value)) {
    return std::nullopt;
  }
  if (type == "boolean") {
    return SettingValue(value == "true");
  }
  if (type == "integer" || type == "duration" || type == "byte_size") {
    std::int64_t parsed = 0;
    static_cast<void>(std::from_chars(value.begin(), value.end(), parsed));
    return SettingValue(parsed);
  }
  if (type == "double") {
    double parsed = 0.0;
    static_cast<void>(std::from_chars(value.begin(), value.end(), parsed));
    return SettingValue(parsed);
  }
  if (type == "string_list") {
    std::vector<std::string> parsed;
    std::size_t start = 0;
    while (start < value.size()) {
      const std::size_t separator = value.find(',', start);
      const std::size_t end =
          separator == std::string_view::npos ? value.size() : separator;
      parsed.emplace_back(value.substr(start, end - start));
      if (separator == std::string_view::npos) {
        break;
      }
      start = separator + 1;
    }
    return SettingValue(std::move(parsed));
  }
  return SettingValue(std::string(value));
}

enum class OptionKind {
  kHelp,
  kVersion,
  kListSettings,
  kHeadless,
  kStats,
  kSet,
  kStart,
  kLogLevel,
  kTrace,
  kSource,
  kUnknown,
};

OptionKind Classify(std::string_view argument) {
  constexpr std::array<std::pair<std::string_view, OptionKind>, 9> kOptions{{
      {"--help", OptionKind::kHelp},
      {"-h", OptionKind::kHelp},
      {"--version", OptionKind::kVersion},
      {"--list-settings", OptionKind::kListSettings},
      {"--headless", OptionKind::kHeadless},
      {"--stats", OptionKind::kStats},
      {"--set", OptionKind::kSet},
      {"--start", OptionKind::kStart},
      {"--log-level", OptionKind::kLogLevel},
  }};
  for (const auto& [name, kind] : kOptions) {
    if (argument == name) {
      return kind;
    }
  }
  if (argument == "--trace") {
    return OptionKind::kTrace;
  }
  if (argument.starts_with('-')) {
    return OptionKind::kUnknown;
  }
  return OptionKind::kSource;
}

bool RequiresValue(OptionKind kind) {
  return kind == OptionKind::kSet || kind == OptionKind::kStart ||
         kind == OptionKind::kLogLevel || kind == OptionKind::kTrace;
}

std::optional<std::string> NextValue(const std::vector<std::string>& args,
                                     std::size_t* index, std::string_view flag,
                                     std::string* error) {
  if (*index + 1 >= args.size() || args[*index + 1].starts_with("--")) {
    *error = std::string(flag) + " requires a value";
    return std::nullopt;
  }
  ++*index;
  return args[*index];
}

bool IsLogLevel(std::string_view level) {
  return level == "verbose" || level == "debug" || level == "info" ||
         level == "warning" || level == "error";
}

bool ApplyOption(OptionKind kind, std::string value, Options* options,
                 std::string* error) {
  switch (kind) {
    case OptionKind::kHelp:
      options->help = true;
      break;
    case OptionKind::kVersion:
      options->version = true;
      break;
    case OptionKind::kListSettings:
      options->list_settings = true;
      break;
    case OptionKind::kHeadless:
      options->headless = true;
      break;
    case OptionKind::kStats:
      options->stats = true;
      break;
    case OptionKind::kSet:
      options->assignments.push_back(std::move(value));
      break;
    case OptionKind::kStart: {
      double seconds = 0.0;
      const std::string_view value_view(value);
      const auto parsed =
          std::from_chars(value_view.begin(), value_view.end(), seconds);
      if (parsed.ec != std::errc{} || parsed.ptr != value_view.end() ||
          !std::isfinite(seconds) || seconds < 0.0) {
        *error = "--start requires a non-negative time in seconds";
        return false;
      }
      options->start_seconds = seconds;
      break;
    }
    case OptionKind::kLogLevel:
      if (!IsLogLevel(value)) {
        *error = "--log-level must be verbose, debug, info, warning or error";
        return false;
      }
      options->log_level = std::move(value);
      break;
    case OptionKind::kTrace:
      options->trace_categories = std::move(value);
      break;
    case OptionKind::kSource:
    case OptionKind::kUnknown:
      break;
  }
  return true;
}

}

ParseResult ParseArguments(const std::vector<std::string>& args) {
  ParseResult result;
  bool has_source = false;
  for (std::size_t index = 0; index < args.size(); ++index) {
    const std::string& argument = args[index];
    const OptionKind kind = Classify(argument);
    if (kind == OptionKind::kUnknown) {
      result.error = "unknown option: " + argument;
      break;
    }
    if (kind == OptionKind::kSource) {
      if (has_source) {
        result.error = "only one source may be specified";
        break;
      }
      result.options.source = argument;
      has_source = true;
      continue;
    }
    std::string value;
    if (RequiresValue(kind)) {
      auto next = NextValue(args, &index, argument, &result.error);
      if (!next) {
        break;
      }
      value = std::move(*next);
    }
    if (!ApplyOption(kind, std::move(value), &result.options, &result.error)) {
      break;
    }
  }
  if (result.error.empty() && !result.options.help && !result.options.version &&
      !result.options.list_settings && !has_source) {
    result.error = "a URL or path is required";
  }
  return result;
}

std::string HelpText() {
  return "Usage: alumbra-cli [options] <url-or-path>\n"
         "  --set key=value       Set an integrator setting (repeatable)\n"
         "  --list-settings       List setting defaults\n"
         "  --start <seconds>    Start playback at a position\n"
         "  --headless            Play without a window\n"
         "  --log-level <level>   verbose, debug, info, warning or error\n"
         "  --trace <categories>  Enable trace categories\n"
         "  --stats               Print playback statistics\n"
         "  --version             Print version\n"
         "  --help                Print this help\n";
}

std::string VersionText() { return "alumbra-cli 0.0.0\n"; }

std::string ListSettings() {
  const std::map<std::string, Setting> settings = ReadSettings();
  std::ostringstream output;
  for (const auto& [key, setting] : settings) {
    output << key << '=' << setting.default_value << '\n';
  }
  return output.str();
}

AssignmentParseResult ParseAssignments(const Options& options) {
  AssignmentParseResult result;
  const std::map<std::string, Setting> settings = ReadSettings();
  for (const std::string& assignment : options.assignments) {
    const std::size_t separator = assignment.find('=');
    if (separator == std::string::npos || separator == 0) {
      result.error = "--set requires key=value: " + assignment;
      return result;
    }
    const std::string key = assignment.substr(0, separator);
    const std::string value = assignment.substr(separator + 1);
    const auto found = settings.find(key);
    if (found == settings.end()) {
      result.error = "unknown setting: " + key;
      return result;
    }
    std::optional<SettingValue> parsed =
        ParseSettingValue(found->second.type, value);
    if (!parsed.has_value()) {
      std::string error = key;
      error.append("=");
      error.append(value);
      error.append(" expects ");
      error.append(ExpectedType(found->second.type));
      result.error = std::move(error);
      return result;
    }
    result.assignments.push_back({key, std::move(*parsed)});
  }
  return result;
}

std::string ValidateAssignments(const Options& options) {
  return ParseAssignments(options).error;
}

}
