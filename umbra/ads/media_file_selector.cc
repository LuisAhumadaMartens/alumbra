#include "umbra/ads/media_file_selector.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {
namespace {

std::string Lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

bool IsStreamingMime(std::string_view mime) {
  return mime == "application/vnd.apple.mpegurl" ||
         mime == "application/x-mpegurl" || mime == "application/dash+xml";
}

bool IsSupportedDelivery(std::string_view delivery) {
  return delivery == "progressive" || delivery == "streaming";
}

struct Candidate {
  std::size_t index = 0;
  bool preferred = false;
  std::int64_t size_distance = 0;
  std::uint64_t bitrate = 0;
};

bool Better(const Candidate& left, const Candidate& right) {
  if (left.preferred != right.preferred) {
    return left.preferred;
  }
  if (left.size_distance != right.size_distance) {
    return left.size_distance < right.size_distance;
  }
  return left.bitrate > right.bitrate;
}

}

std::optional<PreferredDelivery> ParsePreferredDelivery(
    std::string_view value) {
  if (value == "any") {
    return PreferredDelivery::kAny;
  }
  if (value == "progressive") {
    return PreferredDelivery::kProgressive;
  }
  if (value == "streaming") {
    return PreferredDelivery::kStreaming;
  }
  return std::nullopt;
}

MediaFileSelection SelectMediaFile(
    const VastCreative& creative, const MediaFileSelectionOptions& options,
    const penumbra::CapabilityProvider& capabilities) {
  std::optional<Candidate> best;
  for (std::size_t index = 0; index < creative.media_files.size(); ++index) {
    const VastMediaFile& file = creative.media_files[index];
    const std::string mime = Lower(file.type);
    const std::string delivery = Lower(file.delivery);
    if (file.mezzanine || file.url.empty() ||
        (!mime.starts_with("video/") && !IsStreamingMime(mime)) ||
        Lower(file.api_framework) == "vpaid" ||
        !IsSupportedDelivery(delivery)) {
      continue;
    }
    if (IsStreamingMime(mime) && delivery != "streaming") {
      continue;
    }
    const std::uint64_t bitrate = file.bitrate_bps.value_or(0);
    if ((options.max_bitrate_bps != 0 && bitrate > options.max_bitrate_bps) ||
        (options.estimated_bandwidth_bps != 0 &&
         bitrate > options.estimated_bandwidth_bps)) {
      continue;
    }
    penumbra::CapabilityRequest request;
    request.mime_type = mime;
    request.bitrate = static_cast<std::int64_t>(bitrate);
    request.stream.codec_string = file.codec;
    penumbra::VideoInfo video;
    video.coded_width = static_cast<std::int32_t>(file.width.value_or(0));
    video.coded_height = static_cast<std::int32_t>(file.height.value_or(0));
    video.display_width = video.coded_width;
    video.display_height = video.coded_height;
    request.stream.format = video;
    const auto capability = capabilities.QueryDecoding(request);
    if (!capability.ok() || !capability->supported || !capability->smooth) {
      continue;
    }
    const std::int64_t width_distance =
        static_cast<std::int64_t>(video.display_width) - options.viewport_width;
    const std::int64_t height_distance =
        static_cast<std::int64_t>(video.display_height) -
        options.viewport_height;
    const std::int64_t distance =
        std::abs(width_distance) + std::abs(height_distance);
    const bool preferred =
        options.preferred_delivery == PreferredDelivery::kAny ||
        (options.preferred_delivery == PreferredDelivery::kProgressive &&
         delivery == "progressive") ||
        (options.preferred_delivery == PreferredDelivery::kStreaming &&
         delivery == "streaming");
    const Candidate candidate{.index = index,
                              .preferred = preferred,
                              .size_distance = distance,
                              .bitrate = bitrate};
    if (!best || Better(candidate, *best)) {
      best = candidate;
    }
  }
  if (!best) {
    return {};
  }
  return {.index = best->index, .error_code = 0};
}

}
