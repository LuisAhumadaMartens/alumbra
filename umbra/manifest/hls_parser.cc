#include "umbra/manifest/hls_parser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/manifest_error.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/manifest/validate.h"

namespace umbra::manifest {
namespace {

constexpr std::size_t kMaxPlaylistBytes = std::size_t{16} * 1024 * 1024;
constexpr std::size_t kMaxLineBytes = std::size_t{64} * 1024;
constexpr std::size_t kMaxAttributes = 4096;

using AttributeMap = std::map<std::string, std::string, std::less<>>;

enum class Tag {
  kUnknown,
  kDefine,
  kStreamInf,
  kMedia,
  kIframe,
  kSessionData,
  kSessionKey,
  kContentSteering,
  kStart,
  kIndependent,
  kExtinf,
  kTargetDuration,
  kMediaSequence,
  kDiscontinuitySequence,
  kDiscontinuity,
  kPlaylistType,
  kEndList,
  kByteRange,
  kKey,
  kMap,
  kProgramDateTime,
  kDateRange,
  kGap,
  kBitrate,
  kIframeOnly,
  kPart,
  kPartInf,
  kPreloadHint,
  kRenditionReport,
  kServerControl,
  kSkip,
  kVersion,
  kProgramId,
};

struct TagEntry {
  std::string_view prefix;
  Tag tag;
};

constexpr std::array<TagEntry, 33> kTags = {{
    {.prefix = "#EXT-X-DEFINE:", .tag = Tag::kDefine},
    {.prefix = "#EXT-X-STREAM-INF:", .tag = Tag::kStreamInf},
    {.prefix = "#EXT-X-MEDIA:", .tag = Tag::kMedia},
    {.prefix = "#EXT-X-I-FRAME-STREAM-INF:", .tag = Tag::kIframe},
    {.prefix = "#EXT-X-SESSION-DATA:", .tag = Tag::kSessionData},
    {.prefix = "#EXT-X-SESSION-KEY:", .tag = Tag::kSessionKey},
    {.prefix = "#EXT-X-CONTENT-STEERING:", .tag = Tag::kContentSteering},
    {.prefix = "#EXT-X-START:", .tag = Tag::kStart},
    {.prefix = "#EXT-X-INDEPENDENT-SEGMENTS", .tag = Tag::kIndependent},
    {.prefix = "#EXTINF:", .tag = Tag::kExtinf},
    {.prefix = "#EXT-X-TARGETDURATION:", .tag = Tag::kTargetDuration},
    {.prefix = "#EXT-X-MEDIA-SEQUENCE:", .tag = Tag::kMediaSequence},
    {.prefix = "#EXT-X-DISCONTINUITY-SEQUENCE:",
     .tag = Tag::kDiscontinuitySequence},
    {.prefix = "#EXT-X-DISCONTINUITY", .tag = Tag::kDiscontinuity},
    {.prefix = "#EXT-X-PLAYLIST-TYPE:", .tag = Tag::kPlaylistType},
    {.prefix = "#EXT-X-ENDLIST", .tag = Tag::kEndList},
    {.prefix = "#EXT-X-BYTERANGE:", .tag = Tag::kByteRange},
    {.prefix = "#EXT-X-KEY:", .tag = Tag::kKey},
    {.prefix = "#EXT-X-MAP:", .tag = Tag::kMap},
    {.prefix = "#EXT-X-PROGRAM-DATE-TIME:", .tag = Tag::kProgramDateTime},
    {.prefix = "#EXT-X-DATERANGE:", .tag = Tag::kDateRange},
    {.prefix = "#EXT-X-GAP", .tag = Tag::kGap},
    {.prefix = "#EXT-X-BITRATE:", .tag = Tag::kBitrate},
    {.prefix = "#EXT-X-I-FRAMES-ONLY", .tag = Tag::kIframeOnly},
    {.prefix = "#EXT-X-PART:", .tag = Tag::kPart},
    {.prefix = "#EXT-X-PART-INF:", .tag = Tag::kPartInf},
    {.prefix = "#EXT-X-PRELOAD-HINT:", .tag = Tag::kPreloadHint},
    {.prefix = "#EXT-X-RENDITION-REPORT:", .tag = Tag::kRenditionReport},
    {.prefix = "#EXT-X-SERVER-CONTROL:", .tag = Tag::kServerControl},
    {.prefix = "#EXT-X-SKIP:", .tag = Tag::kSkip},
    {.prefix = "#EXT-X-VERSION:", .tag = Tag::kVersion},
    {.prefix = "#EXT-X-PROGRAM-ID:", .tag = Tag::kProgramId},
    {.prefix = "#EXT-X-", .tag = Tag::kUnknown},
}};

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                            value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

absl::Status Failure(std::size_t line, std::string_view tag,
                     std::string_view reason) {
  return InvalidManifest(absl::StrCat("line ", line, " ", tag, ": ", reason));
}

template <typename T>
bool Integer(std::string_view value, T* output) {
  return absl::SimpleAtoi(value, output);
}

std::optional<double> Decimal(std::string_view value, bool signed_value) {
  if (value.empty() || value.size() > 64) {
    return std::nullopt;
  }
  if (!signed_value && (value.front() == '+' || value.front() == '-')) {
    return std::nullopt;
  }
  for (const char character : value) {
    if ((character < '0' || character > '9') && character != '.' &&
        character != '-' && character != '+') {
      return std::nullopt;
    }
  }
  double parsed = 0;
  if (!absl::SimpleAtod(value, &parsed) || !std::isfinite(parsed) ||
      (!signed_value && parsed < 0)) {
    return std::nullopt;
  }
  return parsed;
}

std::size_t AttributeEnd(std::string_view input, std::size_t begin) {
  bool quoted = false;
  for (std::size_t index = begin; index < input.size(); ++index) {
    if (input[index] == '"') {
      quoted = !quoted;
    }
    if (input[index] == ',' && !quoted) {
      return index;
    }
  }
  return input.size();
}

absl::StatusOr<HlsAttribute> ParseAttribute(std::string_view item) {
  item = Trim(item);
  if (item.empty()) {
    return InvalidManifest("empty HLS attribute");
  }
  const std::size_t equal = item.find('=');
  if (equal == std::string_view::npos) {
    return InvalidManifest("attribute has no value");
  }
  const std::string_view name = Trim(item.substr(0, equal));
  std::string_view value = Trim(item.substr(equal + 1));
  if (name.empty() || value.empty()) {
    return InvalidManifest("empty HLS attribute name or value");
  }
  bool quoted = false;
  if (value.front() == '"') {
    if (value.size() < 2 || value.back() != '"') {
      return InvalidManifest("unterminated quoted attribute");
    }
    quoted = true;
    value.remove_prefix(1);
    value.remove_suffix(1);
    if (value.find('"') != std::string_view::npos) {
      return InvalidManifest("quote inside quoted attribute");
    }
  } else if (value.find('"') != std::string_view::npos) {
    return InvalidManifest("quote in unquoted attribute");
  }
  return HlsAttribute{
      .name = std::string(name), .value = std::string(value), .quoted = quoted};
}

absl::StatusOr<AttributeMap> AttributeMapOf(std::string_view input,
                                            std::size_t line,
                                            std::string_view tag) {
  const auto parsed = ParseHlsAttributeList(input);
  if (!parsed.ok()) {
    return Failure(line, tag, parsed.status().message());
  }
  AttributeMap result;
  for (const HlsAttribute& attribute : *parsed) {
    result.emplace(attribute.name, attribute.value);
  }
  return result;
}

std::optional<std::string> Get(const AttributeMap& attributes,
                               std::string_view name) {
  const auto found = attributes.find(name);
  if (found == attributes.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool IsYes(const AttributeMap& attributes, std::string_view name) {
  return Get(attributes, name) == "YES";
}

std::string Resolve(std::string_view base, std::string_view reference) {
  if (reference.find("://") != std::string_view::npos || base.empty()) {
    return std::string(reference);
  }
  const std::size_t scheme = base.find("://");
  const std::size_t query = base.find_first_of("?#");
  const std::size_t path_end =
      query == std::string_view::npos ? base.size() : query;
  const std::size_t authority_end = scheme == std::string_view::npos
                                        ? std::string_view::npos
                                        : base.find('/', scheme + 3);
  if (reference.starts_with("//")) {
    if (scheme == std::string_view::npos) {
      return std::string(reference);
    }
    return absl::StrCat(base.substr(0, scheme), ":", reference);
  }
  if (reference.starts_with('/')) {
    const std::size_t origin_end =
        authority_end == std::string_view::npos ? path_end : authority_end;
    return absl::StrCat(base.substr(0, origin_end), reference);
  }
  const std::size_t slash = base.substr(0, path_end).rfind('/');
  if (slash == std::string_view::npos) {
    return std::string(reference);
  }
  return absl::StrCat(base.substr(0, slash + 1), reference);
}

std::size_t DeferredTrackCount(const Presentation& presentation) {
  std::size_t count = 0;
  for (const auto& period : presentation.periods) {
    if (period == nullptr) {
      continue;
    }
    for (const auto& set : period->switching_sets) {
      if (set == nullptr) {
        continue;
      }
      for (const auto& track : set->tracks) {
        if (track != nullptr && track->segments.Location().has_value()) {
          ++count;
        }
      }
    }
    for (const auto& track : period->trick_play) {
      if (track != nullptr && track->segments.Location().has_value()) {
        ++count;
      }
    }
  }
  return count;
}

absl::StatusOr<std::string> Expand(
    std::string_view input,
    const std::map<std::string, std::string, std::less<>>& variables) {
  std::string result;
  std::size_t cursor = 0;
  while (cursor < input.size()) {
    if (input[cursor] != '{') {
      result.push_back(input[cursor]);
      ++cursor;
      continue;
    }
    const std::size_t close = input.find('}', cursor + 1);
    if (close == std::string_view::npos) {
      return InvalidManifest("unterminated variable reference");
    }
    const auto found =
        variables.find(input.substr(cursor + 1, close - cursor - 1));
    if (found == variables.end()) {
      return InvalidManifest("undefined variable reference");
    }
    if (result.size() + found->second.size() > kMaxLineBytes) {
      return InvalidManifest("expanded URI exceeds size limit");
    }
    result.append(found->second);
    cursor = close + 1;
  }
  return result;
}

Tag Classify(std::string_view line) {
  for (const TagEntry& entry : kTags) {
    if (line.starts_with(entry.prefix)) {
      return entry.tag;
    }
  }
  return Tag::kUnknown;
}

struct PlaylistVariant {
  AttributeMap attributes;
  std::string uri;
  bool ignored = false;
};

struct PlaylistRendition {
  AttributeMap attributes;
};

class HlsParser {
 public:
  HlsParser(std::string_view input, const HlsParserOptions& options)
      : input_(input),
        options_(options),
        variables_(options.imported_variables) {
    result_.protocol = Protocol::kHls;
  }

  absl::StatusOr<Presentation> Run() {
    if (input_.size() > kMaxPlaylistBytes) {
      return InvalidManifest("playlist exceeds size limit");
    }
    absl::Status read_status = ReadLines();
    if (!read_status.ok()) {
      return read_status;
    }
    if (pending_variant_.has_value()) {
      return Failure(line_number_, "EXT-X-STREAM-INF", "missing URI");
    }
    if (!variants_.empty() || !iframe_variants_.empty()) {
      absl::Status master_status = BuildMaster();
      if (!master_status.ok()) {
        return master_status;
      }
    } else {
      absl::Status media_status = BuildMedia();
      if (!media_status.ok()) {
        return media_status;
      }
    }
    result_.ad_breaks = std::move(ad_breaks_);
    if (has_hls_metadata_) {
      result_.hls = std::move(hls_);
    }
    const auto built = BuildPresentation(result_);
    if (!built.ok()) {
      return built.status();
    }
    return result_;
  }

 private:
  absl::Status ReadLines() {
    std::size_t offset = 0;
    while (offset <= input_.size()) {
      const std::size_t end = input_.find('\n', offset);
      const std::size_t line_end =
          end == std::string_view::npos ? input_.size() : end;
      if (line_end - offset > kMaxLineBytes) {
        return Failure(line_number_ + 1, "line", "line exceeds size limit");
      }
      const std::string_view line =
          Trim(input_.substr(offset, line_end - offset));
      ++line_number_;
      if (line_number_ == 1) {
        if (line != "#EXTM3U") {
          return Failure(1, "EXTM3U", "missing playlist header");
        }
      } else if (!line.empty()) {
        absl::Status status = ProcessLine(line);
        if (!status.ok()) {
          return status;
        }
      }
      if (end == std::string_view::npos) {
        break;
      }
      offset = end + 1;
    }
    return absl::OkStatus();
  }

  absl::Status ProcessLine(std::string_view line) {
    if (pending_variant_.has_value() && line.front() != '#') {
      return ConsumeVariantUri(line);
    }
    if (line.front() == '#') {
      return ProcessTag(line);
    }
    if (pending_duration_.has_value()) {
      return ConsumeSegmentUri(line);
    }
    return absl::OkStatus();
  }

  absl::Status ProcessTag(std::string_view line) {
    switch (Classify(line)) {
      case Tag::kDefine:
        return HandleDefine(line);
      case Tag::kStreamInf:
        return HandleStreamInf(line);
      case Tag::kMedia:
        return HandleMedia(line);
      case Tag::kIframe:
        return HandleIframe(line);
      case Tag::kSessionData:
        return HandleSessionData(line);
      case Tag::kSessionKey:
        return HandleSessionKey(line);
      case Tag::kContentSteering:
        return HandleContentSteering(line);
      case Tag::kStart:
        return HandleStart(line);
      case Tag::kIndependent:
        hls_.independent_segments = true;
        has_hls_metadata_ = true;
        return absl::OkStatus();
      case Tag::kExtinf:
        return HandleExtinf(line);
      case Tag::kTargetDuration:
        return HandleTargetDuration(line);
      case Tag::kMediaSequence:
        return HandleMediaSequence(line);
      case Tag::kDiscontinuitySequence:
        return HandleDiscontinuitySequence(line);
      case Tag::kDiscontinuity:
        ++current_discontinuity_;
        return absl::OkStatus();
      case Tag::kPlaylistType:
        return HandlePlaylistType(line);
      case Tag::kEndList:
        end_list_ = true;
        return absl::OkStatus();
      case Tag::kByteRange:
        return HandleByteRange(line);
      case Tag::kKey:
        return HandleKey(line);
      case Tag::kMap:
        return HandleMap(line);
      case Tag::kProgramDateTime:
        return HandleProgramDateTime(line);
      case Tag::kDateRange:
        return HandleDateRange(line);
      case Tag::kGap:
        pending_gap_ = true;
        return absl::OkStatus();
      case Tag::kBitrate:
        return HandleBitrate(line);
      case Tag::kIframeOnly:
        iframe_only_ = true;
        has_hls_metadata_ = true;
        return absl::OkStatus();
      case Tag::kPart:
        return HandlePart(line);
      case Tag::kPartInf:
        return HandlePartInf(line);
      case Tag::kPreloadHint:
        return HandlePreloadHint(line);
      case Tag::kRenditionReport:
        return HandleRenditionReport(line);
      case Tag::kServerControl:
        return HandleServerControl(line);
      case Tag::kSkip:
        return HandleSkip(line);
      case Tag::kVersion:
        return HandleVersion(line);
      case Tag::kProgramId:
        return HandleProgramId(line);
      case Tag::kUnknown:
        return HandleUnknown(line);
    }
    return absl::OkStatus();
  }

  absl::StatusOr<AttributeMap> ParseAttributes(std::string_view line,
                                               std::string_view prefix) const {
    return AttributeMapOf(line.substr(prefix.size()), line_number_, prefix);
  }

  [[nodiscard]] static bool AttributeIsQuoted(std::string_view line,
                                              std::string_view prefix,
                                              std::string_view name) {
    const auto parsed = ParseHlsAttributeList(line.substr(prefix.size()));
    if (!parsed.ok()) {
      return false;
    }
    for (const HlsAttribute& attribute : *parsed) {
      if (attribute.name == name) {
        return attribute.quoted;
      }
    }
    return false;
  }

  absl::Status HandleDefine(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-DEFINE:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto name = Get(*parsed, "NAME");
    const auto value = Get(*parsed, "VALUE");
    const auto import_name = Get(*parsed, "IMPORT");
    const auto query_name = Get(*parsed, "QUERYPARAM");
    if (name && value && !import_name && !query_name) {
      variables_[*name] = *value;
      return absl::OkStatus();
    }
    if (import_name && !name && !value && !query_name) {
      return HandleImportedVariable(*import_name);
    }
    if (query_name && !name && !value && !import_name) {
      return HandleQueryVariable(*query_name);
    }
    return Failure(line_number_, "EXT-X-DEFINE", "invalid definition");
  }

  absl::Status HandleImportedVariable(std::string_view name) {
    const auto found = options_.imported_variables.find(name);
    if (found == options_.imported_variables.end()) {
      return Failure(line_number_, "EXT-X-DEFINE", "unknown import");
    }
    variables_[std::string(name)] = found->second;
    return absl::OkStatus();
  }

  absl::Status HandleQueryVariable(std::string_view name) {
    const auto found = options_.query_parameters.find(name);
    if (found == options_.query_parameters.end()) {
      return Failure(line_number_, "EXT-X-DEFINE",
                     "query parameter unavailable");
    }
    variables_[std::string(name)] = found->second;
    return absl::OkStatus();
  }

  absl::Status HandleStreamInf(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-STREAM-INF:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    if (Get(*parsed, "CODECS") &&
        !AttributeIsQuoted(line, "#EXT-X-STREAM-INF:", "CODECS")) {
      return Failure(line_number_, "EXT-X-STREAM-INF",
                     "CODECS must be a quoted string");
    }
    PlaylistVariant variant;
    variant.attributes = *parsed;
    for (const auto& [name, value] : variant.attributes) {
      static_cast<void>(value);
      if (name.starts_with("REQ-")) {
        variant.ignored = true;
      }
    }
    pending_variant_ = std::move(variant);
    return absl::OkStatus();
  }

  absl::Status HandleMedia(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-MEDIA:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    renditions_.push_back(PlaylistRendition{.attributes = *parsed});
    return absl::OkStatus();
  }

  absl::Status HandleIframe(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-I-FRAME-STREAM-INF:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto uri = Get(*parsed, "URI");
    if (!uri) {
      return Failure(line_number_, "EXT-X-I-FRAME-STREAM-INF", "missing URI");
    }
    auto expanded = Expand(*uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "EXT-X-I-FRAME-STREAM-INF",
                     expanded.status().message());
    }
    iframe_variants_.push_back(
        PlaylistVariant{.attributes = *parsed,
                        .uri = Resolve(options_.base_uri, *expanded),
                        .ignored = HasRequiredUnknown(*parsed)});
    return absl::OkStatus();
  }

  [[nodiscard]] static bool HasRequiredUnknown(const AttributeMap& attributes) {
    return std::ranges::any_of(attributes, [](const auto& attribute) {
      return attribute.first.starts_with("REQ-");
    });
  }

  absl::Status HandleSessionData(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-SESSION-DATA:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto data_id = Get(*parsed, "DATA-ID");
    const auto value = Get(*parsed, "VALUE");
    const auto uri = Get(*parsed, "URI");
    if (!data_id || (value.has_value() == uri.has_value())) {
      return Failure(line_number_, "EXT-X-SESSION-DATA",
                     "requires DATA-ID and exactly one of VALUE or URI");
    }
    HlsSessionData data;
    data.data_id = *data_id;
    data.language = Get(*parsed, "LANGUAGE");
    data.value = value;
    if (uri) {
      auto expanded = Expand(*uri, variables_);
      if (!expanded.ok()) {
        return Failure(line_number_, "EXT-X-SESSION-DATA",
                       expanded.status().message());
      }
      data.uri = Resolve(options_.base_uri, *expanded);
    }
    hls_.session_data.push_back(std::move(data));
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleSessionKey(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-SESSION-KEY:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    auto key = ParseKey(*parsed, "EXT-X-SESSION-KEY");
    if (!key.ok()) {
      return key.status();
    }
    hls_.session_keys.push_back(std::move(*key));
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::StatusOr<KeyReference> ParseKey(const AttributeMap& attributes,
                                        std::string_view tag) const {
    const auto method = Get(attributes, "METHOD");
    const auto uri = Get(attributes, "URI");
    if (!method || *method == "NONE") {
      return Failure(line_number_, tag, "invalid METHOD");
    }
    if (!uri) {
      return Failure(line_number_, tag, "missing URI");
    }
    KeyReference key;
    if (*method == "AES-128") {
      key.method = KeyMethod::kAes128;
    } else if (*method == "SAMPLE-AES") {
      key.method = KeyMethod::kSampleAes;
    } else if (*method == "SAMPLE-AES-CTR") {
      key.method = KeyMethod::kSampleAesCtr;
    } else {
      return Failure(line_number_, tag, "unsupported METHOD");
    }
    auto expanded = Expand(*uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, tag, expanded.status().message());
    }
    key.uri = Resolve(options_.base_uri, *expanded);
    key.key_format = Get(attributes, "KEYFORMAT").value_or("identity");
    const auto iv = Get(attributes, "IV");
    if (iv) {
      absl::Status status = ParseIv(*iv, key.iv, tag);
      if (!status.ok()) {
        return status;
      }
    }
    return key;
  }

  absl::Status ParseIv(std::string_view value, std::vector<std::uint8_t>& iv,
                       std::string_view tag) const {
    if (value.size() != 34 || !value.starts_with("0x")) {
      return Failure(line_number_, tag, "invalid IV");
    }
    for (std::size_t index = 2; index < value.size(); index += 2) {
      const int high = HexDigit(value[index]);
      const int low = HexDigit(value[index + 1]);
      if (high < 0 || low < 0) {
        return Failure(line_number_, tag, "invalid IV");
      }
      iv.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return absl::OkStatus();
  }

  static int HexDigit(char character) {
    if (character >= '0' && character <= '9') {
      return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
      return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
      return character - 'A' + 10;
    }
    return -1;
  }

  absl::Status HandleContentSteering(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-CONTENT-STEERING:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto uri = Get(*parsed, "SERVER-URI");
    if (!uri) {
      return Failure(line_number_, "EXT-X-CONTENT-STEERING",
                     "missing SERVER-URI");
    }
    auto expanded = Expand(*uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "EXT-X-CONTENT-STEERING",
                     expanded.status().message());
    }
    hls_.content_steering =
        HlsContentSteering{.server_uri = Resolve(options_.base_uri, *expanded),
                           .pathway_id = Get(*parsed, "PATHWAY-ID")};
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleStart(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-START:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto offset = Get(*parsed, "TIME-OFFSET");
    if (!offset) {
      return Failure(line_number_, "EXT-X-START", "missing TIME-OFFSET");
    }
    const auto value = Decimal(*offset, true);
    if (!value) {
      return Failure(line_number_, "EXT-X-START", "invalid TIME-OFFSET");
    }
    hls_.start_offset = penumbra::TimeDelta::FromMicroseconds(
        static_cast<std::int64_t>(*value * 1'000'000));
    hls_.start_time_precise = IsYes(*parsed, "PRECISE");
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleExtinf(std::string_view line) {
    const std::string_view payload = line.substr(8);
    const std::size_t comma = payload.find(',');
    pending_duration_ = Decimal(payload.substr(0, comma), false);
    if (!pending_duration_) {
      return Failure(line_number_, "EXTINF", "invalid duration");
    }
    constexpr long double kMaxMicroseconds =
        static_cast<long double>(std::numeric_limits<std::int64_t>::max()) -
        1'000'000.0L;
    const long double duration_us =
        static_cast<long double>(*pending_duration_) * 1'000'000.0L;
    const long double cursor_us =
        static_cast<long double>(cursor_seconds_) * 1'000'000.0L;
    if (duration_us > kMaxMicroseconds ||
        cursor_us > kMaxMicroseconds - duration_us) {
      return Failure(line_number_, "EXTINF", "duration exceeds limit");
    }
    if (options_.strict && target_duration_ > 0 &&
        std::floor(*pending_duration_ + 0.5) >
            static_cast<double>(target_duration_)) {
      return Failure(line_number_, "EXTINF", "segment exceeds TARGETDURATION");
    }
    return absl::OkStatus();
  }

  absl::Status HandleTargetDuration(std::string_view line) {
    if (!Integer(line.substr(22), &target_duration_) || target_duration_ <= 0) {
      return Failure(line_number_, "EXT-X-TARGETDURATION", "invalid value");
    }
    return absl::OkStatus();
  }

  absl::Status HandleBitrate(std::string_view line) {
    std::int64_t bitrate = 0;
    if (!Integer(line.substr(15), &bitrate) || bitrate <= 0) {
      return Failure(line_number_, "EXT-X-BITRATE", "invalid value");
    }
    pending_bitrate_kbps_ = bitrate;
    return absl::OkStatus();
  }

  absl::Status HandleMediaSequence(std::string_view line) {
    if (!Integer(line.substr(22), &media_sequence_) || media_sequence_ < 0) {
      return Failure(line_number_, "EXT-X-MEDIA-SEQUENCE", "invalid value");
    }
    return absl::OkStatus();
  }

  absl::Status HandleDiscontinuitySequence(std::string_view line) {
    if (!Integer(line.substr(30), &discontinuity_sequence_) ||
        discontinuity_sequence_ < 0) {
      return Failure(line_number_, "EXT-X-DISCONTINUITY-SEQUENCE",
                     "invalid value");
    }
    current_discontinuity_ = discontinuity_sequence_;
    return absl::OkStatus();
  }

  absl::Status HandlePlaylistType(std::string_view line) {
    const std::string_view type = line.substr(21);
    if (type != "VOD" && type != "EVENT") {
      return Failure(line_number_, "EXT-X-PLAYLIST-TYPE", "invalid type");
    }
    result_.type =
        type == "VOD" ? PresentationType::kStatic : PresentationType::kDynamic;
    vod_ = type == "VOD";
    return absl::OkStatus();
  }

  absl::Status HandleByteRange(std::string_view line) {
    const std::string_view value = line.substr(17);
    const std::size_t at = value.find('@');
    std::int64_t length = 0;
    std::int64_t offset = 0;
    if (!Integer(value.substr(0, at), &length) || length <= 0) {
      return Failure(line_number_, "EXT-X-BYTERANGE", "invalid length");
    }
    if (at == std::string_view::npos) {
      if (!range_cursor_) {
        return Failure(line_number_, "EXT-X-BYTERANGE",
                       "implicit offset without previous range");
      }
      offset = *range_cursor_;
    } else if (!Integer(value.substr(at + 1), &offset) || offset < 0) {
      return Failure(line_number_, "EXT-X-BYTERANGE", "invalid offset");
    }
    if (offset > std::numeric_limits<std::int64_t>::max() - length) {
      return Failure(line_number_, "EXT-X-BYTERANGE", "range overflow");
    }
    pending_range_ = ByteRange{.offset = offset, .length = length};
    range_cursor_ = offset + length;
    return absl::OkStatus();
  }

  absl::Status HandleKey(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-KEY:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    if (Get(*parsed, "METHOD") == "NONE") {
      active_key_index_.reset();
      return absl::OkStatus();
    }
    auto key = ParseKey(*parsed, "EXT-X-KEY");
    if (!key.ok()) {
      return key.status();
    }
    keys_.push_back(std::move(*key));
    active_key_index_ = static_cast<std::uint32_t>(keys_.size() - 1);
    return absl::OkStatus();
  }

  absl::Status HandleMap(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-MAP:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto uri = Get(*parsed, "URI");
    if (!uri) {
      return Failure(line_number_, "EXT-X-MAP", "missing URI");
    }
    auto expanded = Expand(*uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "EXT-X-MAP", expanded.status().message());
    }
    InitSection section;
    section.uri = Resolve(options_.base_uri, *expanded);
    const auto range = Get(*parsed, "BYTERANGE");
    if (range) {
      const std::size_t at = range->find('@');
      std::int64_t length = 0;
      std::int64_t offset = 0;
      if (!Integer(std::string_view(*range).substr(0, at), &length) ||
          length <= 0) {
        return Failure(line_number_, "EXT-X-MAP", "invalid BYTERANGE");
      }
      if (at != std::string::npos &&
          (!Integer(std::string_view(*range).substr(at + 1), &offset) ||
           offset < 0)) {
        return Failure(line_number_, "EXT-X-MAP", "invalid BYTERANGE offset");
      }
      section.byte_range = ByteRange{.offset = offset, .length = length};
    }
    init_sections_.push_back(std::move(section));
    active_init_index_ = static_cast<std::uint32_t>(init_sections_.size() - 1);
    return absl::OkStatus();
  }

  absl::Status HandleProgramDateTime(std::string_view line) {
    absl::Time parsed;
    const std::string_view format(static_cast<const char*>(absl::RFC3339_full));
    if (!absl::ParseTime(format, line.substr(25), &parsed, nullptr)) {
      return Failure(line_number_, "EXT-X-PROGRAM-DATE-TIME",
                     "invalid timestamp");
    }
    pending_program_date_time_ = parsed;
    return absl::OkStatus();
  }

  absl::Status HandleDateRange(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-DATERANGE:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto id = Get(*parsed, "ID");
    if (!id) {
      return Failure(line_number_, "EXT-X-DATERANGE", "missing ID");
    }
    const auto klass = Get(*parsed, "CLASS");
    if (!klass) {
      if (options_.strict) {
        return Failure(line_number_, "EXT-X-DATERANGE", "missing CLASS");
      }
      return absl::OkStatus();
    }
    if (!klass->starts_with("com.apple.hls.interstitial")) {
      return absl::OkStatus();
    }
    AdBreak ad;
    ad.id = *id;
    ad.start = penumbra::MediaTime::FromMicroseconds(
        static_cast<std::int64_t>(cursor_seconds_ * 1'000'000));
    const auto duration = Get(*parsed, "DURATION");
    if (duration) {
      const auto seconds = Decimal(*duration, false);
      if (!seconds) {
        return Failure(line_number_, "EXT-X-DATERANGE", "invalid DURATION");
      }
      ad.duration = penumbra::TimeDelta::FromMicroseconds(
          static_cast<std::int64_t>(*seconds * 1'000'000));
    }
    ad_breaks_.push_back(std::move(ad));
    return absl::OkStatus();
  }

  absl::Status HandlePart(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-PART:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto uri = Get(*parsed, "URI");
    const auto duration = Get(*parsed, "DURATION");
    if (!uri || !duration) {
      return Failure(line_number_, "EXT-X-PART", "missing URI or DURATION");
    }
    auto expanded = Expand(*uri, variables_);
    const auto seconds = Decimal(*duration, false);
    if (!expanded.ok() || !seconds || *seconds <= 0) {
      return Failure(line_number_, "EXT-X-PART", "invalid URI or DURATION");
    }
    HlsPart part;
    part.uri = Resolve(options_.base_uri, *expanded);
    part.duration = penumbra::TimeDelta::FromMicroseconds(
        static_cast<std::int64_t>(*seconds * 1'000'000));
    part.independent = IsYes(*parsed, "INDEPENDENT");
    part.gap = IsYes(*parsed, "GAP");
    const auto range = Get(*parsed, "BYTERANGE");
    if (range) {
      const std::size_t at = range->find('@');
      std::int64_t length = 0;
      std::int64_t offset = 0;
      if (!Integer(std::string_view(*range).substr(0, at), &length) ||
          length <= 0 || at == std::string::npos ||
          !Integer(std::string_view(*range).substr(at + 1), &offset) ||
          offset < 0) {
        return Failure(line_number_, "EXT-X-PART", "invalid BYTERANGE");
      }
      part.byte_range = ByteRange{.offset = offset, .length = length};
    }
    pending_parts_.push_back(std::move(part));
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandlePartInf(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-PART-INF:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto target = Get(*parsed, "PART-TARGET");
    if (!target) {
      return Failure(line_number_, "EXT-X-PART-INF", "missing PART-TARGET");
    }
    const auto seconds = Decimal(*target, false);
    if (!seconds || *seconds <= 0) {
      return Failure(line_number_, "EXT-X-PART-INF", "invalid PART-TARGET");
    }
    hls_.part_target_duration = penumbra::TimeDelta::FromMicroseconds(
        static_cast<std::int64_t>(*seconds * 1'000'000));
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandlePreloadHint(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-PRELOAD-HINT:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto type = Get(*parsed, "TYPE");
    const auto uri = Get(*parsed, "URI");
    if (!type || !uri) {
      return Failure(line_number_, "EXT-X-PRELOAD-HINT", "missing TYPE or URI");
    }
    auto expanded = Expand(*uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "EXT-X-PRELOAD-HINT",
                     expanded.status().message());
    }
    HlsPreloadHint hint;
    hint.type = *type;
    hint.uri = Resolve(options_.base_uri, *expanded);
    absl::Status range_status =
        ReadOptionalInteger(*parsed, "BYTERANGE-START", hint.byte_range_start,
                            "EXT-X-PRELOAD-HINT");
    if (!range_status.ok()) {
      return range_status;
    }
    absl::Status length_status =
        ReadOptionalInteger(*parsed, "BYTERANGE-LENGTH", hint.byte_range_length,
                            "EXT-X-PRELOAD-HINT");
    if (!length_status.ok()) {
      return length_status;
    }
    hls_.preload_hint = std::move(hint);
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleRenditionReport(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-RENDITION-REPORT:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto uri = Get(*parsed, "URI");
    if (!uri) {
      return Failure(line_number_, "EXT-X-RENDITION-REPORT", "missing URI");
    }
    auto expanded = Expand(*uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "EXT-X-RENDITION-REPORT",
                     expanded.status().message());
    }
    HlsRenditionReport report;
    report.uri = Resolve(options_.base_uri, *expanded);
    absl::Status msn_status = ReadOptionalInteger(
        *parsed, "LAST-MSN", report.last_msn, "EXT-X-RENDITION-REPORT");
    if (!msn_status.ok()) {
      return msn_status;
    }
    absl::Status part_status = ReadOptionalInteger(
        *parsed, "LAST-PART", report.last_part, "EXT-X-RENDITION-REPORT");
    if (!part_status.ok()) {
      return part_status;
    }
    hls_.rendition_reports.push_back(std::move(report));
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleServerControl(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-SERVER-CONTROL:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    hls_.can_block_reload = IsYes(*parsed, "CAN-BLOCK-RELOAD");
    hls_.can_skip_date_ranges = IsYes(*parsed, "CAN-SKIP-DATERANGES");
    absl::Status skip_status = ReadOptionalDuration(
        *parsed, "CAN-SKIP-UNTIL", hls_.can_skip_until, "EXT-X-SERVER-CONTROL");
    if (!skip_status.ok()) {
      return skip_status;
    }
    absl::Status hold_status = ReadOptionalDuration(
        *parsed, "HOLD-BACK", live_.hold_back, "EXT-X-SERVER-CONTROL");
    if (!hold_status.ok()) {
      return hold_status;
    }
    absl::Status part_status =
        ReadOptionalDuration(*parsed, "PART-HOLD-BACK", live_.part_hold_back,
                             "EXT-X-SERVER-CONTROL");
    if (!part_status.ok()) {
      return part_status;
    }
    has_server_control_ = true;
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleSkip(std::string_view line) {
    const auto parsed = ParseAttributes(line, "#EXT-X-SKIP:");
    if (!parsed.ok()) {
      return parsed.status();
    }
    const auto skipped = Get(*parsed, "SKIPPED-SEGMENTS");
    if (!skipped || !Integer(*skipped, &hls_.skipped_segments) ||
        hls_.skipped_segments < 0) {
      return Failure(line_number_, "EXT-X-SKIP", "invalid SKIPPED-SEGMENTS");
    }
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleProgramId(std::string_view line) {
    std::int32_t program_id = 0;
    if (!Integer(line.substr(16), &program_id)) {
      return Failure(line_number_, "EXT-X-PROGRAM-ID", "invalid value");
    }
    hls_.program_id = program_id;
    has_hls_metadata_ = true;
    return absl::OkStatus();
  }

  absl::Status HandleVersion(std::string_view line) const {
    std::int64_t version = 0;
    if (!Integer(line.substr(15), &version) || version < 1) {
      return Failure(line_number_, "EXT-X-VERSION", "invalid value");
    }
    return absl::OkStatus();
  }

  absl::Status HandleUnknown(std::string_view line) const {
    if (options_.strict && line.starts_with("#EXT-X-")) {
      return Failure(line_number_, "EXT-X", "unknown tag in strict mode");
    }
    return absl::OkStatus();
  }

  template <typename T>
  absl::Status ReadOptionalInteger(const AttributeMap& attributes,
                                   std::string_view name,
                                   std::optional<T>& output,
                                   std::string_view tag) const {
    const auto value = Get(attributes, name);
    if (!value) {
      return absl::OkStatus();
    }
    T parsed = 0;
    if (!Integer(*value, &parsed) || parsed < 0) {
      return Failure(line_number_, tag, absl::StrCat("invalid ", name));
    }
    output = parsed;
    return absl::OkStatus();
  }

  absl::Status ReadOptionalDuration(const AttributeMap& attributes,
                                    std::string_view name,
                                    std::optional<penumbra::TimeDelta>& output,
                                    std::string_view tag) const {
    const auto value = Get(attributes, name);
    if (!value) {
      return absl::OkStatus();
    }
    const auto seconds = Decimal(*value, false);
    if (!seconds || *seconds <= 0) {
      return Failure(line_number_, tag, absl::StrCat("invalid ", name));
    }
    output = penumbra::TimeDelta::FromMicroseconds(
        static_cast<std::int64_t>(*seconds * 1'000'000));
    return absl::OkStatus();
  }

  absl::Status HandleSegmentUri(std::string_view uri) {
    if (!pending_duration_) {
      return Failure(line_number_, "EXTINF", "segment duration is missing");
    }
    if (segments_.size() >= static_cast<std::size_t>(kMaxSegmentsPerIndex)) {
      return Failure(line_number_, "segment", "segment count exceeds limit");
    }
    auto expanded = Expand(uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "URI", expanded.status().message());
    }
    Segment segment;
    segment.sequence_number = media_sequence_ + hls_.skipped_segments +
                              static_cast<std::int64_t>(segments_.size());
    segment.start = penumbra::MediaTime::FromMicroseconds(
        static_cast<std::int64_t>(cursor_seconds_ * 1'000'000));
    segment.duration = penumbra::TimeDelta::FromMicroseconds(
        static_cast<std::int64_t>(*pending_duration_ * 1'000'000));
    segment.uri = Resolve(options_.base_uri, *expanded);
    segment.byte_range = pending_range_;
    segment.discontinuity_sequence = current_discontinuity_;
    segment.gap = pending_gap_;
    segment.bitrate_kbps = pending_bitrate_kbps_;
    segment.program_date_time = pending_program_date_time_;
    segment.init_index = active_init_index_;
    segment.key_index = active_key_index_;
    segment.hls_parts = std::move(pending_parts_);
    const std::int64_t duration_us = segment.duration.InMicroseconds();
    segments_.push_back(std::move(segment));
    cursor_seconds_ += *pending_duration_;
    pending_duration_.reset();
    pending_range_.reset();
    pending_gap_ = false;
    pending_bitrate_kbps_.reset();
    if (pending_program_date_time_) {
      *pending_program_date_time_ += absl::Microseconds(duration_us);
    }
    return absl::OkStatus();
  }

  absl::Status ConsumeSegmentUri(std::string_view uri) {
    return HandleSegmentUri(uri);
  }

  absl::Status ConsumeVariantUri(std::string_view uri) {
    if (!pending_variant_) {
      return Failure(line_number_, "EXT-X-STREAM-INF",
                     "variant attributes are missing");
    }
    auto expanded = Expand(uri, variables_);
    if (!expanded.ok()) {
      return Failure(line_number_, "URI", expanded.status().message());
    }
    pending_variant_->uri = Resolve(options_.base_uri, *expanded);
    variants_.push_back(std::move(*pending_variant_));
    pending_variant_.reset();
    return absl::OkStatus();
  }

  absl::Status BuildMedia() {
    if (pending_duration_) {
      return Failure(line_number_, "EXTINF", "segment URI missing");
    }
    hls_.trailing_parts = std::move(pending_parts_);
    ExplicitSegmentIndexParameters parameters;
    parameters.segments = std::move(segments_);
    parameters.first_sequence_number = media_sequence_ + hls_.skipped_segments;
    parameters.init_sections = std::move(init_sections_);
    parameters.key_references = std::move(keys_);
    const auto index = ExplicitSegmentIndex::Create(std::move(parameters));
    if (!index.ok()) {
      return Failure(line_number_, "playlist", index.status().message());
    }
    absl::Status periods_status = BuildPeriods(**index);
    if (!periods_status.ok()) {
      return periods_status;
    }
    ConfigureMediaPresentation(**index);
    return absl::OkStatus();
  }

  absl::Status BuildPeriods(const SegmentIndex& index) {
    Period period;
    period.id = HlsPeriodId(discontinuity_sequence_);
    std::size_t begin = 0;
    while (begin < index.size()) {
      const std::int64_t sequence = index.At(begin).discontinuity_sequence;
      std::size_t finish = begin + 1;
      while (finish < index.size() &&
             index.At(finish).discontinuity_sequence == sequence) {
        ++finish;
      }
      if (begin > 0) {
        result_.periods.push_back(Freeze(std::move(period)));
        period = Period{};
        period.id = HlsPeriodId(sequence);
      }
      absl::Status track_status =
          AddPeriodTrack(period, index, begin, finish, sequence);
      if (!track_status.ok()) {
        return track_status;
      }
      begin = finish;
    }
    if (!period.switching_sets.empty()) {
      result_.periods.push_back(Freeze(std::move(period)));
    }
    return absl::OkStatus();
  }

  absl::Status AddPeriodTrack(Period& period, const SegmentIndex& index,
                              std::size_t begin, std::size_t finish,
                              std::int64_t sequence) const {
    period.start = index.At(begin).start;
    ExplicitSegmentIndexParameters parameters;
    parameters.first_sequence_number = index.At(begin).sequence_number;
    for (std::size_t item = begin; item < finish; ++item) {
      parameters.segments.push_back(index.At(item));
    }
    parameters.init_sections = index.InitSections();
    parameters.key_references = index.KeyReferences();
    const auto sub_index = ExplicitSegmentIndex::Create(std::move(parameters));
    if (!sub_index.ok()) {
      return Failure(line_number_, "playlist", sub_index.status().message());
    }
    Track track;
    track.id = TrackId(absl::StrCat("hls/media/", sequence));
    track.bandwidth_bps.reset();
    track.format = VideoTrackFormat{};
    HlsTrackExtension extension;
    extension.i_frames_only = iframe_only_;
    track.extension = extension;
    track.segments = SegmentIndexProvider::Resolved(*sub_index);
    SwitchingSet set;
    set.id = HlsVideoSwitchingSetId();
    set.tracks.push_back(Freeze(std::move(track)));
    period.switching_sets.push_back(Freeze(std::move(set)));
    period.duration = index.At(finish - 1).End() - period.start;
    return absl::OkStatus();
  }

  void ConfigureMediaPresentation(const SegmentIndex& index) {
    const bool is_static = end_list_ || vod_;
    result_.type =
        is_static ? PresentationType::kStatic : PresentationType::kDynamic;
    if (is_static) {
      result_.duration = penumbra::TimeDelta::FromMicroseconds(
          static_cast<std::int64_t>(cursor_seconds_ * 1'000'000));
      return;
    }
    live_.window_start =
        index.empty() ? penumbra::MediaTime::Zero() : index.Start();
    live_.window_end =
        index.empty() ? penumbra::MediaTime::Zero() : index.End();
    if (target_duration_ > 0) {
      live_.target_segment_duration =
          penumbra::TimeDelta::FromSeconds(target_duration_);
    }
    result_.live = live_;
  }

  absl::Status BuildMaster() {
    result_.type = PresentationType::kDynamic;
    result_.live = LiveInfo{};
    Period period;
    period.id = HlsPeriodId(0);
    BuildRenditionSets(period);
    absl::Status variant_status = BuildVariantTracks(period);
    if (!variant_status.ok()) {
      return variant_status;
    }
    BuildIframeTracks(period);
    result_.periods.push_back(Freeze(std::move(period)));
    return absl::OkStatus();
  }

  void BuildRenditionSets(Period& period) {
    std::map<std::string, std::vector<SwitchingSetId>, std::less<>> audio;
    for (const PlaylistRendition& rendition : renditions_) {
      const auto type = Get(rendition.attributes, "TYPE");
      const auto group = Get(rendition.attributes, "GROUP-ID");
      const auto name = Get(rendition.attributes, "NAME");
      if (!type || !group || !name) {
        continue;
      }
      const auto rendition_type = ParseRenditionType(*type);
      if (!rendition_type) {
        continue;
      }
      AddRenditionSet(period, rendition, *rendition_type, *group, *name, audio);
    }
    audio_groups_ = std::move(audio);
  }

  [[nodiscard]] static std::optional<RenditionType> ParseRenditionType(
      std::string_view value) {
    if (value == "AUDIO") {
      return RenditionType::kAudio;
    }
    if (value == "SUBTITLES") {
      return RenditionType::kSubtitles;
    }
    if (value == "CLOSED-CAPTIONS") {
      return RenditionType::kClosedCaptions;
    }
    return std::nullopt;
  }

  void AddRenditionSet(
      Period& period, const PlaylistRendition& rendition, RenditionType kind,
      std::string_view group, std::string_view name,
      std::map<std::string, std::vector<SwitchingSetId>, std::less<>>& audio) {
    HlsRenditionKey const key{
        .type = kind,
        .group_id = std::string(group),
        .name = std::string(name),
        .language = Get(rendition.attributes, "LANGUAGE").value_or(""),
        .characteristics =
            Get(rendition.attributes, "CHARACTERISTICS").value_or("")};
    SwitchingSet set;
    set.id = HlsRenditionSwitchingSetId(key);
    set.type = kind == RenditionType::kAudio ? penumbra::StreamType::kAudio
                                             : penumbra::StreamType::kText;
    set.language = key.language;
    set.label = key.name;
    set.is_default = IsYes(rendition.attributes, "DEFAULT");
    set.autoselect = IsYes(rendition.attributes, "AUTOSELECT");
    set.forced = IsYes(rendition.attributes, "FORCED");
    Track track;
    track.id = HlsRenditionTrackId(key);
    track.bandwidth_bps.reset();
    track.extension = HlsTrackExtension{};
    track.format =
        set.type == penumbra::StreamType::kAudio
            ? std::variant<VideoTrackFormat, AudioTrackFormat, TextTrackFormat>(
                  AudioTrackFormat{})
            : std::variant<VideoTrackFormat, AudioTrackFormat, TextTrackFormat>(
                  TextTrackFormat{});
    const auto uri = Get(rendition.attributes, "URI");
    if (uri) {
      const auto expanded = Expand(*uri, variables_);
      if (expanded.ok()) {
        IndexLocation location;
        location.source = IndexLocation::Source::kHlsMediaPlaylist;
        location.uri = Resolve(options_.base_uri, *expanded);
        track.segments = SegmentIndexProvider::Deferred(std::move(location));
      }
    }
    set.tracks.push_back(Freeze(std::move(track)));
    if (kind == RenditionType::kAudio) {
      audio[std::string(group)].push_back(set.id);
    }
    period.switching_sets.push_back(Freeze(std::move(set)));
  }

  absl::Status BuildVariantTracks(Period& period) {
    SwitchingSet video;
    video.id = HlsVideoSwitchingSetId();
    video.type = penumbra::StreamType::kVideo;
    std::map<std::string, std::uint32_t, std::less<>> ordinals;
    for (const PlaylistVariant& variant : variants_) {
      if (variant.ignored) {
        continue;
      }
      auto track = MakeVariantTrack(variant, ordinals);
      if (!track.ok()) {
        return track.status();
      }
      video.tracks.push_back(Freeze(std::move(*track)));
    }
    if (!video.tracks.empty()) {
      period.switching_sets.push_back(Freeze(std::move(video)));
    }
    return absl::OkStatus();
  }

  absl::StatusOr<Track> MakeVariantTrack(
      const PlaylistVariant& variant,
      std::map<std::string, std::uint32_t, std::less<>>& ordinals) const {
    std::int64_t bandwidth = 0;
    const auto bandwidth_text = Get(variant.attributes, "BANDWIDTH");
    if (!bandwidth_text || !Integer(*bandwidth_text, &bandwidth) ||
        bandwidth <= 0) {
      return Failure(line_number_, "EXT-X-STREAM-INF", "invalid BANDWIDTH");
    }
    const std::string codecs_text =
        Get(variant.attributes, "CODECS").value_or("");
    auto codecs = codec::ParseCodecList(codecs_text);
    if (!codecs.ok()) {
      return Failure(line_number_, "EXT-X-STREAM-INF", "invalid CODECS");
    }
    HlsVariantKey key;
    key.bandwidth = bandwidth;
    key.codecs = codecs_text;
    absl::Status resolution_status = SetResolution(variant.attributes, key);
    if (!resolution_status.ok()) {
      return resolution_status;
    }
    key.frame_rate = Get(variant.attributes, "FRAME-RATE").value_or("");
    key.video_range = Get(variant.attributes, "VIDEO-RANGE").value_or("");
    const auto frame_rate = Get(variant.attributes, "FRAME-RATE");
    if (frame_rate) {
      const auto parsed_rate = Decimal(*frame_rate, false);
      if (!parsed_rate || *parsed_rate <= 0) {
        return Failure(line_number_, "EXT-X-STREAM-INF", "invalid FRAME-RATE");
      }
    }
    if (!key.video_range.empty() && key.video_range != "SDR" &&
        key.video_range != "PQ" && key.video_range != "HLG") {
      return Failure(line_number_, "EXT-X-STREAM-INF", "invalid VIDEO-RANGE");
    }
    const std::string ordinal_key =
        absl::StrCat(bandwidth, "/", key.width, "/", key.height, "/",
                     key.frame_rate, "/", key.video_range, "/", key.codecs);
    key.ordinal = ordinals[ordinal_key]++;
    Track track;
    track.id = HlsVariantTrackId(key);
    track.bandwidth_bps = bandwidth;
    const auto average_bandwidth = Get(variant.attributes, "AVERAGE-BANDWIDTH");
    if (average_bandwidth) {
      std::int64_t parsed_average = 0;
      if (!Integer(*average_bandwidth, &parsed_average) ||
          parsed_average <= 0) {
        return Failure(line_number_, "EXT-X-STREAM-INF",
                       "invalid AVERAGE-BANDWIDTH");
      }
      track.average_bandwidth_bps = parsed_average;
    }
    track.codecs = std::move(*codecs);
    track.format = MakeVideoFormat(variant.attributes, key);
    auto extension = MakeVariantExtension(variant.attributes);
    if (!extension.ok()) {
      return extension.status();
    }
    track.extension = std::move(*extension);
    SetAllowedAudio(track);
    IndexLocation location;
    location.source = IndexLocation::Source::kHlsMediaPlaylist;
    location.uri = variant.uri;
    track.segments = SegmentIndexProvider::Deferred(std::move(location));
    return track;
  }

  absl::Status SetResolution(const AttributeMap& attributes,
                             HlsVariantKey& key) const {
    const auto value = Get(attributes, "RESOLUTION");
    if (!value) {
      return absl::OkStatus();
    }
    const std::size_t separator = value->find('x');
    if (separator == std::string::npos ||
        !Integer(std::string_view(*value).substr(0, separator), &key.width) ||
        !Integer(std::string_view(*value).substr(separator + 1), &key.height) ||
        key.width <= 0 || key.height <= 0) {
      return Failure(line_number_, "EXT-X-STREAM-INF", "invalid RESOLUTION");
    }
    return absl::OkStatus();
  }

  [[nodiscard]] static VideoTrackFormat MakeVideoFormat(
      const AttributeMap& attributes, const HlsVariantKey& key) {
    VideoTrackFormat format;
    format.width = key.width;
    format.height = key.height;
    const auto rate = Get(attributes, "FRAME-RATE");
    if (rate) {
      const auto value = Decimal(*rate, false);
      if (value && *value > 0) {
        format.frame_rate = penumbra::Rational::Create(
            static_cast<std::int64_t>(*value * 1000), 1000);
      }
    }
    const auto range = Get(attributes, "VIDEO-RANGE");
    if (range == "PQ") {
      format.range = VideoRange::kPq;
    } else if (range == "HLG") {
      format.range = VideoRange::kHlg;
    } else if (range == "SDR") {
      format.range = VideoRange::kSdr;
    }
    return format;
  }

  [[nodiscard]] static absl::StatusOr<HlsTrackExtension> MakeVariantExtension(
      const AttributeMap& attributes) {
    HlsTrackExtension extension;
    extension.audio_group = Get(attributes, "AUDIO");
    extension.subtitle_group = Get(attributes, "SUBTITLES");
    extension.closed_captions_group = Get(attributes, "CLOSED-CAPTIONS");
    extension.supplemental_codecs = Get(attributes, "SUPPLEMENTAL-CODECS");
    extension.pathway_id = Get(attributes, "PATHWAY-ID");
    extension.stable_variant_id = Get(attributes, "STABLE-VARIANT-ID");
    const auto score = Get(attributes, "SCORE");
    if (score) {
      extension.score = Decimal(*score, false);
      if (!extension.score) {
        return InvalidManifest("EXT-X-STREAM-INF has an invalid SCORE");
      }
    }
    const auto hdcp = Get(attributes, "HDCP-LEVEL");
    if (hdcp == "NONE") {
      extension.hdcp_level = HdcpLevel::kNone;
    } else if (hdcp == "TYPE-0") {
      extension.hdcp_level = HdcpLevel::kType0;
    } else if (hdcp == "TYPE-1") {
      extension.hdcp_level = HdcpLevel::kType1;
    } else if (hdcp) {
      return InvalidManifest("EXT-X-STREAM-INF has an invalid HDCP-LEVEL");
    }
    return extension;
  }

  void SetAllowedAudio(Track& track) const {
    const auto* extension = std::get_if<HlsTrackExtension>(&track.extension);
    if (extension == nullptr || !extension->audio_group) {
      return;
    }
    const auto found = audio_groups_.find(*extension->audio_group);
    if (found != audio_groups_.end()) {
      track.allowed_audio = found->second;
    }
  }

  void BuildIframeTracks(Period& period) const {
    for (const PlaylistVariant& variant : iframe_variants_) {
      if (variant.ignored) {
        continue;
      }
      Track track;
      track.id = HlsTrickPlayTrackId(VariantKeyForIframe(variant));
      const auto bandwidth = Get(variant.attributes, "BANDWIDTH");
      std::int64_t parsed_bandwidth = 0;
      if (bandwidth && Integer(*bandwidth, &parsed_bandwidth)) {
        track.bandwidth_bps = parsed_bandwidth;
      } else {
        track.bandwidth_bps.reset();
      }
      track.format = VideoTrackFormat{};
      const HlsTrackExtension extension;
      track.extension = extension;
      IndexLocation location;
      location.source = IndexLocation::Source::kHlsMediaPlaylist;
      location.uri = variant.uri;
      track.segments = SegmentIndexProvider::Deferred(std::move(location));
      period.trick_play.push_back(Freeze(std::move(track)));
    }
  }

  [[nodiscard]] static HlsVariantKey VariantKeyForIframe(
      const PlaylistVariant& variant) {
    HlsVariantKey key;
    const auto bandwidth = Get(variant.attributes, "BANDWIDTH");
    if (bandwidth) {
      Integer(*bandwidth, &key.bandwidth);
    }
    key.codecs = Get(variant.attributes, "CODECS").value_or("");
    return key;
  }

  std::string_view input_;
  HlsParserOptions options_;
  std::map<std::string, std::string, std::less<>> variables_;
  Presentation result_;
  HlsPresentationExtension hls_;
  LiveInfo live_;
  std::vector<PlaylistVariant> variants_;
  std::vector<PlaylistVariant> iframe_variants_;
  std::vector<PlaylistRendition> renditions_;
  std::map<std::string, std::vector<SwitchingSetId>, std::less<>> audio_groups_;
  std::vector<Segment> segments_;
  std::vector<InitSection> init_sections_;
  std::vector<KeyReference> keys_;
  std::vector<HlsPart> pending_parts_;
  std::vector<AdBreak> ad_breaks_;
  std::optional<PlaylistVariant> pending_variant_;
  std::optional<double> pending_duration_;
  std::optional<ByteRange> pending_range_;
  std::optional<std::int64_t> pending_bitrate_kbps_;
  std::optional<std::int64_t> range_cursor_;
  std::optional<std::uint32_t> active_init_index_;
  std::optional<std::uint32_t> active_key_index_;
  std::optional<absl::Time> pending_program_date_time_;
  std::int64_t media_sequence_ = 0;
  std::int64_t discontinuity_sequence_ = 0;
  std::int64_t current_discontinuity_ = 0;
  std::int64_t target_duration_ = 0;
  double cursor_seconds_ = 0;
  bool end_list_ = false;
  bool vod_ = false;
  bool iframe_only_ = false;
  bool pending_gap_ = false;
  bool has_hls_metadata_ = false;
  bool has_server_control_ = false;
  std::size_t line_number_ = 0;
};

}

absl::StatusOr<std::vector<HlsAttribute>> ParseHlsAttributeList(
    std::string_view input) {
  std::vector<HlsAttribute> result;
  std::size_t begin = 0;
  while (begin < input.size()) {
    if (result.size() >= kMaxAttributes) {
      return InvalidManifest("attribute count exceeds limit");
    }
    const std::size_t end = AttributeEnd(input, begin);
    auto attribute = ParseAttribute(input.substr(begin, end - begin));
    if (!attribute.ok()) {
      return attribute.status();
    }
    for (const HlsAttribute& existing : result) {
      if (existing.name == attribute->name) {
        return InvalidManifest("duplicate HLS attribute");
      }
    }
    result.push_back(std::move(*attribute));
    if (end == input.size()) {
      break;
    }
    begin = end + 1;
    if (begin == input.size()) {
      return InvalidManifest("trailing comma in attribute list");
    }
  }
  if (result.empty()) {
    return InvalidManifest("empty HLS attribute list");
  }
  return result;
}

absl::StatusOr<Presentation> ParseHlsPlaylist(std::string_view input,
                                              const HlsParserOptions& options) {
  return HlsParser(input, options).Run();
}

absl::Status SetHlsPresentationTypeFromMediaPlaylists(
    Presentation& master, std::span<const PresentationType> media_types) {
  if (master.protocol != Protocol::kHls || master.periods.empty() ||
      media_types.empty() || media_types.size() != DeferredTrackCount(master)) {
    return InvalidManifest(
        "HLS presentation type needs loaded media playlists");
  }
  const bool all_static = std::ranges::all_of(
      media_types,
      [](PresentationType type) { return type == PresentationType::kStatic; });
  master.type =
      all_static ? PresentationType::kStatic : PresentationType::kDynamic;
  if (all_static) {
    master.live.reset();
  } else if (!master.live) {
    master.live = LiveInfo{};
  }
  return absl::OkStatus();
}

}
