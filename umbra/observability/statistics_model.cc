#include "umbra/observability/statistics_model.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

#include "umbra/observability/metrics.h"
#include "umbra/player/statistics.h"

namespace umbra::observability {

class StatisticsModel::Impl {
 public:
  explicit Impl(bool enabled) : enabled_(enabled) {}

  void Record(const MetricsRecord& record) {
    const std::scoped_lock lock(mutex_);
    if (!enabled_) {
      return;
    }
    ++events_received_;
    last_event_timestamp_ms_ = record.timestamp_ms;
    std::visit([this](const auto& event) { ApplyEvent(event); }, record.event);
  }

  void ApplyEvent(const PlaybackStart& event) {
    if (event.initial_startup_time_ms >= 0) {
      startup_ms_ += event.initial_startup_time_ms;
      ++startup_samples_;
    }
  }

  void ApplyEvent(const PlaybackStall& event) {
    if (event.stall_duration_ms >= 0) {
      ++stalls_;
      stalled_ms_ += event.stall_duration_ms;
    }
  }

  void ApplyEvent(const RenditionUpdate& event) {
    if (event.from_bitrate_bps != event.to_bitrate_bps) {
      ++rendition_switches_;
    }
  }

  void ApplyEvent(const FramesDropped& event) {
    dropped_frames_ += event.count;
  }

  void ApplyEvent(const PlaybackFailed& event) {
    session_failed_ = true;
    if (event.during_startup) {
      ++startup_failures_;
    }
  }

  void ApplyEvent(const BitrateSample& event) {
    if (event.bits_played >= 0 && event.media_time_ms > 0) {
      bits_played_ += event.bits_played;
      watched_ms_ += event.media_time_ms;
      segment_bitrate_sum_ += static_cast<double>(event.bits_played) * 1000.0 /
                              static_cast<double>(event.media_time_ms);
      ++segment_bitrate_samples_;
    }
  }

  void ApplyEvent(const LiveLatencySample& event) {
    if (event.latency_ms >= 0) {
      live_latency_ms_ = event.latency_ms;
      has_live_latency_ = true;
    }
  }

  void ApplyEvent(const PlaybackSessionEnded& event) {
    if (event.watched_time_ms > 0) {
      watched_time_ms_ += event.watched_time_ms;
    }
    ++sessions_;
    if (session_failed_) {
      ++failed_sessions_;
      session_failed_ = false;
    }
  }

  player::StatisticsSnapshot Snapshot(bool include_unstable_debug) const {
    const std::scoped_lock lock(mutex_);
    player::StatisticsSnapshot result{};
    result.average_initial_startup_time_s.value =
        startup_samples_ == 0
            ? std::optional<double>()
            : std::optional<double>(
                  static_cast<double>(startup_ms_) /
                  (1000.0 * static_cast<double>(startup_samples_)));
    result.startup_failures.value = startup_failures_;
    result.playback_failure_percentage.value =
        sessions_ == 0 ? std::optional<double>()
                       : std::optional<double>(
                             100.0 * static_cast<double>(failed_sessions_) /
                             static_cast<double>(sessions_));
    result.average_playback_stalled_count.value =
        sessions_ == 0 ? std::optional<double>()
                       : std::optional<double>(static_cast<double>(stalls_) /
                                               static_cast<double>(sessions_));
    result.average_stalled_time_percentage.value =
        watched_time_ms_ == 0
            ? std::optional<double>()
            : std::optional<double>(100.0 * static_cast<double>(stalled_ms_) /
                                    static_cast<double>(watched_time_ms_));
    result.average_playback_bitrate_kbps.value =
        watched_ms_ == 0
            ? std::optional<double>()
            : std::optional<double>(static_cast<double>(bits_played_) /
                                    static_cast<double>(watched_ms_));
    result.average_segment_bitrate_bps.value =
        segment_bitrate_samples_ == 0
            ? std::optional<double>()
            : std::optional<double>(
                  segment_bitrate_sum_ /
                  static_cast<double>(segment_bitrate_samples_));
    result.rendition_switch_count.value = rendition_switches_;
    result.dropped_frames.value = dropped_frames_;
    result.live_latency_ms.value = has_live_latency_
                                       ? std::optional<double>(live_latency_ms_)
                                       : std::optional<double>();
    if (include_unstable_debug) {
      result.unstable_debug = player::UnstableDebugStatistics{};
      result.unstable_debug->events_received.value = events_received_;
      result.unstable_debug->last_event_timestamp_ms.value =
          last_event_timestamp_ms_;
    }
    return result;
  }

  void SetEnabled(bool enabled) {
    const std::scoped_lock lock(mutex_);
    enabled_ = enabled;
    if (!enabled_) {
      startup_samples_ = 0;
      startup_ms_ = 0;
      events_received_ = 0;
      startup_failures_ = 0;
      failed_sessions_ = 0;
      session_failed_ = false;
      stalls_ = 0;
      stalled_ms_ = 0;
      watched_time_ms_ = 0;
      sessions_ = 0;
      bits_played_ = 0;
      watched_ms_ = 0;
      segment_bitrate_sum_ = 0;
      segment_bitrate_samples_ = 0;
      rendition_switches_ = 0;
      dropped_frames_ = 0;
      live_latency_ms_ = 0;
      has_live_latency_ = false;
      last_event_timestamp_ms_ = 0;
    }
  }

  bool IsEnabled() const {
    const std::scoped_lock lock(mutex_);
    return enabled_;
  }

 private:
  mutable std::mutex mutex_;
  bool enabled_;
  std::uint64_t startup_samples_ = 0;
  std::uint64_t startup_failures_ = 0;
  std::uint64_t failed_sessions_ = 0;
  std::uint64_t stalls_ = 0;
  std::uint64_t watched_time_ms_ = 0;
  std::uint64_t sessions_ = 0;
  std::uint64_t rendition_switches_ = 0;
  std::uint64_t dropped_frames_ = 0;
  std::uint64_t segment_bitrate_samples_ = 0;
  std::uint64_t events_received_ = 0;
  std::int64_t startup_ms_ = 0;
  std::int64_t stalled_ms_ = 0;
  std::int64_t bits_played_ = 0;
  std::int64_t watched_ms_ = 0;
  std::int64_t live_latency_ms_ = 0;
  std::int64_t last_event_timestamp_ms_ = 0;
  double segment_bitrate_sum_ = 0;
  bool has_live_latency_ = false;
  bool session_failed_ = false;
};

StatisticsModel::StatisticsModel(bool enabled)
    : impl_(std::make_unique<Impl>(enabled)) {}
StatisticsModel::~StatisticsModel() = default;
void StatisticsModel::OnMetricsEvent(const MetricsRecord& record) {
  impl_->Record(record);
}
player::StatisticsSnapshot StatisticsModel::GetSnapshot(
    bool include_unstable_debug) const {
  return impl_->Snapshot(include_unstable_debug);
}
void StatisticsModel::SetEnabled(bool enabled) { impl_->SetEnabled(enabled); }
bool StatisticsModel::IsEnabled() const { return impl_->IsEnabled(); }

}
