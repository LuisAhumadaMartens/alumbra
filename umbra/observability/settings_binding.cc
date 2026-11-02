#include "umbra/observability/settings_binding.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/base/task_runner.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"
#include "umbra/observability/trace.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::observability {
namespace {

Level ToLevel(settings::ObservabilityLogLevel level) {
  switch (level) {
    case settings::ObservabilityLogLevel::kVerbose:
      return Level::kVerbose;
    case settings::ObservabilityLogLevel::kDebug:
      return Level::kDebug;
    case settings::ObservabilityLogLevel::kInfo:
      return Level::kInfo;
    case settings::ObservabilityLogLevel::kWarning:
      return Level::kWarning;
    case settings::ObservabilityLogLevel::kError:
      return Level::kError;
  }
  return Level::kInfo;
}

std::uint32_t ToMask(const std::vector<std::string>& names) {
  std::uint32_t mask = 0;
  for (const std::string& name : names) {
    if (const auto category = ParseCategory(name)) {
      mask |= CategoryBit(*category);
    }
  }
  return mask;
}

void ApplyLogCategories(const std::vector<std::string>& names) {
  SetLogCategoryMask(names.empty() ? kAllCategories : ToMask(names));
}

}

absl::StatusOr<std::unique_ptr<SettingsBinding>> SettingsBinding::Create(
    settings::Store& store,
    const std::shared_ptr<base::SequencedTaskRunner>& runner) {
  SetLogLevel(ToLevel(store.Get(settings::kObservabilityLogLevel)));
  ApplyLogCategories(store.Get(settings::kObservabilityLogCategories));
  SetTraceCategoryMask(
      ToMask(store.Get(settings::kObservabilityTraceCategories)));

  std::unique_ptr<SettingsBinding> binding(new SettingsBinding());
  auto level = store.Observe<settings::ObservabilityLogLevel>(
      settings::kObservabilityLogLevel, runner,
      [](const settings::ObservabilityLogLevel& value) {
        SetLogLevel(ToLevel(value));
      });
  if (!level.ok()) {
    return level.status();
  }
  binding->level_ = *std::move(level);
  auto log_categories = store.Observe<std::vector<std::string>>(
      settings::kObservabilityLogCategories, runner,
      [](const std::vector<std::string>& value) { ApplyLogCategories(value); });
  if (!log_categories.ok()) {
    return log_categories.status();
  }
  binding->log_categories_ = *std::move(log_categories);
  auto trace_categories = store.Observe<std::vector<std::string>>(
      settings::kObservabilityTraceCategories, runner,
      [](const std::vector<std::string>& value) {
        SetTraceCategoryMask(ToMask(value));
      });
  if (!trace_categories.ok()) {
    return trace_categories.status();
  }
  binding->trace_categories_ = *std::move(trace_categories);
  return binding;
}

}
