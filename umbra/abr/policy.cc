#include "umbra/abr/policy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace umbra::abr {
namespace {

constexpr std::int64_t kNoLimit = std::numeric_limits<std::int64_t>::max();

bool SameFrameRate(const Rendition& left, const Rendition& right) {
  if (left.frame_rate_denominator <= 0 || right.frame_rate_denominator <= 0) {
    return left.frame_rate_numerator == right.frame_rate_numerator &&
           left.frame_rate_denominator == right.frame_rate_denominator;
  }
  return left.frame_rate_numerator * right.frame_rate_denominator ==
         right.frame_rate_numerator * left.frame_rate_denominator;
}

std::vector<Rendition> Eligible(const AbrSnapshot& snapshot,
                                const Rendition* current) {
  std::vector<Rendition> result;
  const std::int64_t max_bitrate = std::min(
      snapshot.max_bitrate_bps > 0 ? snapshot.max_bitrate_bps : kNoLimit,
      snapshot.cmsd_max_bitrate_bps > 0 ? snapshot.cmsd_max_bitrate_bps
                                        : kNoLimit);
  for (const Rendition& candidate : snapshot.candidates) {
    if (candidate.bitrate_bps <= 0 ||
        (snapshot.max_height > 0 && candidate.height > snapshot.max_height) ||
        candidate.bitrate_bps > max_bitrate ||
        (snapshot.decode_drop_max_height > 0 &&
         candidate.height > snapshot.decode_drop_max_height) ||
        !candidate.within_decoder_capability || !candidate.within_viewport ||
        !candidate.within_data_saver_limit || !candidate.within_cmsd_limit ||
        !candidate.within_decode_drop_cap ||
        (current != nullptr && !snapshot.force_frame_rate_change &&
         !SameFrameRate(candidate, *current))) {
      continue;
    }
    result.push_back(candidate);
  }
  std::ranges::sort(result, [](const Rendition& left, const Rendition& right) {
    if (left.bitrate_bps != right.bitrate_bps) {
      return left.bitrate_bps < right.bitrate_bps;
    }
    return left.id < right.id;
  });
  return result;
}

const Rendition* FindById(const std::vector<Rendition>& renditions,
                          std::uint64_t id) {
  const auto found = std::ranges::find_if(
      renditions,
      [id](const Rendition& rendition) { return rendition.id == id; });
  return found == renditions.end() ? nullptr : &*found;
}

const Rendition* ThroughputChoice(const std::vector<Rendition>& eligible,
                                  std::int64_t budget) {
  const Rendition* choice = &eligible.front();
  for (const Rendition& candidate : eligible) {
    if (candidate.bitrate_bps > budget) {
      break;
    }
    choice = &candidate;
  }
  return choice;
}

const Rendition* BolaChoice(const std::vector<Rendition>& eligible,
                            std::int64_t buffer_ms,
                            std::int64_t target_buffer_ms) {
  const std::int64_t minimum = eligible.front().bitrate_bps;
  if (minimum <= 0 || eligible.size() < 2 || target_buffer_ms <= 0) {
    return &eligible.front();
  }
  const double normalised_buffer = std::clamp(
      static_cast<double>(buffer_ms) / static_cast<double>(target_buffer_ms),
      0.0, 1.0);
  const Rendition* choice = &eligible.front();
  double best_utility = -std::numeric_limits<double>::infinity();
  for (const Rendition& candidate : eligible) {
    const double utility = std::log(static_cast<double>(candidate.bitrate_bps) /
                                    static_cast<double>(minimum));
    const double size = static_cast<double>(candidate.bitrate_bps) /
                        static_cast<double>(minimum);
    const double objective = (utility * normalised_buffer) -
                             ((1.0 - normalised_buffer) * (size - 1.0));
    if (objective > best_utility ||
        (objective == best_utility && candidate.score > choice->score)) {
      best_utility = objective;
      choice = &candidate;
    }
  }
  return choice;
}

class HybridAbrPolicy final : public AbrPolicy {
 public:
  explicit HybridAbrPolicy(AbrPolicyConfig config) : config_(config) {}

