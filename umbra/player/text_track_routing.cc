#include "umbra/player/text_track_routing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/player/ass_parser.h"
#include "umbra/player/text_track.h"
#include "umbra/player/webvtt_parser.h"
#include "umbra/text/ttml_parser.h"

namespace umbra::player::internal {
namespace {

constexpr std::uint64_t kMpegTsModulus = std::uint64_t{1} << 33;
constexpr double kMpegTsRate = 90000.0;

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r' || value.front() == '\n')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                            value.back() == '\r' || value.back() == '\n')) {
    value.remove_suffix(1);
  }
  return value;
}

std::optional<std::uint64_t> ParseUnsigned(std::string_view value);

std::optional<double> ParseVttTime(std::string_view value) {
  value = Trim(value);
  const std::size_t first = value.find(':');
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t second = value.find(':', first + 1);
  const std::size_t dot = value.find(
      '.', second == std::string_view::npos ? first + 1 : second + 1);
  if (second == std::string_view::npos || dot == std::string_view::npos ||
      dot <= second + 1 || value.size() - dot != 4) {
    return std::nullopt;
  }
  const std::optional<std::uint64_t> hours =
      ParseUnsigned(value.substr(0, first));
  const std::optional<std::uint64_t> minutes =
      ParseUnsigned(value.substr(first + 1, second - first - 1));
  const std::optional<std::uint64_t> seconds =
      ParseUnsigned(value.substr(second + 1, dot - second - 1));
  const std::optional<std::uint64_t> millis =
      ParseUnsigned(value.substr(dot + 1));
  if (!hours.has_value() || !minutes.has_value() || !seconds.has_value() ||
      !millis.has_value() || *minutes > 59 || *seconds > 59) {
    return std::nullopt;
  }
  const auto hour_value = static_cast<long double>(*hours);
  const auto minute_value = static_cast<long double>(*minutes);
  const auto second_value = static_cast<long double>(*seconds);
  const auto millisecond_value = static_cast<long double>(*millis);
  const long double total_seconds =
      (((hour_value * 60.0L) + minute_value) * 60.0L) + second_value +
      (millisecond_value / 1000.0L);
  const auto result = static_cast<double>(total_seconds);
  return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

std::optional<std::uint64_t> ParseUnsigned(std::string_view value) {
  if (value.empty()) {
    return std::nullopt;
  }
  std::uint64_t result = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
      return std::nullopt;
    }
    result = (result * 10) + digit;
  }
  return result;
}

std::optional<std::string_view> TimestampMapFields(std::string_view bytes) {
  constexpr std::string_view kPrefix = "X-TIMESTAMP-MAP=";
  while (!bytes.empty()) {
    const std::size_t line_end = bytes.find_first_of("\r\n");
    const std::string_view header = Trim(bytes.substr(0, line_end));
    if (header.empty()) {
      return std::nullopt;
    }
    if (header.starts_with(kPrefix)) {
      return header.substr(kPrefix.size());
    }
    if (line_end == std::string_view::npos) {
      return std::nullopt;
    }
    std::size_t next = line_end + 1;
    if (bytes[line_end] == '\r' && next < bytes.size() && bytes[next] == '\n') {
      ++next;
    }
    bytes.remove_prefix(next);
  }
  return std::nullopt;
}

bool ParseTimestampMapField(std::string_view field,
                            std::optional<double>& local,
                            std::optional<std::uint64_t>& mpegts) {
  const std::size_t separator = field.find(':');
  if (separator == std::string_view::npos) {
    return false;
  }
  const std::string_view name = Trim(field.substr(0, separator));
  const std::string_view value = Trim(field.substr(separator + 1));
  if (name == "LOCAL") {
    local = ParseVttTime(value);
    return local.has_value();
  }
  if (name == "MPEGTS") {
    mpegts = ParseUnsigned(value);
    return mpegts.has_value() && *mpegts < kMpegTsModulus;
  }
  return true;
}

}

