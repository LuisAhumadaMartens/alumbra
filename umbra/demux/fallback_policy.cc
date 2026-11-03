#include "umbra/demux/fallback_policy.h"

#include <string_view>

#include "absl/status/status.h"
#include "umbra/observability/log.h"
#include "umbra/settings/settings_keys.h"

namespace umbra::demux {

FallbackPolicy::FallbackPolicy(settings::DemuxFfmpegMode mode,
                               bool slot_available)
    : mode_(mode), slot_available_(slot_available) {}

bool FallbackPolicy::SlotUsable() const {
  return slot_available_ && mode_ != settings::DemuxFfmpegMode::kOff;
}

ParserChoice FallbackPolicy::ChooseFirst(const ParserFacts& facts) const {
  if (SlotUsable() && facts.slot_accepts &&
      (!facts.has_own_parser ||
       (mode_ == settings::DemuxFfmpegMode::kPrefer && facts.progressive))) {
    return ParserChoice::kFallback;
  }
  return facts.has_own_parser ? ParserChoice::kOwn : ParserChoice::kUnsupported;
}

ParserChoice FallbackPolicy::ChooseAfterSlotRejection(
    const ParserFacts& facts) {
  ++stats_.slot_rejections;
  return facts.has_own_parser ? ParserChoice::kOwn : ParserChoice::kUnsupported;
}

ParserChoice FallbackPolicy::ChooseAfterOwnRejection(
    const absl::Status& rejection, bool sample_emitted,
    std::string_view container, const ParserFacts& facts) {
  if (rejection.ok() || sample_emitted || retried_ || !SlotUsable() ||
      !facts.slot_accepts) {
    return ParserChoice::kUnsupported;
  }
  retried_ = true;
  ++stats_.retries;
  ALUMBRA_LOG(kDemux, kWarning)
          .Field("container", container)
          .Field("status", rejection.ToString())
      << "own parser rejected the file before its first sample, retrying "
         "with the fallback demuxer";
  return ParserChoice::kFallback;
}

}
