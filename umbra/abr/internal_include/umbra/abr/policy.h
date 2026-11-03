#ifndef UMBRA_ABR_POLICY_H_
#define UMBRA_ABR_POLICY_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace umbra::abr {

struct Rendition {
  std::uint64_t id = 0;
  std::int64_t bitrate_bps = 0;
  std::int32_t height = 0;
  std::int64_t frame_rate_numerator = 0;
  std::int64_t frame_rate_denominator = 1;
  double score = 0.0;
  bool within_decoder_capability = true;
  bool within_viewport = true;
  bool within_data_saver_limit = true;
  bool within_cmsd_limit = true;
  bool within_decode_drop_cap = true;
};

struct AbrSnapshot {
  std::int64_t bandwidth_estimate_bps = 0;
  std::int64_t buffer_ms = 0;
  std::int64_t now_ms = 0;
  std::int64_t last_switch_ms = -1;
  std::int64_t segment_duration_ms = 0;
  std::uint64_t current_rendition_id = 0;
  std::vector<Rendition> candidates;
  std::int32_t max_height = 0;
  std::int64_t max_bitrate_bps = 0;
  std::int64_t cmsd_max_bitrate_bps = 0;
  std::int64_t decode_drop_max_height = 0;
  bool force_frame_rate_change = false;
};

struct AbrPolicyConfig {
  bool enabled = true;
  bool emergency_down_switch = true;
  std::int64_t switch_buffer_ms = 8000;
  std::int64_t low_buffer_ms = 10000;
  std::int64_t target_buffer_ms = 30000;
  std::int64_t minimum_switch_interval_ms = 3000;
  double throughput_safety_factor = 0.85;
  double startup_safety_factor = 0.70;
};

enum class AbrReason {
  kDisabled,
  kNoCandidates,
  kCurrentCapped,
  kStartup,
  kEmergencyDownSwitch,
  kInsufficientBuffer,
  kThroughput,
  kBuffer,
  kHysteresis,
  kStable,
};

struct AbrDecision {
  std::uint64_t rendition_id = 0;
  AbrReason reason = AbrReason::kNoCandidates;
  bool abandon_in_flight_segment = false;
};

class AbrPolicy {
 public:
  virtual ~AbrPolicy() = default;
  [[nodiscard]] virtual AbrDecision Decide(
      const AbrSnapshot& snapshot) const = 0;
};

[[nodiscard]] std::unique_ptr<AbrPolicy> CreateAbrPolicy(
    AbrPolicyConfig config = {});
[[nodiscard]] std::string_view ToString(AbrReason reason);

}

#endif
