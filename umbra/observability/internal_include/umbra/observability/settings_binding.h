#ifndef UMBRA_OBSERVABILITY_SETTINGS_BINDING_H_
#define UMBRA_OBSERVABILITY_SETTINGS_BINDING_H_

#include <memory>

#include "absl/status/statusor.h"
#include "umbra/base/task_runner.h"
#include "umbra/settings/store.h"

namespace umbra::observability {

class SettingsBinding {
 public:
  [[nodiscard]] static absl::StatusOr<std::unique_ptr<SettingsBinding>> Create(
      settings::Store& store,
      const std::shared_ptr<base::SequencedTaskRunner>& runner);

  SettingsBinding(const SettingsBinding&) = delete;
  SettingsBinding& operator=(const SettingsBinding&) = delete;
  ~SettingsBinding() = default;

 private:
  SettingsBinding() = default;

  settings::Subscription level_;
  settings::Subscription log_categories_;
  settings::Subscription trace_categories_;
};

}

#endif
