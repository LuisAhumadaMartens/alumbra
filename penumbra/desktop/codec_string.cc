#include "penumbra/desktop/codec_string.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

std::vector<std::string_view> Split(std::string_view text) {
  std::vector<std::string_view> parts;
  size_t start = 0;
  while (true) {
    const size_t dot = text.find('.', start);
    if (dot == std::string_view::npos) {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, dot - start));
    start = dot + 1;
  }
}

std::optional<int> DigitValue(char c, int base) {
  int digit = -1;
  if (c >= '0' && c <= '9') {
    digit = c - '0';
  } else if (c >= 'a' && c <= 'f') {
    digit = c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    digit = c - 'A' + 10;
  }
  if (digit < 0 || digit >= base) {
    return std::nullopt;
  }
  return digit;
}

std::optional<int> ParseNumber(std::string_view text, int base) {
  constexpr size_t kMaxDigits = 6;
  if (text.empty() || text.size() > kMaxDigits) {
    return std::nullopt;
  }
  int value = 0;
  for (const char c : text) {
    const std::optional<int> digit = DigitValue(c, base);
    if (!digit.has_value()) {
      return std::nullopt;
    }
    value = (value * base) + *digit;
  }
  return value;
}

std::optional<ParsedCodec> ParseAvc(
    const std::vector<std::string_view>& parts) {
  ParsedCodec parsed{.codec = CodecId::kH264};
  if (parts.size() == 1) {
    return parsed;
  }
  if (parts.size() != 2 || parts[1].size() != 6) {
    return std::nullopt;
  }
  const auto profile = ParseNumber(parts[1].substr(0, 2), 16);
  const auto constraints = ParseNumber(parts[1].substr(2, 2), 16);
  const auto level = ParseNumber(parts[1].substr(4, 2), 16);
  if (!profile || !constraints || !level) {
    return std::nullopt;
  }
  parsed.profile = *profile;
  parsed.level = *level;
  return parsed;
}

std::optional<ParsedCodec> ParseHevc(
    const std::vector<std::string_view>& parts) {
  ParsedCodec parsed{.codec = CodecId::kHevc};
  if (parts.size() == 1) {
    return parsed;
  }
  if (parts.size() < 4) {
    return std::nullopt;
  }
  std::string_view profile = parts[1];
  if (!profile.empty() && (profile.front() == 'A' || profile.front() == 'B' ||
                           profile.front() == 'C')) {
    profile.remove_prefix(1);
  }
  const std::string_view tier_level = parts[3];
  if (tier_level.size() < 2 ||
      (tier_level.front() != 'L' && tier_level.front() != 'H')) {
    return std::nullopt;
  }
  const auto idc = ParseNumber(profile, 10);
  const auto level = ParseNumber(tier_level.substr(1), 10);
  if (!idc || !level) {
    return std::nullopt;
  }
  parsed.profile = *idc;
  parsed.level = *level;
  return parsed;
}

std::optional<ParsedCodec> ParseVp9(
    const std::vector<std::string_view>& parts) {
  ParsedCodec parsed{.codec = CodecId::kVp9};
  if (parts.size() == 1) {
    return parsed;
  }
  if (parts.size() < 4) {
    return std::nullopt;
  }
  const auto profile = ParseNumber(parts[1], 10);
  const auto level = ParseNumber(parts[2], 10);
  const auto depth = ParseNumber(parts[3], 10);
  if (!profile || !level || !depth) {
    return std::nullopt;
  }
  parsed.profile = *profile;
  parsed.level = *level;
  parsed.bit_depth = *depth;
  return parsed;
}

std::optional<ParsedCodec> ParseAv1(
    const std::vector<std::string_view>& parts) {
  ParsedCodec parsed{.codec = CodecId::kAv1};
  if (parts.size() == 1) {
    return parsed;
  }
  if (parts.size() < 4 || parts[2].size() < 3) {
    return std::nullopt;
  }
  const char tier = parts[2].back();
  if (tier != 'M' && tier != 'H') {
    return std::nullopt;
  }
  const auto profile = ParseNumber(parts[1], 10);
  const auto level = ParseNumber(parts[2].substr(0, parts[2].size() - 1), 10);
  const auto depth = ParseNumber(parts[3], 10);
  if (!profile || !level || !depth) {
    return std::nullopt;
  }
  parsed.profile = *profile;
  parsed.level = *level;
  parsed.bit_depth = *depth;
  return parsed;
}

std::optional<ParsedCodec> ParseMp4a(
    const std::vector<std::string_view>& parts) {
  if (parts.size() < 2) {
    return std::nullopt;
  }
  const auto object_type = ParseNumber(parts[1], 16);
  if (!object_type) {
    return std::nullopt;
  }
  ParsedCodec parsed;
  switch (*object_type) {
    case 0x40:
      parsed.codec = CodecId::kAac;
      if (parts.size() > 2) {
        const auto audio_object = ParseNumber(parts[2], 10);
        if (!audio_object) {
          return std::nullopt;
        }
        parsed.profile = *audio_object;
      }
      return parsed;
    case 0x69:
    case 0x6B:
      parsed.codec = CodecId::kMp3;
      return parsed;
    case 0xA5:
      parsed.codec = CodecId::kAc3;
      return parsed;
    case 0xA6:
      parsed.codec = CodecId::kEac3;
      return parsed;
    default:
      return std::nullopt;
  }
}

}

std::optional<ParsedCodec> ParseCodecString(std::string_view codec_string) {
  const std::vector<std::string_view> parts = Split(codec_string);
  const std::string_view family = parts.front();
  if (family == "avc1" || family == "avc3") {
    return ParseAvc(parts);
  }
  if (family == "hvc1" || family == "hev1") {
    return ParseHevc(parts);
  }
  if (family == "vp09") {
    return ParseVp9(parts);
  }
  if (family == "av01") {
    return ParseAv1(parts);
  }
  if (family == "mp4a") {
    return ParseMp4a(parts);
  }
  if (parts.size() != 1) {
    return std::nullopt;
  }
  if (family == "opus") {
    return ParsedCodec{.codec = CodecId::kOpus};
  }
  if (family == "ac-3") {
    return ParsedCodec{.codec = CodecId::kAc3};
  }
  if (family == "ec-3") {
    return ParsedCodec{.codec = CodecId::kEac3};
  }
  if (family == "flac") {
    return ParsedCodec{.codec = CodecId::kFlac};
  }
  if (family == "mp3") {
    return ParsedCodec{.codec = CodecId::kMp3};
  }
  return std::nullopt;
}

}
