#include "penumbra/reference_player/media_clock.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace penumbra {
namespace {

constexpr int64_t kResyncThresholdUs = 60'000;
constexpr double kMaxCorrectionRate = 0.0025;

int64_t Clamp(int64_t value) {
  return std::clamp(
      value,
      MediaTime::FromMicroseconds(std::numeric_limits<int64_t>::min() + 2)
          .InMicroseconds(),
      MediaTime::FromMicroseconds(std::numeric_limits<int64_t>::max() - 1)
          .InMicroseconds());
}

int64_t Advance(int64_t position, int64_t elapsed_us, double rate) {
  const long double amount = static_cast<long double>(elapsed_us) * rate;
  if (amount > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return std::numeric_limits<int64_t>::max() - 1;
  }
  if (amount < static_cast<long double>(std::numeric_limits<int64_t>::min())) {
    return std::numeric_limits<int64_t>::min() + 2;
  }
  return Clamp(position + static_cast<int64_t>(amount));
}

}

MediaClock::MediaClock() = default;

MediaClock::State MediaClock::ReadState() const {
  State state;
  for (;;) {
    const uint64_t before = sequence_.load(std::memory_order_acquire);
    if ((before & 1U) != 0U) {
      continue;
    }
    state.anchor_monotonic_us =
        anchor_monotonic_us_.load(std::memory_order_relaxed);
    state.anchor_media_us = anchor_media_us_.load(std::memory_order_relaxed);
    state.last_media_us = last_media_us_.load(std::memory_order_relaxed);
    state.audio_media_us = audio_media_us_.load(std::memory_order_relaxed);
    state.audio_observed_us =
        audio_observed_us_.load(std::memory_order_relaxed);
    state.latency_us = latency_us_.load(std::memory_order_relaxed);
    state.offset_us = offset_us_.load(std::memory_order_relaxed);
    state.slew_start_us = slew_start_us_.load(std::memory_order_relaxed);
    state.slew_end_us = slew_end_us_.load(std::memory_order_relaxed);
    state.slew_from_us = slew_from_us_.load(std::memory_order_relaxed);
    state.slew_to_us = slew_to_us_.load(std::memory_order_relaxed);
    state.playback_rate = playback_rate_.load(std::memory_order_relaxed);
    state.resample_ratio = resample_ratio_.load(std::memory_order_relaxed);
    state.audio_active = audio_active_.load(std::memory_order_relaxed);
    state.paused = paused_.load(std::memory_order_relaxed);
    if (before == sequence_.load(std::memory_order_acquire)) {
      return state;
    }
  }
}

void MediaClock::WriteState(const State& state) {
  sequence_.fetch_add(1, std::memory_order_acq_rel);
  anchor_monotonic_us_.store(state.anchor_monotonic_us,
                             std::memory_order_relaxed);
  anchor_media_us_.store(state.anchor_media_us, std::memory_order_relaxed);
  last_media_us_.store(std::max(last_media_us_.load(std::memory_order_relaxed),
                                state.last_media_us),
                       std::memory_order_relaxed);
  audio_media_us_.store(state.audio_media_us, std::memory_order_relaxed);
  audio_observed_us_.store(state.audio_observed_us, std::memory_order_relaxed);
  latency_us_.store(state.latency_us, std::memory_order_relaxed);
  offset_us_.store(state.offset_us, std::memory_order_relaxed);
  slew_start_us_.store(state.slew_start_us, std::memory_order_relaxed);
  slew_end_us_.store(state.slew_end_us, std::memory_order_relaxed);
  slew_from_us_.store(state.slew_from_us, std::memory_order_relaxed);
  slew_to_us_.store(state.slew_to_us, std::memory_order_relaxed);
  playback_rate_.store(state.playback_rate, std::memory_order_relaxed);
  resample_ratio_.store(state.resample_ratio, std::memory_order_relaxed);
  audio_active_.store(state.audio_active, std::memory_order_relaxed);
  paused_.store(state.paused, std::memory_order_relaxed);
  sequence_.fetch_add(1, std::memory_order_release);
}

void MediaClock::Reanchor(State& state, int64_t monotonic_us,
                          int64_t media_us) const {
  const int64_t current = Now(monotonic_us).InMicroseconds();
  state.anchor_monotonic_us = monotonic_us;
  state.anchor_media_us = std::max(current, media_us);
  state.last_media_us = state.anchor_media_us;
}

void MediaClock::Reset(MediaTime position, int64_t monotonic_us) {
  const int64_t media_us = position.is_finite() ? position.InMicroseconds() : 0;
  State state = ReadState();
  state.anchor_monotonic_us = monotonic_us;
  state.anchor_media_us = media_us;
  state.last_media_us = media_us;
  state.audio_media_us = media_us;
  state.audio_observed_us = monotonic_us;
  state.latency_us = 0;
  state.offset_us = 0;
  state.slew_start_us = 0;
  state.slew_end_us = 0;
  state.slew_from_us = 0;
  state.slew_to_us = 0;
  state.audio_active = false;
  state.paused = true;
  WriteState(state);
  last_media_us_.store(media_us, std::memory_order_relaxed);
}

void MediaClock::SetRate(double rate, int64_t monotonic_us) {
  if (!std::isfinite(rate) || rate < 0.0) {
    return;
  }
  State state = ReadState();
  Reanchor(state, monotonic_us, Now(monotonic_us).InMicroseconds());
  state.playback_rate = rate;
  WriteState(state);
}

