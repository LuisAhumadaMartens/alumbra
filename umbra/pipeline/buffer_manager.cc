#include "umbra/pipeline/buffer_manager.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::pipeline {
namespace {

std::optional<penumbra::MediaTime> SampleTime(
    const penumbra::Timestamp& timestamp) {
  const auto time =
      penumbra::ToMediaTime(timestamp, penumbra::Rounding::kNearest);
  if (!time.has_value() || !time->is_finite()) {
    return std::nullopt;
  }
  return time;
}

}

BufferManager::BufferManager() = default;

BufferManager::BufferManager(Config config) : config_(config) {}

bool BufferManager::ValidateConfig(const Config& config) {
  return config.forward_goal_ms >= 1000 && config.forward_goal_ms <= 600000 &&
         config.back_goal_ms >= 0 && config.back_goal_ms <= 600000 &&
         config.start_threshold_ms >= 0 && config.start_threshold_ms <= 60000 &&
         config.rebuffer_threshold_ms >= 0 &&
         config.rebuffer_threshold_ms <= 60000 && config.video_max_bytes > 0 &&
         config.audio_max_bytes > 0;
}

bool BufferManager::SetConfig(Config config) {
  if (!ValidateConfig(config)) {
    return false;
  }
  config_ = config;
  return true;
}

void BufferManager::SetStreams(
    const std::vector<penumbra::StreamConfig>& streams) {
  tracks_.clear();
  for (const penumbra::StreamConfig& stream : streams) {
    tracks_[stream.info.track_id].type = stream.info.type();
  }
}

void BufferManager::SetPortBudgets(penumbra::BufferBudgets budgets) {
  port_budgets_ = budgets;
}

void BufferManager::SetMemoryPressure(penumbra::MemoryPressure pressure) {
  if (pressure == penumbra::MemoryPressure::kNormal) {
    pressure_.reset();
  } else {
    pressure_ = pressure;
  }
}

void BufferManager::Clear() { tracks_.clear(); }

void BufferManager::RecordSamples(
    const std::vector<penumbra::EncodedSample>& samples) {
  for (const penumbra::EncodedSample& sample : samples) {
    auto track = tracks_.find(sample.track_id);
    const auto start = SampleTime(sample.pts);
    if (track == tracks_.end() || !start.has_value()) {
      continue;
    }
    penumbra::MediaTime end = *start;
    const auto length =
        penumbra::ToTimeDelta(sample.duration, penumbra::Rounding::kNearest);
    if (length.has_value() && length->is_finite()) {
      end = end + *length;
    }
    const std::uint64_t bytes = sample.data.size();
    track->second.samples.push_back({.start = *start,
                                     .end = end,
                                     .bytes = bytes,
                                     .key_frame = sample.key_frame});
    track->second.bytes += bytes;
    const std::int64_t duration_ms =
        std::max<std::int64_t>(1, (end - *start).InMicroseconds() / 1000);
    const std::uint64_t rate =
        bytes > std::numeric_limits<std::uint64_t>::max() / 1000U
            ? std::numeric_limits<std::uint64_t>::max()
            : bytes * 1000U / static_cast<std::uint64_t>(duration_ms);
    track->second.bytes_per_second =
        std::max(track->second.bytes_per_second, rate);
  }
}

std::uint64_t BufferManager::EvictBehind(penumbra::MediaTime position) {
  if (!position.is_finite()) {
    return 0;
  }
  std::uint64_t evicted = EvictBackBuffer(position);
  const penumbra::MediaTime protected_end =
      position +
      penumbra::TimeDelta::FromMilliseconds(config_.rebuffer_threshold_ms);
  for (const penumbra::StreamType type :
       {penumbra::StreamType::kAudio, penumbra::StreamType::kVideo}) {
    evicted += EvictForward(type, protected_end);
  }
  return evicted;
}

std::uint64_t BufferManager::EvictBackBuffer(penumbra::MediaTime position) {
  std::uint64_t evicted = 0;
  for (auto& [track_id, track] : tracks_) {
    static_cast<void>(track_id);
    const penumbra::MediaTime cutoff =
        position - penumbra::TimeDelta::FromMilliseconds(config_.back_goal_ms);
    penumbra::MediaTime keyframe = penumbra::MediaTime::Unknown();
    for (const SampleRecord& sample : track.samples) {
      if (sample.key_frame && sample.start <= cutoff) {
        keyframe = sample.start;
      }
    }
    const penumbra::MediaTime eviction_end =
        keyframe.is_finite() ? keyframe : cutoff;
    while (!track.samples.empty() &&
           track.samples.front().end <= eviction_end) {
      evicted += track.samples.front().bytes;
      track.bytes -= track.samples.front().bytes;
      track.samples.pop_front();
    }
  }
  return evicted;
}

