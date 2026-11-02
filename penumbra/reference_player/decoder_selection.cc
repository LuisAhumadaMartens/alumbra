#include "penumbra/reference_player/decoder_selection.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/reference_player/decoder_blocklist.h"

namespace penumbra {
namespace {

bool IsKnownCodecName(std::string_view name) {
  for (uint8_t value = 1; value <= static_cast<uint8_t>(CodecId::kMp3);
       ++value) {
    if (CodecName(static_cast<CodecId>(value)) == name) {
      return true;
    }
  }
  return false;
}

bool ExtraExclusionMatches(std::string_view item, std::string_view decoder,
                           CodecId codec) {
  const size_t separator = item.rfind(':');
  if (separator != std::string_view::npos &&
      IsKnownCodecName(item.substr(separator + 1))) {
    return item.substr(0, separator) == decoder &&
           item.substr(separator + 1) == CodecName(codec);
  }
  return item == decoder;
}

bool Supports(const DecoderCandidate& candidate, const StreamInfo& info) {
  if (std::ranges::find(candidate.codecs, info.codec) ==
      candidate.codecs.end()) {
    return false;
  }
  if (const auto* video = std::get_if<VideoInfo>(&info.format)) {
    if (candidate.max_width > 0 && video->coded_width > candidate.max_width) {
      return false;
    }
    if (candidate.max_height > 0 &&
        video->coded_height > candidate.max_height) {
      return false;
    }
  }
  return true;
}

int KindRank(DecoderKind kind) { return static_cast<int>(kind); }

bool HardwareAllowed(const DecoderCandidate& candidate,
                     const SelectionRequest& request) {
  if (request.require_secure && candidate.secure) {
    return true;
  }
  switch (request.settings.prefer_hardware) {
    case HardwarePreference::kAuto:
      return true;
    case HardwarePreference::kNever:
      return candidate.kind != DecoderKind::kHardware;
    case HardwarePreference::kAlways:
      return candidate.kind == DecoderKind::kHardware;
  }
  return true;
}

}

std::vector<DecoderCandidate> SelectDecoderCandidates(
    std::vector<DecoderCandidate> available, const SelectionRequest& request) {
  const auto& info = request.stream.info;
  const auto& settings = request.settings;
  std::vector<DecoderCandidate> selected;
  for (auto& candidate : available) {
    if (candidate.name.empty() || !Supports(candidate, info) ||
        (request.require_secure && !candidate.secure) ||
        !HardwareAllowed(candidate, request)) {
      continue;
    }
    if (FindBlocklistEntry(request.blocklist, candidate.name, info.codec,
                           request.device,
                           settings.blocklist_ignored_entries) != nullptr) {
      continue;
    }
    const bool excluded = std::ranges::any_of(
        settings.blocklist_extra_decoders, [&](const std::string& item) {
          return ExtraExclusionMatches(item, candidate.name, info.codec);
        });
    if (!excluded) {
      selected.push_back(std::move(candidate));
    }
  }
  std::ranges::stable_sort(selected, [](const DecoderCandidate& left,
                                        const DecoderCandidate& right) {
    return KindRank(left.kind) < KindRank(right.kind);
  });
  size_t front = 0;
  for (const auto& preferred : settings.preferred_decoders) {
    const auto match =
        std::find_if(selected.begin() + static_cast<std::ptrdiff_t>(front),
                     selected.end(), [&](const DecoderCandidate& candidate) {
                       return candidate.name == preferred;
                     });
    if (match != selected.end()) {
      std::rotate(selected.begin() + static_cast<std::ptrdiff_t>(front), match,
                  match + 1);
      ++front;
    }
  }
  return selected;
}

}