void MediaClock::SetResampleRatio(double ratio, int64_t monotonic_us) {
  if (!std::isfinite(ratio) || ratio <= 0.0) {
    return;
  }
  State state = ReadState();
  Reanchor(state, monotonic_us, Now(monotonic_us).InMicroseconds());
  state.resample_ratio = ratio;
  WriteState(state);
}

void MediaClock::SetPaused(bool paused, int64_t monotonic_us) {
  State state = ReadState();
  Reanchor(state, monotonic_us, Now(monotonic_us).InMicroseconds());
  state.paused = paused;
  WriteState(state);
}

void MediaClock::SetAudioActive(bool active, int64_t monotonic_us) {
  State state = ReadState();
  Reanchor(state, monotonic_us, Now(monotonic_us).InMicroseconds());
  state.audio_active = active;
  WriteState(state);
}

void MediaClock::UpdateAudio(const AudioClockUpdate& update,
                             int64_t monotonic_us) {
  if (!update.media_time.is_finite()) {
    return;
  }
  State state = ReadState();
  const int64_t latency = update.output_latency.is_finite()
                              ? update.output_latency.InMicroseconds()
                              : state.latency_us;
  const int64_t offset = update.audio_offset.is_finite()
                             ? update.audio_offset.InMicroseconds()
                             : state.offset_us;
  const int64_t sample_age =
      std::max<int64_t>(0, monotonic_us - update.observed_at_us);
  const int64_t raw =
      Advance(Clamp(update.media_time.InMicroseconds() - latency - offset),
              sample_age, state.playback_rate * state.resample_ratio);
  const int64_t current = Now(monotonic_us).InMicroseconds();
  const int64_t elapsed =
      std::max<int64_t>(0, monotonic_us - state.anchor_monotonic_us);
  const int64_t difference = raw - current;
  if (std::abs(difference) > kResyncThresholdUs) {
    if (difference > 0) {
      state.anchor_media_us = raw;
    } else {
      state.anchor_media_us = current;
    }
    state.anchor_monotonic_us = monotonic_us;
  } else if (!state.paused && state.playback_rate > 0.0) {
    const auto correction_limit =
        static_cast<int64_t>(static_cast<double>(elapsed) *
                             state.playback_rate * kMaxCorrectionRate);
    const int64_t correction =
        std::clamp(difference, -correction_limit, correction_limit);
    state.anchor_media_us = Clamp(state.anchor_media_us + correction);
    state.anchor_monotonic_us = monotonic_us;
  }
  state.audio_media_us = update.media_time.InMicroseconds();
  state.audio_observed_us = update.observed_at_us;
  state.latency_us = latency;
  state.offset_us = offset;
  state.audio_active = true;
  state.last_media_us = std::max(state.last_media_us, current);
  WriteState(state);
}

void MediaClock::SlewAudioLatency(TimeDelta latency, TimeDelta audio_offset,
                                  int64_t monotonic_us, TimeDelta duration) {
  if (!latency.is_finite() || !audio_offset.is_finite() ||
      !duration.is_finite() || duration <= TimeDelta::Zero()) {
    return;
  }
  State state = ReadState();
  const int64_t now_us = Now(monotonic_us).InMicroseconds();
  state.slew_start_us = monotonic_us;
  state.slew_end_us = monotonic_us + duration.InMicroseconds();
  state.slew_from_us = state.latency_us + state.offset_us;
  state.slew_to_us = latency.InMicroseconds() + audio_offset.InMicroseconds();
  state.latency_us = latency.InMicroseconds();
  state.offset_us = audio_offset.InMicroseconds();
  state.anchor_monotonic_us = monotonic_us;
  state.anchor_media_us = now_us;
  WriteState(state);
}

MediaTime MediaClock::Now(int64_t monotonic_us) const {
  const State state = ReadState();
  const int64_t elapsed =
      state.paused || monotonic_us <= state.anchor_monotonic_us
          ? 0
          : monotonic_us - state.anchor_monotonic_us;
  const int64_t clock_us = Advance(state.anchor_media_us, elapsed,
                                   state.playback_rate * state.resample_ratio);
  int64_t result = clock_us;
  if (state.audio_active && !state.paused &&
      monotonic_us >= state.audio_observed_us) {
    int64_t compensation = state.latency_us + state.offset_us;
    if (state.slew_end_us > state.slew_start_us &&
        monotonic_us < state.slew_end_us) {
      const double progress = std::clamp(
          static_cast<double>(monotonic_us - state.slew_start_us) /
              static_cast<double>(state.slew_end_us - state.slew_start_us),
          0.0, 1.0);
      compensation =
          state.slew_from_us +
          static_cast<int64_t>(
              static_cast<double>(state.slew_to_us - state.slew_from_us) *
              progress);
    }
    const int64_t audible = Advance(state.audio_media_us - compensation,
                                    monotonic_us - state.audio_observed_us,
                                    state.playback_rate * state.resample_ratio);
    const int64_t difference = audible - clock_us;
    const auto correction_limit =
        static_cast<int64_t>(static_cast<double>(elapsed) *
                             state.playback_rate * kMaxCorrectionRate);
    result =
        clock_us + std::clamp(difference, -correction_limit, correction_limit);
  }
  result = std::max(result, state.last_media_us);
  int64_t previous = last_media_us_.load(std::memory_order_relaxed);
  while (result > previous &&
         !last_media_us_.compare_exchange_weak(previous, result,
                                               std::memory_order_relaxed)) {
  }
  return MediaTime::FromMicroseconds(std::max(result, previous));
}

double MediaClock::Rate() const {
  const State state = ReadState();
  return state.playback_rate * state.resample_ratio;
}

}
