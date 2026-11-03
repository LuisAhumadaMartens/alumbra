#ifndef UMBRA_DEMUX_FALLBACK_POLICY_H_
#define UMBRA_DEMUX_FALLBACK_POLICY_H_

#include <cstdint>
#include <string_view>

#include "absl/status/status.h"
#include "umbra/settings/settings_keys.h"

namespace umbra::demux {

enum class ParserChoice : uint8_t {
  kOwn,
  kFallback,
  kUnsupported,
};

struct FallbackStats {
  uint64_t retries = 0;
  uint64_t slot_rejections = 0;
};

struct ParserFacts {
  bool has_own_parser = false;
  bool slot_accepts = false;
  bool progressive = true;
};

class FallbackPolicy {
 public:
  FallbackPolicy(settings::DemuxFfmpegMode mode, bool slot_available);

  [[nodiscard]] ParserChoice ChooseFirst(const ParserFacts& facts) const;

  [[nodiscard]] ParserChoice ChooseAfterSlotRejection(const ParserFacts& facts);

  [[nodiscard]] ParserChoice ChooseAfterOwnRejection(
      const absl::Status& rejection, bool sample_emitted,
      std::string_view container, const ParserFacts& facts);

  [[nodiscard]] const FallbackStats& Stats() const { return stats_; }
  [[nodiscard]] settings::DemuxFfmpegMode Mode() const { return mode_; }
  [[nodiscard]] bool SlotAvailable() const { return slot_available_; }

 private:
  [[nodiscard]] bool SlotUsable() const;

  settings::DemuxFfmpegMode mode_;
  bool slot_available_;
  bool retried_ = false;
  FallbackStats stats_;
};

}

#endif