std::uint64_t BufferManager::EvictForward(penumbra::StreamType type,
                                          penumbra::MediaTime protected_end) {
  std::uint64_t evicted = 0;
  const std::uint64_t budget = BudgetFor(type);
  while (BytesFor(type) > budget) {
    auto candidate = tracks_.end();
    for (auto track = tracks_.begin(); track != tracks_.end(); ++track) {
      if (track->second.type != type || track->second.samples.empty() ||
          track->second.samples.back().start < protected_end) {
        continue;
      }
      if (candidate == tracks_.end() ||
          track->second.samples.back().start >
              candidate->second.samples.back().start) {
        candidate = track;
      }
    }
    if (candidate == tracks_.end()) {
      break;
    }
    evicted += candidate->second.samples.back().bytes;
    candidate->second.bytes -= candidate->second.samples.back().bytes;
    candidate->second.samples.pop_back();
  }
  return evicted;
}

bool BufferManager::CanFetch(std::uint32_t track_id,
                             penumbra::MediaTime position,
                             penumbra::MediaTime buffered_end,
                             std::int64_t next_segment_ms) const {
  const auto track = tracks_.find(track_id);
  if (track == tracks_.end() || !position.is_finite() || next_segment_ms <= 0) {
    return false;
  }
  const std::int64_t ahead_ms =
      buffered_end.is_finite()
          ? std::max<std::int64_t>(
                0, (buffered_end - position).InMicroseconds() / 1000)
          : 0;
  const std::int64_t goal_ms = EffectiveForwardGoalMs();
  if (ahead_ms >= goal_ms) {
    return false;
  }
  const std::uint64_t budget = BudgetFor(track->second.type);
  const std::uint64_t projected =
      ProjectedBytes(track->second, next_segment_ms);
  const std::uint64_t accounted = BytesFor(track->second.type);
  if (projected <= budget && accounted <= budget - projected) {
    return true;
  }
  return accounted <= budget;
}

BufferManager::Accounting BufferManager::GetAccounting() const {
  Accounting accounting;
  for (const auto& [track_id, track] : tracks_) {
    static_cast<void>(track_id);
    if (track.type == penumbra::StreamType::kAudio) {
      accounting.audio_bytes += track.bytes;
    } else if (track.type == penumbra::StreamType::kVideo) {
      accounting.video_bytes += track.bytes;
    }
  }
  return accounting;
}

std::int64_t BufferManager::EffectiveForwardGoalMs() const {
  if (pressure_ == penumbra::MemoryPressure::kModerate) {
    return std::max<std::int64_t>(1000, config_.forward_goal_ms * 3 / 4);
  }
  if (pressure_ == penumbra::MemoryPressure::kCritical) {
    return std::max<std::int64_t>(1000, config_.forward_goal_ms / 2);
  }
  return config_.forward_goal_ms;
}

std::uint64_t BufferManager::BudgetFor(penumbra::StreamType type) const {
  const std::uint64_t configured = type == penumbra::StreamType::kAudio
                                       ? config_.audio_max_bytes
                                       : config_.video_max_bytes;
  const std::optional<std::uint64_t>& port_budget =
      type == penumbra::StreamType::kAudio ? port_budgets_.audio_max_bytes
                                           : port_budgets_.video_max_bytes;
  const std::uint64_t effective =
      port_budget.has_value() ? std::min(configured, *port_budget) : configured;
  if (pressure_ == penumbra::MemoryPressure::kModerate) {
    return (effective / 4 * 3) + ((effective % 4 * 3) / 4);
  }
  if (pressure_ == penumbra::MemoryPressure::kCritical) {
    return std::max<std::uint64_t>(1, effective / 2);
  }
  return effective;
}

std::uint64_t BufferManager::BytesFor(penumbra::StreamType type) const {
  std::uint64_t total = 0;
  for (const auto& [track_id, track] : tracks_) {
    static_cast<void>(track_id);
    if (track.type == type) {
      total += track.bytes;
    }
  }
  return total;
}

std::uint64_t BufferManager::ProjectedBytes(const TrackLedger& track,
                                            std::int64_t duration_ms) {
  const auto duration = static_cast<std::uint64_t>(duration_ms);
  if (track.bytes_per_second >
      std::numeric_limits<std::uint64_t>::max() / duration) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return track.bytes_per_second * duration / 1000U;
}

}
