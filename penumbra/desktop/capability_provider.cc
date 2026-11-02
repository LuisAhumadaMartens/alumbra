#include "penumbra/desktop/capability_provider.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/codec_string.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/desktop/decoder_table.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

constexpr size_t kMaxCachedQueries = 512;
constexpr double kAssumedFrameRate = 60.0;

constexpr std::array<std::string_view, 14> kContainerTypes = {{
    "video/mp4",
    "audio/mp4",
    "video/webm",
    "audio/webm",
    "video/mp2t",
    "video/x-matroska",
    "audio/x-matroska",
    "audio/mpeg",
    "audio/mp3",
    "audio/flac",
    "audio/ogg",
    "audio/aac",
    "audio/x-flac",
    "application/mp4",
}};

std::string ContainerOf(std::string_view mime_type) {
  std::string base(mime_type.substr(0, mime_type.find(';')));
  const auto first = base.find_first_not_of(" \t");
  const auto last = base.find_last_not_of(" \t");
  base = first == std::string::npos ? "" : base.substr(first, last - first + 1);
  std::ranges::transform(base, base.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return base;
}

bool KnownContainer(const std::string& container) {
  return container.empty() ||
         std::ranges::find(kContainerTypes, container) != kContainerTypes.end();
}

std::string CacheKey(const CapabilityRequest& request) {
  const StreamInfo& stream = request.stream;
  std::string key = request.mime_type;
  key += '|';
  key += stream.codec_string;
  key += '|';
  key += std::to_string(static_cast<int>(stream.codec));
  key += '|';
  key += request.key_system;
  key += '|';
  key += request.hdr_format;
  key += '|';
  key += std::to_string(request.bitrate);
  key += '|';
  key += request.encryption_scheme.has_value() ? "1" : "0";
  if (const auto* video = std::get_if<VideoInfo>(&stream.format)) {
    key += "|v";
    key += std::to_string(video->coded_width);
    key += 'x';
    key += std::to_string(video->coded_height);
    key += 'x';
    key += std::to_string(video->display_width);
    key += 'x';
    key += std::to_string(video->display_height);
    key += '@';
    key += video->frame_rate.ToString();
  } else if (std::holds_alternative<AudioInfo>(stream.format)) {
    key += "|a";
  } else {
    key += "|t";
  }
  return key;
}

absl::Status Validate(const CapabilityRequest& request) {
  if (request.bitrate < 0) {
    return absl::InvalidArgumentError("bitrate must not be negative");
  }
  if (const auto* video = std::get_if<VideoInfo>(&request.stream.format)) {
    if (video->coded_width < 0 || video->coded_height < 0 ||
        video->display_width < 0 || video->display_height < 0) {
      return absl::InvalidArgumentError("video size must not be negative");
    }
    if (video->frame_rate.numerator() < 0) {
      return absl::InvalidArgumentError("frame rate must not be negative");
    }
  }
  return absl::OkStatus();
}

int64_t Height(const VideoInfo& video) {
  return video.coded_height > 0 ? video.coded_height : video.display_height;
}

int64_t Width(const VideoInfo& video) {
  return video.coded_width > 0 ? video.coded_width : video.display_width;
}

bool IsSmooth(const DecoderEntry& entry, const VideoInfo& video) {
  const int64_t width = Width(video);
  const int64_t height = Height(video);
  if (width <= 0 || height <= 0) {
    return false;
  }
  double frame_rate = kAssumedFrameRate;
  if (video.frame_rate.is_positive()) {
    frame_rate = static_cast<double>(video.frame_rate.numerator()) /
                 static_cast<double>(video.frame_rate.denominator());
  }
  const double rate = static_cast<double>(width * height) * frame_rate;
  return rate <= static_cast<double>(entry.smooth_luma_samples_per_second);
}

}

DesktopCapabilityProvider::DesktopCapabilityProvider(
    DecodeLimits limits, std::shared_ptr<const DecoderProbe> probe)
    : limits_(std::move(limits)), probe_(std::move(probe)) {}

bool DesktopCapabilityProvider::IsDisabled(
    std::string_view settings_name) const {
  return std::ranges::find(limits_.disabled_decoders, settings_name) !=
         limits_.disabled_decoders.end();
}

absl::StatusOr<DecodingCapability> DesktopCapabilityProvider::QueryDecoding(
    const CapabilityRequest& request) const {
  if (absl::Status status = Validate(request); !status.ok()) {
    return status;
  }
  const std::string key = CacheKey(request);
  {
    const std::scoped_lock lock(mutex_);
    const auto found = cache_.find(key);
    if (found != cache_.end()) {
      return found->second;
    }
  }
  absl::StatusOr<DecodingCapability> result = Evaluate(request);
  if (!result.ok()) {
    return result;
  }
  const std::scoped_lock lock(mutex_);
  if (cache_.size() >= kMaxCachedQueries) {
    cache_.clear();
  }
  cache_.emplace(key, *result);
  return result;
}

absl::StatusOr<DecodingCapability> DesktopCapabilityProvider::Evaluate(
    const CapabilityRequest& request) const {
  const DecodingCapability unsupported;
  const StreamInfo& stream = request.stream;
  if (!KnownContainer(ContainerOf(request.mime_type)) ||
      !request.key_system.empty() || request.encryption_scheme.has_value() ||
      !request.hdr_format.empty() || stream.type() == StreamType::kText) {
    return unsupported;
  }
  ParsedCodec parsed{.codec = stream.codec};
  if (!stream.codec_string.empty()) {
    const auto from_string = ParseCodecString(stream.codec_string);
    if (!from_string.has_value() || (stream.codec != CodecId::kUnknown &&
                                     stream.codec != from_string->codec)) {
      return unsupported;
    }
    parsed = *from_string;
  }
  const DecoderEntry* entry = FindDecoderEntry(parsed.codec);
  if (entry == nullptr || IsDisabled(entry->settings_name) ||
      !entry->accepts(parsed)) {
    return unsupported;
  }
  const bool video_codec = entry->smooth_luma_samples_per_second > 0;
  if (video_codec != (stream.type() == StreamType::kVideo)) {
    return unsupported;
  }
  const auto* video = std::get_if<VideoInfo>(&stream.format);
  if (video != nullptr && limits_.max_video_height > 0 &&
      Height(*video) > limits_.max_video_height) {
    return unsupported;
  }
  absl::StatusOr<bool> available = probe_->IsCompiledIn(entry->decoder_name);
  if (!available.ok()) {
    return available.status();
  }
  if (!*available) {
    return unsupported;
  }
  DecodingCapability capability;
  capability.supported = true;
  capability.smooth = video == nullptr || IsSmooth(*entry, *video);
  capability.power_efficient = false;
  capability.preferred_output_mode = OutputMode::kTexture;
  capability.accepts_fragments = false;
  return capability;
}

absl::StatusOr<bool> DesktopCapabilityProvider::CanChangeType(
    [[maybe_unused]] const StreamConfig& from,
    [[maybe_unused]] const StreamConfig& to) const {
  return false;
}

}