std::optional<TextFormat> SniffTextFormat(std::string_view bytes) {
  if (bytes.starts_with("\xef\xbb\xbf")) {
    bytes.remove_prefix(3);
  }
  bytes = Trim(bytes);
  if (bytes.starts_with("WEBVTT")) {
    return TextFormat::kWebVtt;
  }
  if (bytes.starts_with("[Script Info]")) {
    return TextFormat::kAss;
  }
  if (bytes.starts_with("<") && (bytes.find("<tt") != std::string_view::npos ||
                                 bytes.find(":tt") != std::string_view::npos)) {
    return TextFormat::kTtml;
  }
  std::size_t position = 0;
  while (position < bytes.size()) {
    const std::size_t end = bytes.find_first_of("\r\n", position);
    const std::string_view line = bytes.substr(
        position, end == std::string_view::npos ? bytes.size() - position
                                                : end - position);
    const std::size_t arrow = line.find("-->");
    if (arrow != std::string_view::npos &&
        line.substr(0, arrow).find(',') != std::string_view::npos) {
      return TextFormat::kSubRip;
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
    if (bytes[end] == '\r' && position < bytes.size() &&
        bytes[position] == '\n') {
      ++position;
    }
  }
  return std::nullopt;
}

std::optional<WebVttTimestampMap> ParseWebVttTimestampMap(
    std::string_view bytes) {
  std::optional<std::string_view> fields = TimestampMapFields(bytes);
  if (!fields.has_value()) {
    return std::nullopt;
  }
  std::optional<double> local;
  std::optional<std::uint64_t> mpegts;
  const std::size_t line_end = fields->find_first_of("\r\n");
  *fields = fields->substr(0, line_end);
  while (!fields->empty()) {
    const std::size_t comma = fields->find(',');
    const std::string_view field = Trim(fields->substr(0, comma));
    if (!ParseTimestampMapField(field, local, mpegts)) {
      return std::nullopt;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    fields->remove_prefix(comma + 1);
  }
  if (!local.has_value() || !mpegts.has_value()) {
    return std::nullopt;
  }
  return WebVttTimestampMap{.local_seconds = *local, .mpegts = *mpegts};
}

bool MapWebVttCues(std::vector<TextCue>& cues,
                   std::optional<WebVttTimestampMap> map,
                   double segment_start_seconds) {
  if (!std::isfinite(segment_start_seconds)) {
    return false;
  }
  double offset = segment_start_seconds;
  if (map.has_value()) {
    if (!std::isfinite(map->local_seconds) || map->mpegts >= kMpegTsModulus) {
      return false;
    }
    const long double expected =
        static_cast<long double>(segment_start_seconds) * kMpegTsRate;
    const auto base = static_cast<long double>(map->mpegts);
    const long double turns = std::round(
        (expected - base) / static_cast<long double>(kMpegTsModulus));
    const long double unwrapped =
        base + (turns * static_cast<long double>(kMpegTsModulus));
    if (unwrapped <
            static_cast<long double>(std::numeric_limits<double>::lowest()) ||
        unwrapped >
            static_cast<long double>(std::numeric_limits<double>::max())) {
      return false;
    }
    offset = static_cast<double>(unwrapped / kMpegTsRate) - map->local_seconds;
  }
  for (TextCue& cue : cues) {
    cue.start_time += offset;
    cue.end_time += offset;
    if (!std::isfinite(cue.start_time) || !std::isfinite(cue.end_time) ||
        cue.end_time < cue.start_time) {
      return false;
    }
  }
  return true;
}

bool LoadTextTrackPayload(TextTrack& track, std::string_view bytes,
                          std::string fallback_encoding, bool auto_detect,
                          double segment_start_seconds) {
  const std::optional<TextFormat> format = SniffTextFormat(bytes);
  if (!format.has_value()) {
    return false;
  }
  if (*format == TextFormat::kSubRip) {
    return track.LoadSubRip(std::string(bytes), std::move(fallback_encoding),
                            auto_detect);
  }
  std::vector<TextCue> cues;
  if (*format == TextFormat::kWebVtt) {
    WebVttResult parsed = WebVttParser::Parse(bytes);
    const std::optional<WebVttTimestampMap> map =
        ParseWebVttTimestampMap(bytes);
    if (!MapWebVttCues(parsed.cues, map, segment_start_seconds)) {
      return false;
    }
    cues = std::move(parsed.cues);
  } else if (*format == TextFormat::kAss) {
    cues = AssParser::Parse(bytes).cues;
  } else {
    text::TtmlParserOptions options;
    options.sample_time_offset = segment_start_seconds;
    auto parsed = text::ParseTtml(bytes, options);
    if (!parsed.ok()) {
      return false;
    }
    cues = std::move(parsed->text_cues);
  }
  for (TextCue& cue : cues) {
    if (!track.AddCue(std::move(cue))) {
      return false;
    }
  }
  return true;
}

bool LoadTextTrackSample(TextTrack& track, const penumbra::StreamInfo& stream,
                         const penumbra::EncodedSample& sample,
                         double segment_start_seconds) {
  const auto start_micros = penumbra::Rescale(
      sample.pts.value, sample.pts.time_base, penumbra::kMicrosecondTimeBase,
      penumbra::Rounding::kNearest);
  const auto duration_micros = penumbra::Rescale(
      sample.duration.value, sample.duration.time_base,
      penumbra::kMicrosecondTimeBase, penumbra::Rounding::kNearest);
  if (!start_micros.has_value() || !duration_micros.has_value()) {
    return false;
  }
  const double start =
      static_cast<double>(*start_micros) / 1000000.0 + segment_start_seconds;
  const double end = start + static_cast<double>(*duration_micros) / 1000000.0;
  const std::string_view bytes(
      reinterpret_cast<const char*>(sample.data.data()), sample.data.size());
  if (stream.codec_string == "wvtt") {
    WebVttResult parsed = WebVttParser::ParseWvttSample(bytes, start, end);
    const bool has_cues = !parsed.cues.empty();
    for (TextCue& cue : parsed.cues) {
      if (!track.AddCue(std::move(cue))) {
        return false;
      }
    }
    return has_cues;
  }
  if (stream.codec_string == "tx3g") {
    if (bytes.size() < 2) {
      return false;
    }
    const size_t text_size =
        (static_cast<size_t>(static_cast<uint8_t>(bytes[0])) << 8) |
        static_cast<uint8_t>(bytes[1]);
    if (text_size > bytes.size() - 2) {
      return false;
    }
    TextCue cue;
    cue.start_time = start;
    cue.end_time = end;
    StyledTextRun run;
    run.text = bytes.substr(2, text_size);
    cue.runs.push_back(std::move(run));
    return track.AddCue(std::move(cue));
  }
  if (stream.codec_string == "srt") {
    TextCue cue;
    cue.start_time = start;
    cue.end_time = end;
    StyledTextRun run;
    run.text = std::string(bytes);
    cue.runs.push_back(std::move(run));
    return track.AddCue(std::move(cue));
  }
  if (stream.codec_string == "stpp") {
    return LoadTextTrackPayload(track, bytes, "", true, segment_start_seconds);
  }
  if (stream.codec_string == "ass" || stream.codec_string == "ssa") {
    const std::string_view codec_private(
        reinterpret_cast<const char*>(stream.codec_config.data()),
        stream.codec_config.size());
    AssResult parsed =
        AssParser::ParseMatroskaEvent(codec_private, bytes, start, end);
    for (TextCue& cue : parsed.cues) {
      if (!track.AddCue(std::move(cue))) {
        return false;
      }
    }
    return !parsed.cues.empty();
  }
  TextCue cue;
  cue.start_time = start;
  cue.end_time = end;
  StyledTextRun run;
  run.text = std::string(bytes);
  cue.runs.push_back(std::move(run));
  return track.AddCue(std::move(cue));
}

TextTrackMetadata MetadataForManifestTextTrack(
    const manifest::SwitchingSet& switching_set) {
  TextTrackMetadata metadata;
  metadata.label = switching_set.label;
  metadata.language = switching_set.language;
  if (std::ranges::find(switching_set.roles, manifest::Role::kCaption) !=
      switching_set.roles.end()) {
    metadata.kind = TextTrackKind::kCaptions;
  } else if (std::ranges::find(switching_set.roles,
                               manifest::Role::kDescription) !=
                 switching_set.roles.end() ||
             std::ranges::find(switching_set.roles, manifest::Role::kSign) !=
                 switching_set.roles.end()) {
    metadata.kind = TextTrackKind::kDescriptions;
  }
  return metadata;
}

TextTrackMetadata MetadataForHlsTextRendition(
    const manifest::HlsRendition& rendition) {
  TextTrackMetadata metadata;
  metadata.label = rendition.name;
  metadata.language = rendition.language.value_or("");
  if (rendition.type == manifest::RenditionType::kClosedCaptions) {
    metadata.kind = TextTrackKind::kCaptions;
  }
  return metadata;
}

}
