#ifndef UMBRA_PLAYER_STATISTICS_H_
#define UMBRA_PLAYER_STATISTICS_H_

#include <cstdint>
#include <optional>

namespace umbra::player {

enum class MetricStability {
  kStable,
  kUnstable,
};

template <typename T>
struct Statistic {
  std::optional<T> value;
  MetricStability stability = MetricStability::kStable;
};

struct UnstableDebugStatistics {
  Statistic<std::uint64_t> events_received{
      .value = std::nullopt, .stability = MetricStability::kUnstable};
  Statistic<std::int64_t> last_event_timestamp_ms{
      .value = std::nullopt, .stability = MetricStability::kUnstable};
};

struct StatisticsSnapshot {
  Statistic<double> average_initial_startup_time_s;
  Statistic<std::uint64_t> startup_failures;
  Statistic<double> playback_failure_percentage;
  Statistic<double> average_playback_stalled_count;
  Statistic<double> average_stalled_time_percentage;
  Statistic<double> average_playback_bitrate_kbps;
  Statistic<double> average_segment_bitrate_bps;
  Statistic<std::uint64_t> rendition_switch_count;
  Statistic<std::uint64_t> dropped_frames;
  Statistic<double> live_latency_ms;
  std::optional<UnstableDebugStatistics> unstable_debug;
};

}

#endif
