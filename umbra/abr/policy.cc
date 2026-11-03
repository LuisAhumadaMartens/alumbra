#include "umbra/abr/policy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/abr/rendition_filter.h"

namespace umbra::abr {
namespace {

bool SameFrameRate(const Rendition& left, const Rendition& right) {
  if (left.frame_rate_denominator <= 0 || right.frame_rate_denominator <= 0) {
    return left.frame_rate_numerator == right.frame_rate_numerator &&
           left.frame_rate_denominator == right.frame_rate_denominator;
  }
  return left.frame_rate_numerator * right.frame_rate_denominator ==
         right.frame_rate_numerator * left.frame_rate_denominator;
}

std::vector<Rendition> Eligible(
    const AbrSnapshot& snapshot,
    const std::vector<std::size_t>& filtered_indices,
    const Rendition* current) {
  std::vector<Rendition> result;
  for (const std::size_t index : filtered_indices) {
    if (index >= snapshot.candidates.size()) {
      continue;
    }
    const Rendition& candidate = snapshot.candidates[index];
    if (candidate.bitrate_bps <= 0 ||
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

absl::StatusOr<FilterResult> FilterSnapshot(const AbrSnapshot& snapshot) {
  std::vector<RenditionDescriptor> descriptors;
  descriptors.reserve(snapshot.candidates.size());
  std::vector<RenditionCapability> capabilities =
      snapshot.rendition_capabilities;
  if (capabilities.empty()) {
    capabilities.reserve(snapshot.candidates.size());
  }
  for (const Rendition& candidate : snapshot.candidates) {
    const std::string id = candidate.filter_id.empty()
                               ? std::to_string(candidate.id)
                               : candidate.filter_id;
    descriptors.push_back(
        RenditionDescriptor{.id = id,
                            .height = candidate.height,
                            .bitrate_bps = candidate.bitrate_bps});
    if (snapshot.rendition_capabilities.empty()) {
      capabilities.push_back(
          RenditionCapability{.id = id, .supported = true, .smooth = true});
    }
  }
  return FilterRenditions(descriptors, capabilities, snapshot.filter_policy);
}

AbrDecision MakeDecision(
    std::uint64_t rendition_id, AbrReason reason, bool abandon_in_flight,
    const std::vector<ExcludedRendition>& excluded_renditions) {
  return AbrDecision{.rendition_id = rendition_id,
                     .reason = reason,
                     .abandon_in_flight_segment = abandon_in_flight,
                     .excluded_renditions = excluded_renditions};
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

const Rendition* FilteredCurrent(const AbrSnapshot& snapshot,
                                 const FilterResult& filtered) {
  for (const std::size_t index : filtered.eligible_indices) {
    if (index < snapshot.candidates.size() &&
        snapshot.candidates[index].id == snapshot.current_rendition_id) {
      return &snapshot.candidates[index];
    }
  }
  return nullptr;
}

AbrDecision StartupDecision(const AbrSnapshot& snapshot,
                            const AbrPolicyConfig& config,
                            const Rendition* raw_current,
                            const std::vector<Rendition>& eligible,
                            const std::vector<ExcludedRendition>& excluded) {
  const std::int64_t conservative_budget =
      static_cast<std::int64_t>(static_cast<double>(std::max<std::int64_t>(
                                    0, snapshot.bandwidth_estimate_bps)) *
                                config.startup_safety_factor);
  const Rendition* startup = ThroughputChoice(eligible, conservative_budget);
  return MakeDecision(
      startup->id,
      raw_current == nullptr ? AbrReason::kStartup : AbrReason::kCurrentCapped,
      false, excluded);
}

std::optional<AbrDecision> EmergencyDownSwitch(
    const AbrSnapshot& snapshot, const AbrPolicyConfig& config,
    const Rendition* current, const Rendition* lowest,
    const std::vector<Rendition>& eligible,
    const std::vector<ExcludedRendition>& excluded) {
  if (!config.emergency_down_switch || snapshot.buffer_ms < 0 ||
      snapshot.segment_duration_ms <= 0 ||
      snapshot.bandwidth_estimate_bps <= 0 || current == lowest) {
    return std::nullopt;
  }
  const long double projected_ms =
      static_cast<long double>(snapshot.segment_duration_ms) *
      static_cast<long double>(current->bitrate_bps) /
      static_cast<long double>(snapshot.bandwidth_estimate_bps);
  if (projected_ms <= static_cast<long double>(snapshot.buffer_ms)) {
    return std::nullopt;
  }
  const auto budget = static_cast<std::int64_t>(
      static_cast<long double>(snapshot.bandwidth_estimate_bps) *
      config.throughput_safety_factor);
  const Rendition* safer = ThroughputChoice(eligible, budget);
  if (safer->bitrate_bps >= current->bitrate_bps) {
    return std::nullopt;
  }
  return MakeDecision(safer->id, AbrReason::kEmergencyDownSwitch, true,
                      excluded);
}

AbrDecision SteadyDecision(const AbrSnapshot& snapshot,
                           const AbrPolicyConfig& config,
                           const Rendition* current,
                           const std::vector<Rendition>& eligible,
                           const std::vector<ExcludedRendition>& excluded) {
  const bool insufficient_buffer = snapshot.buffer_ms < config.low_buffer_ms;
  const auto throughput_budget = static_cast<std::int64_t>(
      static_cast<long double>(snapshot.bandwidth_estimate_bps) *
      config.throughput_safety_factor);
  const Rendition* proposed =
      insufficient_buffer
          ? ThroughputChoice(eligible, throughput_budget)
          : BolaChoice(eligible, snapshot.buffer_ms, config.target_buffer_ms);
  const bool inside_switch_interval =
      proposed->id != current->id && snapshot.last_switch_ms >= 0 &&
      snapshot.now_ms >= snapshot.last_switch_ms &&
      snapshot.now_ms - snapshot.last_switch_ms <
          config.minimum_switch_interval_ms;
  const bool up_switch_needs_buffer =
      proposed->bitrate_bps > current->bitrate_bps &&
      snapshot.buffer_ms < config.switch_buffer_ms;
  if (inside_switch_interval || up_switch_needs_buffer) {
    return MakeDecision(current->id, AbrReason::kHysteresis, false, excluded);
  }
  if (proposed->id == current->id) {
    return MakeDecision(current->id,
                        insufficient_buffer ? AbrReason::kInsufficientBuffer
                                            : AbrReason::kStable,
                        false, excluded);
  }
  return MakeDecision(
      proposed->id,
      insufficient_buffer ? AbrReason::kThroughput : AbrReason::kBuffer, false,
      excluded);
}

class HybridAbrPolicy final : public AbrPolicy {
 public:
  explicit HybridAbrPolicy(AbrPolicyConfig config) : config_(config) {}

  [[nodiscard]] AbrDecision Decide(const AbrSnapshot& snapshot) const override {
    const Rendition* raw_current =
        FindById(snapshot.candidates, snapshot.current_rendition_id);
    const auto filter_result = FilterSnapshot(snapshot);
    if (!filter_result.ok()) {
      return MakeDecision(0, AbrReason::kNoCandidates, false, {});
    }
    const FilterResult& filtered = *filter_result;
    const std::vector<Rendition> eligible =
        Eligible(snapshot, filtered.eligible_indices,
                 FilteredCurrent(snapshot, filtered));
    if (eligible.empty()) {
      return MakeDecision(0, AbrReason::kNoCandidates, false,
                          filtered.excluded);
    }
    const Rendition* current =
        FindById(eligible, snapshot.current_rendition_id);
    const Rendition* lowest = &eligible.front();
    if (!config_.enabled) {
      if (current != nullptr) {
        return MakeDecision(current->id, AbrReason::kDisabled, false,
                            filtered.excluded);
      }
      return MakeDecision(lowest->id, AbrReason::kCurrentCapped, false,
                          filtered.excluded);
    }
    if (current == nullptr) {
      return StartupDecision(snapshot, config_, raw_current, eligible,
                             filtered.excluded);
    }
    if (std::optional<AbrDecision> emergency = EmergencyDownSwitch(
            snapshot, config_, current, lowest, eligible, filtered.excluded);
        emergency.has_value()) {
      return *emergency;
    }
    return SteadyDecision(snapshot, config_, current, eligible,
                          filtered.excluded);
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
