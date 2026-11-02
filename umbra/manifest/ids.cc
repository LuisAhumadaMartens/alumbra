#include "umbra/manifest/ids.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

namespace umbra::manifest {
namespace {

constexpr std::string_view kHexDigits = "0123456789ABCDEF";

bool IsPlain(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
         c == '~' || c == ':' || c == '@' || c == '+' || c == '=' || c == ',' ||
         c == ';';
}

std::string_view TypeKey(RenditionType type) {
  switch (type) {
    case RenditionType::kAudio:
      return "audio";
    case RenditionType::kSubtitles:
      return "subtitles";
    case RenditionType::kClosedCaptions:
      return "cc";
  }
  return "unknown";
}

std::string VariantKey(const HlsVariantKey& key) {
  return absl::StrCat(key.bandwidth, "/", key.width, "x", key.height, "/",
                      EncodeKeyPart(key.frame_rate), "/",
                      EncodeKeyPart(key.video_range), "/",
                      EncodeKeyPart(key.codecs), "/", key.ordinal);
}

}

std::string EncodeKeyPart(std::string_view part) {
  std::string encoded;
  encoded.reserve(part.size());
  for (const char c : part) {
    if (IsPlain(c)) {
      encoded.push_back(c);
    } else {
      const auto byte = static_cast<std::uint8_t>(c);
      encoded.push_back('%');
      encoded.push_back(kHexDigits[byte >> 4]);
      encoded.push_back(kHexDigits[byte & 0x0F]);
    }
  }
  return encoded;
}

PeriodId DashPeriodId(std::string_view period_id) {
  return PeriodId(absl::StrCat("dash/p/", EncodeKeyPart(period_id)));
}

PeriodId DashPeriodIdByIndex(std::size_t index) {
  return PeriodId(absl::StrCat("dash/i/", index));
}

SwitchingSetId DashSwitchingSetId(std::string_view adaptation_set_id) {
  return SwitchingSetId(
      absl::StrCat("dash/", EncodeKeyPart(adaptation_set_id)));
}

TrackId DashTrackId(std::string_view adaptation_set_id,
                    std::string_view representation_id) {
  return TrackId(absl::StrCat("dash/", EncodeKeyPart(adaptation_set_id), "/",
                              EncodeKeyPart(representation_id)));
}

PeriodId HlsPeriodId(std::int64_t discontinuity_sequence) {
  return PeriodId(absl::StrCat("hls/d/", discontinuity_sequence));
}

SwitchingSetId HlsVideoSwitchingSetId() { return SwitchingSetId("hls/video"); }

TrackId HlsVariantTrackId(const HlsVariantKey& key) {
  return TrackId(absl::StrCat("hls/v/", VariantKey(key)));
}

SwitchingSetId HlsRenditionSwitchingSetId(const HlsRenditionKey& key) {
  return SwitchingSetId(absl::StrCat(
      "hls/", TypeKey(key.type), "/", EncodeKeyPart(key.name), "/",
      EncodeKeyPart(key.language), "/", EncodeKeyPart(key.characteristics)));
}

TrackId HlsRenditionTrackId(const HlsRenditionKey& key) {
  return TrackId(absl::StrCat(
      "hls/", TypeKey(key.type), "/", EncodeKeyPart(key.group_id), "/",
      EncodeKeyPart(key.name), "/", EncodeKeyPart(key.language), "/",
      EncodeKeyPart(key.characteristics)));
}

TrackId HlsTrickPlayTrackId(const HlsVariantKey& key) {
  return TrackId(absl::StrCat("hls/t/", VariantKey(key)));
}

}
