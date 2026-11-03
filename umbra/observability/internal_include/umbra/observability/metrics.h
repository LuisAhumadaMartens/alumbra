#ifndef UMBRA_OBSERVABILITY_METRICS_H_
#define UMBRA_OBSERVABILITY_METRICS_H_

#include <cstdint>
#include <memory>
#include <variant>

namespace umbra::observability {

class StatisticsModel;

struct PlaybackStart {
  std::int64_t initial_startup_time_ms;
};

struct PlaybackStall {
  std::int64_t stall_duration_ms;
};

struct RenditionUpdate {
  std::int64_t from_bitrate_bps;
  std::int64_t to_bitrate_bps;
};

struct FramesDropped {
  std::uint64_t count;
};

struct PlaybackFailed {
  std::int32_t error_code;
  bool during_startup;
};

struct BitrateSample {
  std::int64_t bits_played;
  std::int64_t media_time_ms;
};

struct LiveLatencySample {
  std::int64_t latency_ms;
};

struct PlaybackSessionEnded {
  std::int64_t watched_time_ms;
};

struct BandwidthEstimateMetric {
  std::int64_t bandwidth_bps;
  std::int64_t ttfb_ms;
  std::uint64_t sample_count;
};

using MetricsEvent = std::variant<PlaybackStart, PlaybackStall, RenditionUpdate,
                                  FramesDropped, PlaybackFailed, BitrateSample,
                                  LiveLatencySample, PlaybackSessionEnded,
                                  BandwidthEstimateMetric>;

struct MetricsRecord {
  MetricsEvent event;
  std::int64_t timestamp_ms;
};

class MetricsSink {
 public:
  virtual ~MetricsSink() = default;
  virtual void OnMetricsEvent(const MetricsRecord& record) = 0;
};

void SetMetricsSink(std::shared_ptr<MetricsSink> sink);
void ReportMetric(const MetricsEvent& event);
void ReportMetric(StatisticsModel& statistics, const MetricsEvent& event);

}

#endif