  [[nodiscard]] AbrDecision Decide(const AbrSnapshot& snapshot) const override {
    const Rendition* raw_current = nullptr;
    for (const Rendition& candidate : snapshot.candidates) {
      if (candidate.id == snapshot.current_rendition_id) {
        raw_current = &candidate;
        break;
      }
    }
    if (!config_.enabled) {
      return {.rendition_id = snapshot.current_rendition_id,
              .reason = AbrReason::kDisabled,
              .abandon_in_flight_segment = false};
    }
    const std::vector<Rendition> eligible = Eligible(snapshot, raw_current);
    if (eligible.empty()) {
      return {.rendition_id = snapshot.current_rendition_id,
              .reason = AbrReason::kNoCandidates,
              .abandon_in_flight_segment = false};
    }
    const Rendition* current =
        FindById(eligible, snapshot.current_rendition_id);
    const Rendition* lowest = &eligible.front();
    if (current == nullptr) {
      const Rendition* startup = ThroughputChoice(
          eligible,
          static_cast<std::int64_t>(static_cast<double>(std::max<std::int64_t>(
                                        0, snapshot.bandwidth_estimate_bps)) *
                                    config_.startup_safety_factor));
      return {.rendition_id = startup->id,
              .reason = raw_current == nullptr ? AbrReason::kStartup
                                               : AbrReason::kCurrentCapped,
              .abandon_in_flight_segment = false};
    }
    if (config_.emergency_down_switch && snapshot.buffer_ms >= 0 &&
        snapshot.segment_duration_ms > 0 &&
        snapshot.bandwidth_estimate_bps > 0 && current != lowest) {
      const long double projected_ms =
          static_cast<long double>(snapshot.segment_duration_ms) *
          static_cast<long double>(current->bitrate_bps) /
          static_cast<long double>(snapshot.bandwidth_estimate_bps);
      if (projected_ms > static_cast<long double>(snapshot.buffer_ms)) {
        const Rendition* safer = ThroughputChoice(
            eligible,
            static_cast<std::int64_t>(
                static_cast<long double>(snapshot.bandwidth_estimate_bps) *
                config_.throughput_safety_factor));
        if (safer->bitrate_bps < current->bitrate_bps) {
          return {.rendition_id = safer->id,
                  .reason = AbrReason::kEmergencyDownSwitch,
                  .abandon_in_flight_segment = true};
        }
      }
    }
    const bool insufficient_buffer = snapshot.buffer_ms < config_.low_buffer_ms;
    const Rendition* proposed =
        insufficient_buffer
            ? ThroughputChoice(eligible,
                               static_cast<std::int64_t>(
                                   static_cast<long double>(
                                       snapshot.bandwidth_estimate_bps) *
                                   config_.throughput_safety_factor))
            : BolaChoice(eligible, snapshot.buffer_ms,
                         config_.target_buffer_ms);
    if (proposed->id != current->id && snapshot.last_switch_ms >= 0 &&
        snapshot.now_ms >= snapshot.last_switch_ms &&
        snapshot.now_ms - snapshot.last_switch_ms <
            config_.minimum_switch_interval_ms) {
      return {.rendition_id = current->id,
              .reason = AbrReason::kHysteresis,
              .abandon_in_flight_segment = false};
    }
    if (proposed->bitrate_bps > current->bitrate_bps &&
        snapshot.buffer_ms < config_.switch_buffer_ms) {
      return {.rendition_id = current->id,
              .reason = AbrReason::kHysteresis,
              .abandon_in_flight_segment = false};
    }
    if (proposed->id == current->id) {
      return {.rendition_id = current->id,
              .reason = insufficient_buffer ? AbrReason::kInsufficientBuffer
                                            : AbrReason::kStable,
              .abandon_in_flight_segment = false};
    }
    return {.rendition_id = proposed->id,
            .reason = insufficient_buffer ? AbrReason::kThroughput
                                          : AbrReason::kBuffer,
            .abandon_in_flight_segment = false};
  }

 private:
  AbrPolicyConfig config_;
};

}

std::unique_ptr<AbrPolicy> CreateAbrPolicy(AbrPolicyConfig config) {
  return std::make_unique<HybridAbrPolicy>(config);
}

std::string_view ToString(AbrReason reason) {
  switch (reason) {
    case AbrReason::kDisabled:
      return "disabled";
    case AbrReason::kNoCandidates:
      return "no_candidates";
    case AbrReason::kCurrentCapped:
      return "current_capped";
    case AbrReason::kStartup:
      return "startup";
    case AbrReason::kEmergencyDownSwitch:
      return "emergency_down_switch";
    case AbrReason::kInsufficientBuffer:
      return "insufficient_buffer";
    case AbrReason::kThroughput:
      return "throughput";
    case AbrReason::kBuffer:
      return "buffer";
    case AbrReason::kHysteresis:
      return "hysteresis";
    case AbrReason::kStable:
      return "stable";
  }
  return "unknown";
}

}
