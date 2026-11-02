#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fuzz_target.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/setting_parser.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/setting_value.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace {

void Check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void CheckValueParsing(const umbra::base::UrlRedactor& redactor,
                       std::string_view input) {
  const umbra::settings::Registry registry = umbra::settings::GetRegistry();
  const auto& info =
      registry.settings[static_cast<std::uint8_t>(input.front()) %
                        registry.settings.size()];
  const std::string_view text = input.substr(1);
  const absl::StatusOr<umbra::settings::Value> parsed =
      umbra::settings::ParseValue(info, text, redactor);
  if (!parsed.ok()) {
    Check(absl::IsInvalidArgument(parsed.status()));
    return;
  }
  Check(!umbra::settings::FindViolation(info, *parsed).has_value());
  const absl::StatusOr<umbra::settings::Value> reparsed =
      umbra::settings::ParseValue(info, umbra::settings::FormatValue(*parsed),
                                  redactor);
  Check(reparsed.ok());
  Check(*reparsed == *parsed);
}

void CheckAssignments(const umbra::base::UrlRedactor& redactor,
                      std::string_view input) {
  const umbra::settings::Registry registry = umbra::settings::GetRegistry();
  const absl::StatusOr<umbra::settings::Assignment> assignment =
      umbra::settings::ParseAssignment(input, redactor);
  if (!assignment.ok()) {
    Check(absl::IsInvalidArgument(assignment.status()));
    return;
  }
  Check(input.starts_with(assignment->key));
  Check(!assignment->key.empty());
  umbra::settings::Store store(registry, umbra::settings::Platform::kLinux);
  const absl::Status status =
      store.SetFromAssignment(umbra::settings::Layer::kUser, input);
  const auto found = umbra::settings::FindSetting(registry, assignment->key);
  if (!found.has_value()) {
    Check(absl::IsInvalidArgument(status));
    return;
  }
  const auto parsed = umbra::settings::ParseValue(
      registry.settings[found->index], assignment->value, redactor);
  Check(status.ok() == parsed.ok());
  if (!status.ok()) {
    Check(absl::IsInvalidArgument(status));
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  static const umbra::base::UrlRedactor redactor({1, 2});
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  if (!input.empty()) {
    CheckValueParsing(redactor, input);
  }
  CheckAssignments(redactor, input);
  return 0;
}
