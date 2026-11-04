#include "umbra/abr/rendition_filter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/presentation.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::abr {
namespace {

std::string MimeType(const manifest::Track& track) {
  if (const auto* extension =
          std::get_if<manifest::DashTrackExtension>(&track.extension);
      extension != nullptr && extension->mime_type.has_value()) {
    return *extension->mime_type;
  }
  return "video/mp4";
}

std::string HdrFormat(manifest::VideoRange range, bool dolby_vision) {
  if (dolby_vision) {
    return "dolby-vision";
  }
  switch (range) {
    case manifest::VideoRange::kPq:
      return "pq";
    case manifest::VideoRange::kHlg:
      return "hlg";
    case manifest::VideoRange::kUnspecified:
    case manifest::VideoRange::kSdr:
      return "";
  }
  return "";
}

bool IsVideoCodec(penumbra::CodecId codec_id) {
  switch (codec_id) {
    case penumbra::CodecId::kH264:
    case penumbra::CodecId::kHevc:
    case penumbra::CodecId::kVp8:
    case penumbra::CodecId::kVp9:
    case penumbra::CodecId::kTheora:
    case penumbra::CodecId::kAv1:
      return true;
    case penumbra::CodecId::kUnknown:
    case penumbra::CodecId::kAac:
    case penumbra::CodecId::kVorbis:
    case penumbra::CodecId::kOpus:
    case penumbra::CodecId::kAc3:
    case penumbra::CodecId::kEac3:
    case penumbra::CodecId::kFlac:
    case penumbra::CodecId::kMp3:
      return false;
  }
  return false;
}

penumbra::CapabilityRequest MakeCapabilityRequest(
    const manifest::Track& track) {
  penumbra::CapabilityRequest request;
  request.mime_type = MimeType(track);
  request.bitrate = track.bandwidth_bps.value_or(0);
  penumbra::VideoInfo video;
  if (const auto* format =
          std::get_if<manifest::VideoTrackFormat>(&track.format)) {
    video.coded_width = format->width;
    video.coded_height = format->height;
    video.display_width = format->width;
    video.display_height = format->height;
    if (format->frame_rate.has_value()) {
      video.frame_rate = *format->frame_rate;
    }
    video.color = format->color;
    video.dolby_vision = format->dolby_vision;
    request.hdr_format =
        HdrFormat(format->range, format->dolby_vision.has_value());
  }
  request.stream.track_id = 0;
  request.stream.format = video;
  for (const auto& codec : track.codecs) {
    const penumbra::CodecId codec_id = codec::CodecIdOf(codec);
    if (IsVideoCodec(codec_id)) {
      request.stream.codec = codec_id;
      request.stream.codec_string = codec::FormatCodecString(codec);
      break;
    }
  }
  return request;
}

const RenditionCapability* FindCapability(
    std::span<const RenditionCapability> capabilities, std::string_view id) {
  const auto found = std::ranges::find_if(
      capabilities,
      [id](const auto& capability) { return capability.id == id; });
  return found == capabilities.end() ? nullptr : &*found;
}

std::int32_t ViewportCap(std::span<const RenditionDescriptor> renditions,
                         std::span<const RenditionCapability> capabilities,
                         const FilterPolicy& policy) {
  if (!policy.cap_to_viewport || policy.viewport_height_pixels == 0) {
    return 0;
  }
  std::int32_t smallest_sufficient = 0;
  std::int32_t largest_available = 0;
  for (const RenditionDescriptor& rendition : renditions) {
    const RenditionCapability* capability =
        FindCapability(capabilities, rendition.id);
    if (capability == nullptr || !capability->supported ||
        !capability->smooth || rendition.height <= 0) {
      continue;
    }
    largest_available = std::max(largest_available, rendition.height);
    if (rendition.height >= policy.viewport_height_pixels &&
        (smallest_sufficient == 0 || rendition.height < smallest_sufficient)) {
      smallest_sufficient = rendition.height;
    }
  }
  return smallest_sufficient == 0 ? largest_available : smallest_sufficient;
}

std::optional<FilterReason> FindExclusionReason(
    const RenditionDescriptor& rendition, const RenditionCapability* capability,
    std::int32_t viewport_cap, const FilterPolicy& policy) {
  if (capability == nullptr || !capability->supported) {
    return FilterReason::kUnsupported;
  }
  if (!capability->smooth) {
    return FilterReason::kNotSmooth;
  }
  if (policy.manual_selection.has_value() &&
      rendition.id != *policy.manual_selection) {
    return FilterReason::kManualSelection;
  }
  if (policy.max_height > 0 && rendition.height > policy.max_height) {
    return FilterReason::kMaximumHeight;
  }
  if (policy.max_bitrate_bps > 0 &&
      rendition.bitrate_bps > policy.max_bitrate_bps) {
    return FilterReason::kMaximumBitrate;
  }
  if (policy.data_saver_max_bitrate_bps.has_value() &&
      *policy.data_saver_max_bitrate_bps > 0 &&
      rendition.bitrate_bps > *policy.data_saver_max_bitrate_bps) {
    return FilterReason::kDataSaver;
  }
  if (policy.cmsd_max_bitrate_bps > 0 &&
      rendition.bitrate_bps > policy.cmsd_max_bitrate_bps) {
    return FilterReason::kCmsd;
  }
  if (policy.decode_drop_max_height > 0 &&
      rendition.height > policy.decode_drop_max_height) {
    return FilterReason::kDecodeDropCap;
  }
  if (viewport_cap > 0 && rendition.height > viewport_cap) {
    return FilterReason::kViewport;
  }
  return std::nullopt;
}

}

