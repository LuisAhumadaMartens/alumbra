#include "umbra/observability/metrics.h"

#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "umbra/base/lock.h"
#include "umbra/observability/log.h"

namespace umbra::observability {
namespace {

struct Config {
  base::Lock lock;
  std::shared_ptr<MetricsSink> sink ABSL_GUARDED_BY(lock);
};

Config& GetConfig() {
  static Config config;
  return config;
}

}

void SetMetricsSink(std::shared_ptr<MetricsSink> sink) {
  Config& config = GetConfig();
  const base::AutoLock lock(config.lock);
  config.sink = std::move(sink);
}

void ReportMetric(const MetricsEvent& event) {
  Config& config = GetConfig();
  std::shared_ptr<MetricsSink> sink;
  {
    const base::AutoLock lock(config.lock);
    sink = config.sink;
  }
  if (sink) {
    sink->OnMetricsEvent({.event = event, .timestamp_ms = internal::NowMs()});
  }
}

}
