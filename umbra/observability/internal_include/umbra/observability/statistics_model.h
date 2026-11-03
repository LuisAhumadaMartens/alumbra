#ifndef UMBRA_OBSERVABILITY_STATISTICS_MODEL_H_
#define UMBRA_OBSERVABILITY_STATISTICS_MODEL_H_

#include <cstdint>
#include <memory>

#include "umbra/observability/metrics.h"
#include "umbra/player/statistics.h"

namespace umbra::observability {

class StatisticsModel final {
 public:
  explicit StatisticsModel(bool enabled = true);
  ~StatisticsModel();

  void OnMetricsEvent(const MetricsRecord& record);
  [[nodiscard]] player::StatisticsSnapshot GetSnapshot(
      bool include_unstable_debug = false) const;
  void SetEnabled(bool enabled);
  [[nodiscard]] bool IsEnabled() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