FilterPolicy ReadFilterPolicy(const settings::Snapshot& load_settings,
                              bool cap_to_viewport,
                              std::int32_t viewport_height_pixels,
                              std::optional<std::string> manual_selection) {
  return FilterPolicy{
      .max_height =
          static_cast<std::int32_t>(load_settings.Get(settings::kAbrMaxHeight)),
      .max_bitrate_bps = load_settings.Get(settings::kAbrMaxBitrateBps),
      .data_saver_max_bitrate_bps = std::nullopt,
      .cmsd_max_bitrate_bps = 0,
      .decode_drop_max_height = 0,
      .cap_to_viewport = cap_to_viewport,
      .viewport_height_pixels = cap_to_viewport ? viewport_height_pixels : 0,
      .manual_selection = std::move(manual_selection)};
}

absl::StatusOr<std::vector<RenditionCapability>> QueryRenditionCapabilities(
    std::span<const manifest::Track* const> tracks,
    const penumbra::CapabilityProvider& capabilities) {
  std::vector<RenditionCapability> results;
  results.reserve(tracks.size());
  for (const manifest::Track* track : tracks) {
    if (track == nullptr || track->type() != penumbra::StreamType::kVideo) {
      return absl::InvalidArgumentError("rendition must be a video track");
    }
    auto result = capabilities.QueryDecoding(MakeCapabilityRequest(*track));
    if (!result.ok()) {
      return result.status();
    }
    results.push_back(RenditionCapability{.id = track->id.Value(),
                                          .supported = result->supported,
                                          .smooth = result->smooth});
  }
  return results;
}

absl::StatusOr<FilterResult> FilterRenditions(
    std::span<const RenditionDescriptor> renditions,
    std::span<const RenditionCapability> capabilities,
    const FilterPolicy& policy) {
  if (policy.max_height < 0 || policy.max_bitrate_bps < 0 ||
      (policy.data_saver_max_bitrate_bps.has_value() &&
       *policy.data_saver_max_bitrate_bps < 0) ||
      policy.cmsd_max_bitrate_bps < 0 || policy.decode_drop_max_height < 0 ||
      policy.viewport_height_pixels < 0) {
    return absl::InvalidArgumentError("rendition caps must not be negative");
  }

  const std::int32_t viewport_cap =
      ViewportCap(renditions, capabilities, policy);

  FilterResult result;
  result.eligible_indices.reserve(renditions.size());
  result.excluded.reserve(renditions.size());
  for (std::size_t index = 0; index < renditions.size(); ++index) {
    const RenditionDescriptor& rendition = renditions[index];
    if (rendition.id.empty() || rendition.height < 0 ||
        rendition.bitrate_bps < 0) {
      return absl::InvalidArgumentError("rendition fields are invalid");
    }
    const RenditionCapability* capability =
        FindCapability(capabilities, rendition.id);
    const std::optional<FilterReason> reason =
        FindExclusionReason(rendition, capability, viewport_cap, policy);
    if (reason.has_value()) {
      result.excluded.push_back(
          ExcludedRendition{.id = rendition.id, .reason = *reason});
    } else {
      result.eligible_indices.push_back(index);
    }
  }
  return result;
}

}
