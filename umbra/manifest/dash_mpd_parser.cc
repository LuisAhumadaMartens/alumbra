#include "umbra/manifest/dash_mpd_parser.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
#include <absl/time/civil_time.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/time/time.h"
#include "penumbra/interface/encryption_info.h"
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
#include "umbra/observability/log.h"

namespace umbra::manifest {
namespace {

struct Node {
  std::string name;
  std::string text;
  std::map<std::string, std::string, std::less<>> attributes;
  std::vector<Node> children;
};

std::string_view LocalName(std::string_view name) {
  const std::size_t colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

class XmlReader {
 public:
  explicit XmlReader(std::string_view input) : input_(input) {}

  absl::StatusOr<Node> Read() {
    SkipSpace();
    if (absl::Status status = SkipComments(); !status.ok()) {
      return status;
    }
    if (input_.substr(position_, 5) == "<?xml") {
      const std::size_t end = input_.find("?>", position_);
      if (end == std::string_view::npos) {
        return InvalidManifest("invalid XML declaration");
      }
      position_ = end + 2;
    }
    SkipSpace();
    if (absl::Status status = SkipComments(); !status.ok()) {
      return status;
    }
    if (input_.substr(position_, 9) == "<!DOCTYPE" ||
        input_.substr(position_, 8) == "<!ENTITY") {
      return InvalidManifest("DTD and entity declarations are disabled");
    }
    auto root = ReadNode(0);
    if (!root.ok()) {
      return root.status();
    }
    SkipSpace();
    if (absl::Status status = SkipComments(); !status.ok()) {
      return status;
    }
    if (position_ != input_.size()) {
      return InvalidManifest("trailing XML data");
    }
    return root;
  }

 private:
  absl::Status SkipComments() {
    while (input_.substr(position_, 4) == "<!--") {
      const std::size_t end = input_.find("-->", position_ + 4);
      if (end == std::string_view::npos) {
        return InvalidManifest("unterminated XML comment");
      }
      position_ = end + 3;
      SkipSpace();
    }
    return absl::OkStatus();
  }

  void SkipSpace() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t')) {
      ++position_;
    }
  }

  absl::StatusOr<std::string> ReadName() {
    const std::size_t start = position_;
    while (position_ < input_.size()) {
      const char c = input_[position_];
      if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
          (c < '0' || c > '9') && c != ':' && c != '_' && c != '-' &&
          c != '.') {
        break;
      }
      ++position_;
    }
    if (start == position_) {
      return InvalidManifest("invalid XML name");
    }
    return std::string(input_.substr(start, position_ - start));
  }

  absl::StatusOr<std::string> ReadValue() {
    if (position_ >= input_.size() ||
        (input_[position_] != '\'' && input_[position_] != '"')) {
      return InvalidManifest("invalid XML attribute");
    }
    const char quote = input_[position_++];
    const std::size_t start = position_;
    while (position_ < input_.size() && input_[position_] != quote) {
      if (input_[position_] == '<') {
        return InvalidManifest("invalid XML attribute value");
      }
      if (input_[position_] == '&') {
        const std::size_t end = input_.find(';', position_);
        if (end == std::string_view::npos) {
          return InvalidManifest("invalid XML entity reference");
        }
        const std::string_view entity =
            input_.substr(position_, end - position_ + 1);
        if (entity != "&amp;" && entity != "&lt;" && entity != "&gt;" &&
            entity != "&quot;" && entity != "&apos;") {
          return InvalidManifest("unsupported XML entity reference");
        }
        position_ = end + 1;
      } else {
        ++position_;
      }
    }
    if (position_ == input_.size()) {
      return InvalidManifest("unterminated XML attribute");
    }
    std::string value(input_.substr(start, position_ - start));
    ++position_;
    return value;
  }

  absl::StatusOr<Node> ReadNode(std::size_t depth) {
    if (depth > 64 || ++nodes_ > 200'000) {
      return InvalidManifest("XML structure exceeds limits");
    }
    if (!ConsumeElementOpen()) {
      return InvalidManifest("expected XML element");
    }
    auto node = ReadStartTag();
    if (!node.ok()) {
      return node.status();
    }
    if (self_closing_) {
      self_closing_ = false;
      return node;
    }
    absl::Status content = ReadElementContent(*node, depth);
    if (!content.ok()) {
      return content;
    }
    return node;
  }

  bool ConsumeElementOpen() {
    if (position_ >= input_.size() || input_[position_] != '<') {
      return false;
    }
    ++position_;
    return position_ < input_.size() && input_[position_] != '/' &&
           input_[position_] != '!' && input_[position_] != '?';
  }

  absl::StatusOr<Node> ReadStartTag() {
    Node node;
    auto name = ReadName();
    if (!name.ok()) {
      return name.status();
    }
    node.name = LocalName(*name);
    while (true) {
      SkipSpace();
      if (input_.substr(position_, 2) == "/>") {
        position_ += 2;
        self_closing_ = true;
        return node;
      }
      if (position_ < input_.size() && input_[position_] == '>') {
        ++position_;
        break;
      }
      auto key = ReadName();
      if (!key.ok()) {
        return key.status();
      }
      SkipSpace();
      if (position_ >= input_.size() || input_[position_] != '=') {
        return InvalidManifest("expected XML attribute assignment");
      }
      ++position_;
      SkipSpace();
      auto value = ReadValue();
      if (!value.ok()) {
        return value.status();
      }
      if (!node.attributes.emplace(std::move(*key), std::move(*value)).second) {
        return InvalidManifest("duplicate XML attribute");
      }
    }
    return node;
  }

  absl::Status ReadElementContent(Node& node, std::size_t depth) {
    while (true) {
      auto complete = ReadContentItem(node, depth);
      if (!complete.ok()) {
        return complete.status();
      }
      if (*complete) {
        return absl::OkStatus();
      }
    }
  }

  absl::StatusOr<bool> ReadContentItem(Node& node, std::size_t depth) {
    SkipSpace();
    if (input_.substr(position_, 4) == "<!--") {
      absl::Status status = SkipComments();
      if (!status.ok()) {
        return status;
      }
      return false;
    }
    if (input_.substr(position_, 2) == "</") {
      return ReadClosingTag(node.name);
    }
    if (position_ >= input_.size()) {
      return InvalidManifest("unterminated XML element");
    }
    if (input_[position_] == '<') {
      auto child = ReadNode(depth + 1);
      if (!child.ok()) {
        return child.status();
      }
      node.children.push_back(std::move(*child));
      return false;
    }
    return ReadText(node);
  }

  absl::StatusOr<bool> ReadClosingTag(std::string_view expected_name) {
    position_ += 2;
    auto close = ReadName();
    if (!close.ok() || LocalName(*close) != expected_name) {
      return InvalidManifest("mismatched XML closing element");
    }
    SkipSpace();
    if (position_ >= input_.size() || input_[position_] != '>') {
      return InvalidManifest("invalid XML closing element");
    }
    ++position_;
    return true;
  }

  absl::StatusOr<bool> ReadText(Node& node) {
    const std::size_t end = input_.find('<', position_);
    if (end == std::string_view::npos) {
      return InvalidManifest("unterminated XML text");
    }
    const std::string_view content = input_.substr(position_, end - position_);
    const std::size_t first = content.find_first_not_of(" \n\r\t");
    if (first != std::string_view::npos) {
      const std::size_t last = content.find_last_not_of(" \n\r\t");
      if (!node.text.empty()) {
        node.text.push_back(' ');
      }
      node.text.append(content.substr(first, last - first + 1));
    }
    position_ = end;
    return false;
  }

  std::string_view input_;
  std::size_t position_ = 0;
  std::size_t nodes_ = 0;
  bool self_closing_ = false;
};

const std::string* Attribute(const Node& node, std::string_view name) {
  const auto found = node.attributes.find(name);
  if (found != node.attributes.end()) {
    return &found->second;
  }
  for (const auto& [key, value] : node.attributes) {
    if (LocalName(key) == name) {
      return &value;
    }
  }
  return nullptr;
}

std::optional<std::int64_t> ParseSigned(std::string_view text) {
  std::int64_t result = 0;
  if (!absl::SimpleAtoi(text, &result)) {
    return std::nullopt;
  }
  return result;
}

std::vector<const Node*> Children(const Node& node, std::string_view name) {
  std::vector<const Node*> result;
  for (const Node& child : node.children) {
    if (child.name == name) {
      result.push_back(&child);
    }
  }
  return result;
}

std::vector<std::string> ResolveBaseUrls(
    const Node& node, const std::vector<std::string>& parents);

std::string NormalizeUriPath(std::string_view prefix, std::size_t path_start,
                             bool leading_slash, bool trailing_slash) {
  const std::string authority(prefix.substr(0, path_start));
  std::vector<std::string> parts;
  std::size_t cursor = path_start + (leading_slash ? 1 : 0);
  while (cursor <= prefix.size()) {
    const std::size_t slash = prefix.find('/', cursor);
    const std::size_t end = slash == std::string::npos ? prefix.size() : slash;
    const std::string part(prefix.substr(cursor, end - cursor));
    if (part == "..") {
      if (!parts.empty()) {
        parts.pop_back();
      }
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    if (slash == std::string::npos) {
      break;
    }
    cursor = slash + 1;
  }
  std::string normalized = authority;
  if (leading_slash) {
    normalized.push_back('/');
  }
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      normalized.push_back('/');
    }
    normalized.append(parts[i]);
  }
  if (trailing_slash && normalized.back() != '/') {
    normalized.push_back('/');
  }
  return normalized;
}

void AddReferenceToPrefix(std::string& prefix, std::string_view reference,
                          std::size_t scheme, std::size_t authority_start) {
  if (reference.front() == '/') {
    if (scheme == std::string::npos) {
      prefix.clear();
      return;
    }
    const std::size_t authority_end = prefix.find('/', authority_start);
    prefix.resize(authority_end == std::string::npos ? prefix.size()
                                                     : authority_end);
    return;
  }
  if (prefix.back() == '/') {
    return;
  }
  const std::size_t slash = prefix.find_last_of('/');
  prefix.resize(slash == std::string::npos ? 0 : slash + 1);
}

std::string ResolveUri(std::string_view base, std::string_view reference) {
  if (reference.empty()) {
    return std::string(base);
  }
  if (reference.find("://") != std::string_view::npos ||
      reference.starts_with("//") || base.empty()) {
    return std::string(reference);
  }
  std::string prefix(base);
  const std::size_t scheme = prefix.find("://");
  const std::size_t authority_start =
      scheme == std::string::npos ? 0 : scheme + 3;
  AddReferenceToPrefix(prefix, reference, scheme, authority_start);
  prefix.append(reference);
  const std::size_t path_start =
      scheme == std::string::npos ? 0 : prefix.find('/', authority_start);
  if (path_start == std::string::npos) {
    return prefix;
  }
  return NormalizeUriPath(prefix, path_start,
                          scheme != std::string::npos || prefix.front() == '/',
                          prefix.back() == '/');
}

std::vector<std::string> ResolveBaseUrls(
    const Node& node, const std::vector<std::string>& parents) {
  const std::vector<const Node*> declarations = Children(node, "BaseURL");
  std::vector<std::string> bases =
      parents.empty() ? std::vector<std::string>{""} : parents;
  if (declarations.empty()) {
    return bases;
  }
  std::vector<std::string> resolved;
  for (const std::string& parent : bases) {
    for (const Node* declaration : declarations) {
      resolved.push_back(ResolveUri(parent, declaration->text));
    }
  }
  return resolved;
}

std::optional<std::int64_t> Integer(const Node& node, std::string_view name) {
  const std::string* value = Attribute(node, name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return ParseSigned(*value);
}

DashDescriptor Descriptor(const Node& node) {
  const std::string* scheme = Attribute(node, "schemeIdUri");
  const std::string* value = Attribute(node, "value");
  const std::string* id = Attribute(node, "id");
  return DashDescriptor{.scheme_id_uri = scheme == nullptr ? "" : *scheme,
                        .value = value == nullptr ? "" : *value,
                        .id = id == nullptr ? "" : *id};
}

bool IsKnownEssentialProperty(const Node& node) {
  const std::string* scheme = Attribute(node, "schemeIdUri");
  if (scheme == nullptr) {
    return false;
  }
  return *scheme == "urn:mpeg:dash:role:2011" ||
         *scheme == "urn:mpeg:dash:accessibility:2011" ||
         *scheme == "urn:mpeg:dash:adaptation-set-switching:2016" ||
         *scheme == "urn:mpeg:dash:audio-channel-configuration:2011" ||
         *scheme == "urn:mpeg:mpegB:cicp:ChannelConfiguration";
}

bool HasUnknownEssentialProperty(const Node& node) {
  return std::ranges::any_of(Children(node, "EssentialProperty"),
                             [](const Node* property) {
                               return !IsKnownEssentialProperty(*property);
                             });
}

std::optional<Role> ParseRoleValue(std::string_view value) {
  if (value == "main") {
    return Role::kMain;
  }
  if (value == "alternate") {
    return Role::kAlternate;
  }
  if (value == "supplementary") {
    return Role::kSupplementary;
  }
  if (value == "commentary") {
    return Role::kCommentary;
  }
  if (value == "dub") {
    return Role::kDub;
  }
  if (value == "emergency") {
    return Role::kEmergency;
  }
  if (value == "caption") {
    return Role::kCaption;
  }
  if (value == "subtitle") {
    return Role::kSubtitle;
  }
  if (value == "description") {
    return Role::kDescription;
  }
  if (value == "enhanced-audio-intelligibility") {
    return Role::kEnhancedAudioIntelligibility;
  }
  if (value == "sign") {
    return Role::kSign;
  }
  return std::nullopt;
}

std::optional<penumbra::Rational> ParseRationalText(std::string_view text) {
  const std::size_t slash = text.find('/');
  if (slash != std::string_view::npos) {
    const auto numerator = ParseSigned(text.substr(0, slash));
    const auto denominator = ParseSigned(text.substr(slash + 1));
    if (numerator.has_value() && denominator.has_value()) {
      return penumbra::Rational::Create(*numerator, *denominator);
    }
    return std::nullopt;
  }
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos) {
    const auto numerator = ParseSigned(text);
    if (numerator.has_value()) {
      return penumbra::Rational::Create(*numerator, 1);
    }
    return std::nullopt;
  }
  const auto whole = ParseSigned(text.substr(0, dot));
  const std::string_view fraction = text.substr(dot + 1);
  std::int64_t fractional = 0;
  if (!whole.has_value() || fraction.empty() || fraction.size() > 6 ||
      !absl::SimpleAtoi(fraction, &fractional)) {
    return std::nullopt;
  }
  std::int64_t denominator = 1;
  for (std::size_t i = 0; i < fraction.size(); ++i) {
    denominator *= 10;
  }
  return penumbra::Rational::Create((*whole * denominator) + fractional,
                                    denominator);
}

void ReadRepresentationDescriptors(const Node& node,
                                   DashTrackExtension& extension) {
  for (const Node* property : Children(node, "SupplementalProperty")) {
    const DashDescriptor descriptor = Descriptor(*property);
    extension.supplemental_properties.push_back(descriptor.scheme_id_uri + "|" +
                                                descriptor.value + "|" +
                                                descriptor.id);
  }
  for (const Node* property : Children(node, "EssentialProperty")) {
    const std::string* scheme = Attribute(*property, "schemeIdUri");
    const std::string* value = Attribute(*property, "value");
    if (scheme != nullptr &&
        *scheme == "urn:mpeg:dash:adaptation-set-switching:2016" &&
        value != nullptr) {
      std::size_t start = 0;
      while (start < value->size()) {
        const std::size_t end = value->find(',', start);
        const std::size_t count =
            end == std::string::npos ? value->size() - start : end - start;
        extension.dependency_ids.push_back(value->substr(start, count));
        start += count + 1;
      }
    }
  }
}

void ReadCicpField(const Node& adaptation, const Node& representation,
                   std::string_view scheme, std::uint8_t& target) {
  for (const Node* node : {&representation, &adaptation}) {
    for (const std::string_view name :
         {"SupplementalProperty", "EssentialProperty"}) {
      for (const Node* property : Children(*node, name)) {
        const std::string* property_scheme =
            Attribute(*property, "schemeIdUri");
        const std::string* value = Attribute(*property, "value");
        int parsed = 0;
        if (property_scheme != nullptr && *property_scheme == scheme &&
            value != nullptr && absl::SimpleAtoi(*value, &parsed) &&
            parsed >= 0 && parsed <= 255) {
          target = static_cast<std::uint8_t>(parsed);
          return;
        }
      }
    }
  }
}

void ReadCicp(const Node& adaptation, const Node& representation,
              VideoTrackFormat& format) {
  ReadCicpField(adaptation, representation,
                "urn:mpeg:mpegB:cicp:ColourPrimaries", format.color.primaries);
  ReadCicpField(adaptation, representation,
                "urn:mpeg:mpegB:cicp:TransferCharacteristics",
                format.color.transfer);
  ReadCicpField(adaptation, representation,
                "urn:mpeg:mpegB:cicp:MatrixCoefficients", format.color.matrix);
}

void ReadVideoTrackFormat(const Node& adaptation, const Node& representation,
                          Track& track) {
  VideoTrackFormat format;
  format.width = static_cast<std::int32_t>(
      Integer(representation, "width")
          .value_or(Integer(adaptation, "width").value_or(0)));
  format.height = static_cast<std::int32_t>(
      Integer(representation, "height")
          .value_or(Integer(adaptation, "height").value_or(0)));
  const std::string* frame_rate = Attribute(representation, "frameRate");
  if (frame_rate == nullptr) {
    frame_rate = Attribute(adaptation, "frameRate");
  }
  if (frame_rate != nullptr) {
    format.frame_rate = ParseRationalText(*frame_rate);
  }
  format.bit_depth = static_cast<std::int32_t>(
      Integer(representation, "bitDepth")
          .value_or(Integer(adaptation, "bitDepth").value_or(0)));
  const std::string* range = Attribute(representation, "videoRange");
  if (range == nullptr) {
    range = Attribute(adaptation, "videoRange");
  }
  if (range != nullptr && (*range == "PQ" || *range == "pq")) {
    format.range = VideoRange::kPq;
  } else if (range != nullptr && (*range == "HLG" || *range == "hlg")) {
    format.range = VideoRange::kHlg;
  } else if (range != nullptr) {
    format.range = VideoRange::kSdr;
  }
  ReadCicp(adaptation, representation, format);
  if (format.range == VideoRange::kUnspecified) {
    if (format.color.transfer == 16) {
      format.range = VideoRange::kPq;
    } else if (format.color.transfer == 18) {
      format.range = VideoRange::kHlg;
    }
  } else if (format.color.transfer == penumbra::ColorSpace::kUnspecified &&
             (format.range == VideoRange::kPq ||
              format.range == VideoRange::kHlg)) {
    format.color.transfer = format.range == VideoRange::kPq ? 16 : 18;
  }
  for (const codec::CodecDescription& description : track.codecs) {
    if (const auto* dolby =
            std::get_if<codec::DolbyVisionCodec>(&description)) {
      format.dolby_vision =
          penumbra::DolbyVisionConfig{.profile = dolby->profile,
                                      .level = dolby->level,
                                      .base_layer_compatibility_id = 0};
      break;
    }
  }
  track.format = format;
}

void ReadTrackFormat(const Node& adaptation, const Node& representation,
                     const SwitchingSet& set, Track& track) {
  if (set.type == penumbra::StreamType::kVideo) {
    ReadVideoTrackFormat(adaptation, representation, track);
  } else if (set.type == penumbra::StreamType::kAudio) {
    AudioTrackFormat format;
    format.sample_rate = static_cast<std::int32_t>(
        Integer(representation, "audioSamplingRate")
            .value_or(Integer(adaptation, "audioSamplingRate").value_or(0)));
    format.channels = set.channels;
    track.format = format;
  } else {
    track.format = TextTrackFormat{};
  }
}

void ReadDescriptors(const Node& node, SwitchingSet& set) {
  for (const Node* role : Children(node, "Role")) {
    const std::string* scheme = Attribute(*role, "schemeIdUri");
    const std::string* value = Attribute(*role, "value");
    if (scheme != nullptr && *scheme == "urn:mpeg:dash:role:2011" &&
        value != nullptr) {
      const std::optional<Role> parsed = ParseRoleValue(*value);
      if (parsed.has_value()) {
        set.roles.push_back(*parsed);
      }
    }
  }
  for (const Node* item : Children(node, "Accessibility")) {
    set.accessibility.push_back(Descriptor(*item));
  }
  for (const Node* item : Children(node, "EssentialProperty")) {
    set.essential_properties.push_back(Descriptor(*item));
  }
  for (const Node* item : Children(node, "SupplementalProperty")) {
    set.supplemental_properties.push_back(Descriptor(*item));
  }
  for (const Node* label : Children(node, "Label")) {
    if (set.label.empty()) {
      set.label = label->text;
    }
  }
  for (const Node* configuration :
       Children(node, "AudioChannelConfiguration")) {
    const std::optional<std::int64_t> count = Integer(*configuration, "value");
    if (count.has_value() && *count >= 0 && *count <= 255) {
      set.channels.channel_count = static_cast<std::int32_t>(*count);
    }
  }
}

int HexDigit(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

std::optional<SystemId> ParseSystemId(std::string_view value) {
  std::array<std::uint8_t, 16> result{};
  std::size_t output = 0;
  bool high = true;
  std::uint8_t byte = 0;
  for (char const c : value) {
    if (c == '-') {
      continue;
    }
    const int digit = HexDigit(c);
    if (digit < 0 || output >= result.size()) {
      return std::nullopt;
    }
    if (high) {
      byte = static_cast<std::uint8_t>(digit << 4);
      high = false;
    } else {
      result.at(output++) = static_cast<std::uint8_t>(byte | digit);
      high = true;
    }
  }
  return high && output == result.size() ? std::optional<SystemId>(result)
                                         : std::nullopt;
}

std::optional<std::vector<std::uint8_t>> DecodeBase64(std::string_view text) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<std::uint8_t> output;
  std::uint32_t accumulator = 0;
  int bits = 0;
  for (char const value : text) {
    if (value == '=' || value == ' ' || value == '\n' || value == '\r' ||
        value == '\t') {
      continue;
    }
    const std::size_t digit = alphabet.find(value);
    if (digit == std::string_view::npos) {
      return std::nullopt;
    }
    accumulator = (accumulator << 6) | static_cast<std::uint32_t>(digit);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<std::uint8_t>(accumulator >> bits));
      if (output.size() > kMaxInitDataBytes) {
        return std::nullopt;
      }
    }
  }
  return output;
}

void AddProtectionSystemId(std::optional<SystemId> system_id,
                           ProtectionInfo& info) {
  if (system_id.has_value() &&
      std::ranges::find(info.system_ids, *system_id) == info.system_ids.end()) {
    info.system_ids.push_back(*system_id);
  }
}

void ReadProtectionInitData(const Node& node, ProtectionInfo& info) {
  for (const Node* pssh : Children(node, "pssh")) {
    const auto decoded = DecodeBase64(pssh->text);
    if (!decoded.has_value() || decoded->size() < 24) {
      continue;
    }
    SystemId system_id;
    const std::span<const std::uint8_t> pssh_bytes(*decoded);
    const auto system_id_bytes = pssh_bytes.subspan(4, system_id.size());
    std::ranges::copy(system_id_bytes, system_id.begin());
    AddProtectionSystemId(system_id, info);
    info.init_data.push_back(
        InitData{.system_id = system_id, .data = *decoded});
  }
}

void ReadProtectionEntry(const Node& node, ProtectionInfo& info) {
  const std::string* scheme = Attribute(node, "schemeIdUri");
  const std::string* value = Attribute(node, "value");
  if (scheme != nullptr && *scheme == "urn:mpeg:dash:mp4protection:2011" &&
      value != nullptr && *value == "cbcs") {
    info.scheme = penumbra::EncryptionScheme::kCbcs;
  }
  if (scheme != nullptr && scheme->starts_with("urn:uuid:")) {
    AddProtectionSystemId(ParseSystemId(std::string_view(*scheme).substr(9)),
                          info);
  }
  if (const std::string* key_id = Attribute(node, "default_KID")) {
    const auto parsed = ParseSystemId(*key_id);
    if (parsed.has_value()) {
      info.default_key_id = *parsed;
    }
  }
  ReadProtectionInitData(node, info);
}

void ReadProtection(const Node& node, SwitchingSet& set) {
  for (const Node* protection : Children(node, "ContentProtection")) {
    if (!set.protection.has_value()) {
      set.protection.emplace();
    }
    ReadProtectionEntry(*protection, *set.protection);
  }
}

struct DurationParts {
  std::int64_t years = 0;
  std::int64_t months = 0;
  std::int64_t days = 0;
  std::int64_t hours = 0;
  std::int64_t minutes = 0;
  std::int64_t seconds = 0;
  std::int64_t fractional_micros = 0;
};

absl::StatusOr<std::pair<std::string_view, char>> ReadDurationComponent(
    std::string_view text, std::size_t& position) {
  const std::size_t start = position;
  while (position < text.size() &&
         ((text[position] >= '0' && text[position] <= '9') ||
          text[position] == '.')) {
    ++position;
  }
  if (position == start || position == text.size()) {
    return InvalidManifest("invalid xs:duration component");
  }
  const std::string_view number = text.substr(start, position - start);
  return std::pair<std::string_view, char>{number, text[position++]};
}

absl::StatusOr<std::pair<std::int64_t, std::int64_t>> ParseDurationNumber(
    std::string_view number, bool seconds) {
  const std::size_t dot = number.find('.');
  const std::string_view whole_text = number.substr(0, dot);
  const auto whole = ParseSigned(whole_text);
  if (!whole.has_value() || *whole < 0) {
    return InvalidManifest("invalid xs:duration number");
  }
  std::int64_t fraction_micros = 0;
  if (dot != std::string_view::npos) {
    const std::string_view fraction = number.substr(dot + 1);
    if (!seconds || fraction.empty() || fraction.size() > 6) {
      return InvalidManifest("xs:duration precision exceeds microseconds");
    }
    if (!absl::SimpleAtoi(fraction, &fraction_micros)) {
      return InvalidManifest("invalid xs:duration fraction");
    }
    for (std::size_t i = fraction.size(); i < 6; ++i) {
      fraction_micros *= 10;
    }
  }
  return std::pair<std::int64_t, std::int64_t>{*whole, fraction_micros};
}

absl::StatusOr<DurationParts> ParseDurationParts(std::string_view text) {
  if (text.size() < 3 || text.front() != 'P') {
    return InvalidManifest("invalid xs:duration");
  }
  DurationParts parts;
  std::size_t position = 1;
  bool in_time = false;
  bool found = false;
  while (position < text.size()) {
    if (text[position] == 'T' && !in_time) {
      in_time = true;
      ++position;
      continue;
    }
    auto component = ReadDurationComponent(text, position);
    if (!component.ok()) {
      return component.status();
    }
    const auto number =
        ParseDurationNumber(component->first, component->second == 'S');
    if (!number.ok()) {
      return number.status();
    }
    const std::int64_t value = number->first;
    switch (component->second) {
      case 'Y':
        parts.years = value;
        break;
      case 'M':
        if (in_time) {
          parts.minutes = value;
        } else {
          parts.months = value;
        }
        break;
      case 'D':
        parts.days = value;
        break;
      case 'H':
        parts.hours = value;
        break;
      case 'S':
        parts.seconds = value;
        parts.fractional_micros = number->second;
        break;
      default:
        return InvalidManifest("unknown xs:duration component");
    }
    found = true;
  }
  if (!found) {
    return InvalidManifest("xs:duration has no components");
  }
  return parts;
}

absl::StatusOr<std::int64_t> CalendarDurationMicros(const DurationParts& parts,
                                                    absl::Time anchor) {
  if (parts.years > std::numeric_limits<std::int64_t>::max() / 12) {
    return InvalidManifest("xs:duration calendar value overflows");
  }
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::CivilSecond civil = absl::ToCivilSecond(anchor, utc);
  const std::int64_t total_months = (parts.years * 12) + parts.months;
  const absl::CivilMonth month =
      absl::CivilMonth(civil.year(), civil.month()) + total_months;
  const absl::CivilDay month_end = absl::CivilDay(month + 1) - 1;
  const int day = std::min(civil.day(), month_end.day());
  const absl::CivilSecond shifted(month.year(), month.month(), day,
                                  civil.hour(), civil.minute(), civil.second());
  const absl::Time shifted_anchor = absl::FromCivil(shifted, utc);
  return absl::ToInt64Microseconds(shifted_anchor - anchor);
}

absl::StatusOr<penumbra::TimeDelta> ParseDuration(
    std::string_view text, std::optional<absl::Time> anchor) {
  auto parsed = ParseDurationParts(text);
  if (!parsed.ok()) {
    return parsed.status();
  }
  std::int64_t micros = 0;
  if (parsed->years != 0 || parsed->months != 0) {
    const absl::Time calendar_anchor = anchor.value_or(absl::UnixEpoch());
    auto calendar = CalendarDurationMicros(*parsed, calendar_anchor);
    if (!calendar.ok()) {
      return calendar.status();
    }
    micros = *calendar;
  }
  const std::array<std::pair<std::int64_t, std::int64_t>, 4> components = {
      std::pair{parsed->days, 86'400'000'000LL},
      std::pair{parsed->hours, 3'600'000'000LL},
      std::pair{parsed->minutes, 60'000'000LL},
      std::pair{parsed->seconds, 1'000'000LL}};
  for (const auto& [value, scale] : components) {
    if (value > std::numeric_limits<std::int64_t>::max() / scale ||
        value * scale > std::numeric_limits<std::int64_t>::max() - micros) {
      return InvalidManifest("xs:duration exceeds the media time range");
    }
    micros += value * scale;
  }
  if (parsed->fractional_micros >
      std::numeric_limits<std::int64_t>::max() - micros) {
    return InvalidManifest("xs:duration exceeds the media time range");
  }
  micros += parsed->fractional_micros;
  return penumbra::TimeDelta::FromMicroseconds(micros);
}

absl::StatusOr<std::optional<penumbra::TimeDelta>> DurationAttribute(
    const Node& node, std::string_view name, std::optional<absl::Time> anchor) {
  const std::string* value = Attribute(node, name);
  if (value == nullptr) {
    return std::optional<penumbra::TimeDelta>();
  }
  auto duration = ParseDuration(*value, anchor);
  if (!duration.ok()) {
    return duration.status();
  }
  return std::optional<penumbra::TimeDelta>(*duration);
}

absl::StatusOr<std::optional<absl::Time>> DateTimeAttribute(
    const Node& node, std::string_view name) {
  const std::string* value = Attribute(node, name);
  if (value == nullptr) {
    return std::optional<absl::Time>();
  }
  absl::Time parsed;
  constexpr std::string_view format = "%Y-%m-%d%ET%H:%M:%E*S%Ez";
  std::string error;
  if (!absl::ParseTime(format, *value, &parsed, &error)) {
    return InvalidManifest("invalid xs:dateTime value");
  }
  return std::optional<absl::Time>(parsed);
}

std::optional<std::int64_t> TicksToMicros(std::int64_t ticks,
                                          std::int64_t timescale) {
  if (timescale <= 0) {
    return std::nullopt;
  }
  const auto base = penumbra::Rational::Create(1, timescale);
  if (!base.has_value()) {
    return std::nullopt;
  }
  return penumbra::Rescale(ticks, *base, penumbra::kMicrosecondTimeBase,
                           penumbra::Rounding::kDown);
}

absl::StatusOr<std::int64_t> NegativeRepeatCount(
    const Node& entry, const Node* next_entry, std::int64_t timescale,
    penumbra::TimeDelta period_duration, std::int64_t presentation_offset,
    std::int64_t start, std::int64_t length) {
  const std::int64_t repeat = Integer(entry, "r").value_or(0);
  if (repeat < -1) {
    return InvalidManifest("SegmentTimeline r is below -1");
  }
  if (repeat >= 0) {
    if (repeat == std::numeric_limits<std::int64_t>::max()) {
      return InvalidManifest("SegmentTimeline repeat count overflows");
    }
    return repeat + 1;
  }
  std::optional<std::int64_t> end;
  if (next_entry != nullptr) {
    end = Integer(*next_entry, "t");
  }
  if (!end.has_value() && period_duration.is_finite() && timescale > 0) {
    const std::int64_t micros = period_duration.InMicroseconds();
    const auto time_base = penumbra::Rational::Create(1, timescale);
    const auto period_ticks =
        time_base.has_value()
            ? penumbra::Rescale(micros, penumbra::kMicrosecondTimeBase,
                                *time_base, penumbra::Rounding::kDown)
            : std::nullopt;
    std::int64_t end_ticks = 0;
    if (period_ticks.has_value() &&
        !__builtin_add_overflow(presentation_offset, *period_ticks,
                                &end_ticks)) {
      end = end_ticks;
    }
  }
  if (!end.has_value() || *end <= start) {
    return InvalidManifest("negative SegmentTimeline r has no finite end");
  }
  std::int64_t span = 0;
  if (__builtin_sub_overflow(*end, start, &span)) {
    return InvalidManifest("negative SegmentTimeline span overflows");
  }
  return (span / length) + (span % length == 0 ? 0 : 1);
}

absl::StatusOr<std::vector<TimelineRun>> ParseTimelineRuns(
    const Node& timeline, std::int64_t timescale,
    penumbra::TimeDelta period_duration, std::int64_t presentation_offset) {
  const std::vector<const Node*> entries = Children(timeline, "S");
  std::vector<TimelineRun> runs;
  std::int64_t next_start = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Node& entry = *entries[i];
    const std::int64_t start = Integer(entry, "t").value_or(next_start);
    const std::optional<std::int64_t> length = Integer(entry, "d");
    if (!length.has_value() || *length <= 0) {
      return InvalidManifest("SegmentTimeline S needs positive d");
    }
    const Node* next_entry = i + 1 < entries.size() ? entries[i + 1] : nullptr;
    auto repeat_count =
        NegativeRepeatCount(entry, next_entry, timescale, period_duration,
                            presentation_offset, start, *length);
    if (!repeat_count.ok()) {
      return repeat_count.status();
    }
    const std::int64_t count = *repeat_count;
    if (count < 1 || count > kMaxSegmentsPerIndex) {
      return InvalidManifest("SegmentTimeline run exceeds the segment limit");
    }
    runs.push_back(TimelineRun{
        .start_ticks = start, .duration_ticks = *length, .count = count});
    if (__builtin_mul_overflow(*length, count, &next_start) ||
        __builtin_add_overflow(start, next_start, &next_start)) {
      return InvalidManifest("SegmentTimeline tick range overflows");
    }
  }
  return runs;
}

const Node* Child(const Node& node, std::string_view name) {
  for (const Node& child : node.children) {
    if (child.name == name) {
      return &child;
    }
  }
  return nullptr;
}

const Node* SegmentElement(const Node& node, std::string_view name) {
  if (const Node* value = Child(node, name)) {
    return value;
  }
  return nullptr;
}

std::optional<Node> InheritedSegmentElement(
    const std::vector<const Node*>& levels, std::string_view name) {
  Node result;
  result.name = std::string(name);
  bool found = false;
  for (const Node* level : levels) {
    if (const Node* element = SegmentElement(*level, name)) {
      found = true;
      for (const auto& [key, value] : element->attributes) {
        result.attributes.insert_or_assign(key, value);
      }
    }
  }
  if (!found) {
    return std::nullopt;
  }
  std::map<std::string, bool, std::less<>> replaced_children;
  for (const Node* level : std::views::reverse(levels)) {
    const Node* element = SegmentElement(*level, name);
    if (element == nullptr) {
      continue;
    }
    for (const Node& child : element->children) {
      if (replaced_children.contains(child.name)) {
        continue;
      }
      result.children.push_back(child);
    }
    for (const Node& child : element->children) {
      replaced_children.insert_or_assign(child.name, true);
    }
  }
  return result;
}

void ReadTemplateInitialization(const Node& segment_template,
                                std::string_view base_url,
                                TemplateSegmentIndexParameters& parameters);
absl::Status ReadTemplateRuns(
    const Node& segment_template, std::int64_t timescale, std::int64_t offset,
    penumbra::TimeDelta model_period_duration,
    const std::optional<penumbra::TimeDelta>& period_duration,
    TemplateSegmentIndexParameters& parameters);

absl::StatusOr<SegmentIndexProvider> ParseTemplateIndex(
    const Node& mpd, const Node& period, const Node& adaptation,
    const Node& representation, std::string_view base_url,
    std::string_view representation_id, std::int64_t bandwidth,
    const Period& model_period,
    const std::optional<penumbra::TimeDelta>& period_duration,
    TimelineMapping& mapping) {
  const auto segment_template = InheritedSegmentElement(
      {&mpd, &period, &adaptation, &representation}, "SegmentTemplate");
  if (!segment_template.has_value()) {
    return InvalidManifest("missing SegmentTemplate");
  }
  const std::string* media = Attribute(*segment_template, "media");
  if (media == nullptr) {
    return InvalidManifest("SegmentTemplate needs media");
  }
  const std::int64_t timescale =
      Integer(*segment_template, "timescale").value_or(1);
  const std::int64_t offset =
      Integer(*segment_template, "presentationTimeOffset").value_or(0);
  const std::int64_t sequence =
      Integer(*segment_template, "startNumber").value_or(1);
  const auto time_base = penumbra::Rational::Create(1, timescale);
  if (!time_base.has_value() || sequence < 0) {
    return InvalidManifest("invalid SegmentTemplate timescale or startNumber");
  }
  TemplateSegmentIndexParameters parameters;
  parameters.media_template = ResolveUri(base_url, *media);
  parameters.representation_id = std::string(representation_id);
  parameters.bandwidth = bandwidth;
  parameters.first_sequence_number = sequence;
  parameters.time_base = *time_base;
  parameters.period_start = model_period.start;
  parameters.presentation_time_offset_ticks = offset;
  ReadTemplateInitialization(*segment_template, base_url, parameters);
  if (absl::Status status =
          ReadTemplateRuns(*segment_template, timescale, offset,
                           model_period.duration, period_duration, parameters);
      !status.ok()) {
    return status;
  }
  if (const auto end_number = Integer(*segment_template, "endNumber")) {
    if (*end_number < sequence) {
      return InvalidManifest("SegmentTemplate endNumber precedes startNumber");
    }
    std::int64_t remaining = 0;
    if (__builtin_sub_overflow(*end_number, sequence, &remaining) ||
        __builtin_add_overflow(remaining, std::int64_t{1}, &remaining) ||
        remaining > kMaxSegmentsPerIndex) {
      return InvalidManifest("SegmentTemplate endNumber exceeds the limit");
    }
    for (TimelineRun& run : parameters.runs) {
      if (remaining <= 0) {
        run.count = 0;
      } else if (run.count > remaining) {
        run.count = remaining;
        remaining = 0;
      } else {
        remaining -= run.count;
      }
    }
    const auto removed = std::ranges::remove_if(
        parameters.runs, [](const TimelineRun& run) { return run.count == 0; });
    parameters.runs.erase(removed.begin(), removed.end());
  }
  const auto media_origin = penumbra::ToMediaTime(
      penumbra::Timestamp{.value = offset, .time_base = *time_base},
      penumbra::Rounding::kDown);
  if (!media_origin.has_value()) {
    return InvalidManifest("invalid presentationTimeOffset");
  }
  mapping = TimelineMapping{.media_origin = *media_origin,
                            .presentation_origin = model_period.start};
  auto index = TemplateSegmentIndex::Create(std::move(parameters));
  if (!index.ok()) {
    return index.status();
  }
  return SegmentIndexProvider::Resolved(*index);
}

void ReadTemplateInitialization(const Node& segment_template,
                                std::string_view base_url,
                                TemplateSegmentIndexParameters& parameters) {
  if (const std::string* initialization =
          Attribute(segment_template, "initialization")) {
    parameters.init_section =
        InitSection{.uri = ResolveUri(base_url, *initialization),
                    .byte_range = std::nullopt,
                    .key_index = std::nullopt};
  } else if (const Node* initialization =
                 Child(segment_template, "Initialization")) {
    const std::string* source = Attribute(*initialization, "sourceURL");
    if (source != nullptr) {
      parameters.init_section =
          InitSection{.uri = ResolveUri(base_url, *source),
                      .byte_range = std::nullopt,
                      .key_index = std::nullopt};
    }
  }
}

absl::Status ReadTemplateRuns(
    const Node& segment_template, std::int64_t timescale, std::int64_t offset,
    penumbra::TimeDelta model_period_duration,
    const std::optional<penumbra::TimeDelta>& period_duration,
    TemplateSegmentIndexParameters& parameters) {
  if (const Node* timeline = Child(segment_template, "SegmentTimeline")) {
    auto runs =
        ParseTimelineRuns(*timeline, timescale, model_period_duration, offset);
    if (!runs.ok()) {
      return runs.status();
    }
    parameters.runs = std::move(*runs);
    return absl::OkStatus();
  }
  const auto duration = Integer(segment_template, "duration");
  if (duration.has_value()) {
    if (*duration <= 0) {
      return InvalidManifest("SegmentTemplate duration is invalid");
    }
    std::int64_t count = 1;
    if (period_duration.has_value() && period_duration->is_finite()) {
      const std::int64_t micros = period_duration->InMicroseconds();
      if (micros > std::numeric_limits<std::int64_t>::max() / timescale) {
        return InvalidManifest("SegmentTemplate duration range overflows");
      }
      const std::int64_t ticks = micros * timescale / 1'000'000;
      count = (ticks / *duration) + (ticks % *duration == 0 ? 0 : 1);
    }
    if (count < 1 || count > kMaxSegmentsPerIndex) {
      return InvalidManifest("SegmentTemplate exceeds segment limit");
    }
    parameters.runs.push_back(TimelineRun{
        .start_ticks = offset, .duration_ticks = *duration, .count = count});
    return absl::OkStatus();
  }
  return InvalidManifest("SegmentTemplate needs SegmentTimeline or duration");
}

std::optional<ByteRange> ParseByteRange(std::string_view text);

absl::StatusOr<SegmentIndexProvider> ParseSegmentBase(
    const Node& mpd, const Node& period, const Node& adaptation,
    const Node& representation, std::string_view base_url) {
  const auto segment_base = InheritedSegmentElement(
      {&mpd, &period, &adaptation, &representation}, "SegmentBase");
  if (!segment_base.has_value()) {
    return SegmentIndexProvider::Deferred(
        IndexLocation{.source = IndexLocation::Source::kDashSegmentBase,
                      .uri = std::string(base_url),
                      .byte_range = std::nullopt,
                      .init_section = std::nullopt,
                      .index_range_exact = false});
  }
  IndexLocation location;
  location.uri = std::string(base_url);
  if (const Node* initialization = Child(*segment_base, "Initialization")) {
    const std::string* source = Attribute(*initialization, "sourceURL");
    const std::string* range = Attribute(*initialization, "range");
    std::optional<ByteRange> byte_range;
    if (range != nullptr) {
      byte_range = ParseByteRange(*range);
      if (!byte_range.has_value()) {
        return InvalidManifest("invalid SegmentBase initialization range");
      }
    }
    location.init_section =
        InitSection{.uri = source == nullptr ? std::string(base_url)
                                             : ResolveUri(base_url, *source),
                    .byte_range = byte_range,
                    .key_index = std::nullopt};
  }
  const std::string* index_range = Attribute(*segment_base, "indexRange");
  if (index_range == nullptr) {
    location.source = IndexLocation::Source::kDashSegmentBase;
    return SegmentIndexProvider::Deferred(std::move(location));
  }
  const std::optional<ByteRange> parsed_range = ParseByteRange(*index_range);
  if (!parsed_range.has_value()) {
    return InvalidManifest("invalid SegmentBase indexRange");
  }
  location.source = IndexLocation::Source::kDashSegmentIndexBox;
  location.byte_range = parsed_range;
  if (const std::string* exact = Attribute(*segment_base, "indexRangeExact")) {
    if (*exact == "true" || *exact == "1") {
      location.index_range_exact = true;
    } else if (*exact != "false" && *exact != "0") {
      return InvalidManifest("invalid SegmentBase indexRangeExact");
    }
  }
  return SegmentIndexProvider::Deferred(std::move(location));
}

std::optional<ByteRange> ParseByteRange(std::string_view text) {
  const std::size_t dash = text.find('-');
  if (dash == std::string_view::npos) {
    return std::nullopt;
  }
  const auto first = ParseSigned(text.substr(0, dash));
  const auto last = ParseSigned(text.substr(dash + 1));
  if (!first.has_value() || !last.has_value() || *first < 0 || *last < *first) {
    return std::nullopt;
  }
  std::int64_t length = 0;
  if (__builtin_sub_overflow(*last, *first, &length) ||
      __builtin_add_overflow(length, std::int64_t{1}, &length)) {
    return std::nullopt;
  }
  return ByteRange{.offset = *first, .length = length};
}

struct SegmentListCursor {
  std::size_t run_index = 0;
  std::int64_t run_offset = 0;
  std::int64_t next_tick = 0;
};

absl::Status ReadListInitialization(
    const Node& list, const std::vector<std::string>& base_urls,
    ExplicitSegmentIndexParameters& parameters) {
  const Node* initialization = Child(list, "Initialization");
  if (initialization == nullptr || base_urls.empty()) {
    return absl::OkStatus();
  }
  const std::string* source = Attribute(*initialization, "sourceURL");
  if (source == nullptr) {
    return absl::OkStatus();
  }
  const std::string* range = Attribute(*initialization, "range");
  std::optional<ByteRange> byte_range;
  if (range != nullptr) {
    byte_range = ParseByteRange(*range);
  }
  if (range != nullptr && !byte_range.has_value()) {
    return InvalidManifest("invalid initialization range");
  }
  parameters.init_sections.push_back(
      InitSection{.uri = ResolveUri(base_urls.front(), *source),
                  .byte_range = byte_range,
                  .key_index = std::nullopt});
  return absl::OkStatus();
}

absl::StatusOr<std::vector<TimelineRun>> ReadSegmentListRuns(
    const Node& list, std::int64_t timescale, const Period& period,
    std::int64_t offset) {
  const Node* timeline = Child(list, "SegmentTimeline");
  if (timeline == nullptr) {
    return std::vector<TimelineRun>();
  }
  return ParseTimelineRuns(*timeline, timescale, period.duration, offset);
}

absl::StatusOr<std::pair<std::int64_t, std::int64_t>> NextSegmentPosition(
    const std::vector<TimelineRun>& runs, SegmentListCursor& cursor,
    std::int64_t duration) {
  if (runs.empty()) {
    const std::int64_t tick = cursor.next_tick;
    cursor.next_tick += duration;
    return std::pair<std::int64_t, std::int64_t>{tick, duration};
  }
  while (cursor.run_index < runs.size() &&
         cursor.run_offset >= runs[cursor.run_index].count) {
    ++cursor.run_index;
    cursor.run_offset = 0;
  }
  if (cursor.run_index >= runs.size()) {
    return InvalidManifest("SegmentList exceeds SegmentTimeline");
  }
  const TimelineRun& run = runs[cursor.run_index];
  const std::int64_t tick =
      run.start_ticks + (run.duration_ticks * cursor.run_offset);
  ++cursor.run_offset;
  return std::pair<std::int64_t, std::int64_t>{tick, run.duration_ticks};
}

absl::Status AppendSegmentUrl(const Node& segment_url,
                              const std::vector<std::string>& base_urls,
                              std::int64_t offset, std::int64_t timescale,
                              std::int64_t fallback_duration,
                              penumbra::MediaTime period_start,
                              const std::vector<TimelineRun>& runs,
                              SegmentListCursor& cursor, std::int64_t& sequence,
                              ExplicitSegmentIndexParameters& parameters) {
  auto position = NextSegmentPosition(runs, cursor, fallback_duration);
  if (!position.ok()) {
    return position.status();
  }
  const std::string* media = Attribute(segment_url, "media");
  const std::string* range = Attribute(segment_url, "mediaRange");
  if (media == nullptr || position->second <= 0 || base_urls.empty()) {
    return InvalidManifest("SegmentList URL or duration is missing");
  }
  const auto start_micros = TicksToMicros(position->first - offset, timescale);
  const auto duration_micros = TicksToMicros(position->second, timescale);
  if (!start_micros.has_value() || !duration_micros.has_value()) {
    return InvalidManifest("SegmentList time is out of range");
  }
  std::optional<ByteRange> byte_range;
  if (range != nullptr) {
    byte_range = ParseByteRange(*range);
    if (!byte_range.has_value()) {
      return InvalidManifest("invalid SegmentURL mediaRange");
    }
  }
  parameters.segments.push_back(Segment{
      .sequence_number = sequence++,
      .start =
          period_start + penumbra::TimeDelta::FromMicroseconds(*start_micros),
      .duration = penumbra::TimeDelta::FromMicroseconds(*duration_micros),
      .uri = ResolveUri(base_urls.front(), *media),
      .byte_range = byte_range,
      .init_index = std::nullopt,
      .key_index = std::nullopt,
      .discontinuity_sequence = 0,
      .gap = false,
      .bitrate_kbps = std::nullopt,
      .program_date_time = std::nullopt,
      .hls_parts = {}});
  return absl::OkStatus();
}

absl::StatusOr<SegmentIndexProvider> ParseSegmentList(
    const Node& mpd, const Node& period, const Node& adaptation,
    const Node& representation, const std::vector<std::string>& base_urls,
    const Period& model_period, TimelineMapping& mapping) {
  const auto list = InheritedSegmentElement(
      {&mpd, &period, &adaptation, &representation}, "SegmentList");
  if (!list.has_value()) {
    return InvalidManifest("missing SegmentList");
  }
  const std::int64_t timescale = Integer(*list, "timescale").value_or(1);
  const std::int64_t offset =
      Integer(*list, "presentationTimeOffset").value_or(0);
  const auto time_base = penumbra::Rational::Create(1, timescale);
  if (!time_base.has_value()) {
    return InvalidManifest("invalid SegmentList timescale");
  }
  ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = Integer(*list, "startNumber").value_or(1);
  std::int64_t sequence = parameters.first_sequence_number;
  const std::int64_t duration = Integer(*list, "duration").value_or(0);
  if (absl::Status status =
          ReadListInitialization(*list, base_urls, parameters);
      !status.ok()) {
    return status;
  }
  auto runs = ReadSegmentListRuns(*list, timescale, model_period, offset);
  if (!runs.ok()) {
    return runs.status();
  }
  SegmentListCursor cursor{.next_tick = offset};
  for (const Node* segment_url : Children(*list, "SegmentURL")) {
    if (absl::Status status = AppendSegmentUrl(
            *segment_url, base_urls, offset, timescale, duration,
            model_period.start, *runs, cursor, sequence, parameters);
        !status.ok()) {
      return status;
    }
  }
  const auto origin = penumbra::ToMediaTime(
      penumbra::Timestamp{.value = offset, .time_base = *time_base},
      penumbra::Rounding::kDown);
  if (!origin.has_value()) {
    return InvalidManifest("invalid SegmentList timeline mapping");
  }
  mapping = TimelineMapping{.media_origin = *origin,
                            .presentation_origin = model_period.start};
  auto index = ExplicitSegmentIndex::Create(std::move(parameters));
  if (!index.ok()) {
    return index.status();
  }
  return SegmentIndexProvider::Resolved(*index);
}

absl::Status ParseEventStreams(const Node& period_node, const Period& period,
                               std::vector<AdBreak>& ad_breaks) {
  const auto streams = Children(period_node, "EventStream");
  std::size_t stream_index = 0;
  for (const Node* stream : streams) {
    const std::int64_t timescale = Integer(*stream, "timescale").value_or(1);
    const std::int64_t offset =
        Integer(*stream, "presentationTimeOffset").value_or(0);
    const std::string* stream_id = Attribute(*stream, "id");
    if (timescale <= 0) {
      return InvalidManifest("EventStream timescale is invalid");
    }
    std::size_t event_index = 0;
    for (const Node* event : Children(*stream, "Event")) {
      const std::int64_t event_time =
          Integer(*event, "presentationTime").value_or(0);
      const auto start_micros = TicksToMicros(event_time - offset, timescale);
      if (!start_micros.has_value() || *start_micros < 0) {
        return InvalidManifest("EventStream presentationTime is invalid");
      }
      AdBreak ad_break;
      const std::string* id = Attribute(*event, "id");
      if (id != nullptr && !id->empty()) {
        ad_break.id = *id;
      } else {
        ad_break.id =
            "event-" +
            (stream_id == nullptr ? std::to_string(stream_index) : *stream_id) +
            "-" + std::to_string(event_index);
      }
      ad_break.start =
          period.start + penumbra::TimeDelta::FromMicroseconds(*start_micros);
      const auto duration_ticks = Integer(*event, "duration");
      if (duration_ticks.has_value()) {
        const auto duration_micros = TicksToMicros(*duration_ticks, timescale);
        if (!duration_micros.has_value()) {
          return InvalidManifest("EventStream duration is invalid");
        }
        ad_break.duration =
            penumbra::TimeDelta::FromMicroseconds(*duration_micros);
      }
      ad_breaks.push_back(std::move(ad_break));
      ++event_index;
    }
    ++stream_index;
  }
  return absl::OkStatus();
}

struct RootMetadata {
  std::optional<absl::Time> anchor;
  std::optional<penumbra::TimeDelta> duration;
  std::vector<std::string> base_urls;
};

void ReadProfiles(const Node& root, DashPresentationExtension& extension);
void ReadRootLocations(const Node& root, DashPresentationExtension& extension);
void ReadUtcTimings(const Node& root, DashPresentationExtension& extension);
absl::Status ReadCodecs(const Node& adaptation, const Node& representation,
                        Track& track);
absl::Status ReadTrackMetadata(const Node& adaptation,
                               const Node& representation,
                               DashTrackExtension& extension, Track& track);

absl::StatusOr<RootMetadata> ReadRootMetadata(const Node& root,
                                              Presentation& presentation) {
  RootMetadata metadata;
  auto availability = DateTimeAttribute(root, "availabilityStartTime");
  if (!availability.ok()) {
    return availability.status();
  }
  metadata.anchor = *availability;
  const std::string* type = Attribute(root, "type");
  presentation.type = type != nullptr && *type == "dynamic"
                          ? PresentationType::kDynamic
                          : PresentationType::kStatic;
  auto duration =
      DurationAttribute(root, "mediaPresentationDuration", metadata.anchor);
  if (!duration.ok()) {
    return duration.status();
  }
  metadata.duration = *duration;
  if (metadata.duration.has_value()) {
    presentation.duration = *metadata.duration;
  }
  auto min_buffer = DurationAttribute(root, "minBufferTime", metadata.anchor);
  auto max_segment =
      DurationAttribute(root, "maxSegmentDuration", metadata.anchor);
  if (!min_buffer.ok()) {
    return min_buffer.status();
  }
  if (!max_segment.ok()) {
    return max_segment.status();
  }
  DashPresentationExtension extension;
  extension.min_buffer_time = *min_buffer;
  extension.max_segment_duration = *max_segment;
  ReadProfiles(root, extension);
  ReadRootLocations(root, extension);
  ReadUtcTimings(root, extension);
  presentation.dash_extension = std::move(extension);
  metadata.base_urls = ResolveBaseUrls(root, {});
  return metadata;
}

void ReadProfiles(const Node& root, DashPresentationExtension& extension) {
  const std::string* profiles = Attribute(root, "profiles");
  if (profiles == nullptr) {
    return;
  }
  std::size_t begin = 0;
  while (begin < profiles->size()) {
    while (begin < profiles->size() && (*profiles)[begin] == ' ') {
      ++begin;
    }
    const std::size_t end = profiles->find(' ', begin);
    const std::size_t length =
        end == std::string::npos ? profiles->size() - begin : end - begin;
    if (length > 0) {
      extension.profiles.emplace_back(profiles->substr(begin, length));
    }
    begin += length + 1;
  }
}

void ReadRootLocations(const Node& root, DashPresentationExtension& extension) {
  for (const Node* location : Children(root, "Location")) {
    extension.locations.push_back(location->text);
  }
  for (const Node* location : Children(root, "PatchLocation")) {
    extension.patch_locations.push_back(location->text);
  }
}

void ReadUtcTimings(const Node& root, DashPresentationExtension& extension) {
  for (const Node* utc : Children(root, "UTCTiming")) {
    const std::string* scheme = Attribute(*utc, "schemeIdUri");
    const std::string* value = Attribute(*utc, "value");
    if (scheme != nullptr && value != nullptr) {
      extension.utc_timings.push_back(
          DashUtcTiming{.scheme_id_uri = *scheme, .value = *value});
    }
  }
}

penumbra::StreamType ParseStreamType(const Node& adaptation) {
  const std::string* content_type = Attribute(adaptation, "contentType");
  const std::string* mime = Attribute(adaptation, "mimeType");
  std::string_view media;
  if (content_type != nullptr) {
    media = *content_type;
  } else if (mime != nullptr) {
    media = std::string_view(*mime).substr(0, mime->find('/'));
  }
  if (media == "audio") {
    return penumbra::StreamType::kAudio;
  }
  if (media == "text" || media == "application") {
    return penumbra::StreamType::kText;
  }
  return penumbra::StreamType::kVideo;
}

absl::StatusOr<SegmentIndexProvider> ParseRepresentationIndex(
    const Node& mpd_node, const Node& period_node, const Node& adaptation_node,
    const Node& representation_node, const std::vector<std::string>& bases,
    const Period& period, const std::optional<penumbra::TimeDelta>& duration,
    std::string_view representation_id, std::int64_t bandwidth,
    TimelineMapping& mapping) {
  if (InheritedSegmentElement(
          {&mpd_node, &period_node, &adaptation_node, &representation_node},
          "SegmentTemplate")
          .has_value()) {
    return ParseTemplateIndex(
        mpd_node, period_node, adaptation_node, representation_node,
        bases.front(), representation_id, bandwidth, period, duration, mapping);
  }
  if (InheritedSegmentElement(
          {&mpd_node, &period_node, &adaptation_node, &representation_node},
          "SegmentList")
          .has_value()) {
    return ParseSegmentList(mpd_node, period_node, adaptation_node,
                            representation_node, bases, period, mapping);
  }
  return ParseSegmentBase(mpd_node, period_node, adaptation_node,
                          representation_node, bases.front());
}

absl::StatusOr<std::shared_ptr<const Track>> ParseRepresentation(
    const Node& mpd_node, const Node& period_node, const Node& adaptation_node,
    const Node& representation_node, const Period& period,
    const std::optional<penumbra::TimeDelta>& period_duration,
    const std::vector<std::string>& adaptation_bases, SwitchingSet& set) {
  if (HasUnknownEssentialProperty(representation_node)) {
    ALUMBRA_LOG(kManifest, kWarning)
        << "DASH Representation excluded for unknown EssentialProperty";
    return std::shared_ptr<const Track>();
  }
  const std::string* adaptation_id = Attribute(adaptation_node, "id");
  const std::string* representation_id = Attribute(representation_node, "id");
  if (adaptation_id == nullptr || representation_id == nullptr) {
    return InvalidManifest("Representation requires id");
  }
  const std::vector<std::string> bases =
      ResolveBaseUrls(representation_node, adaptation_bases);
  Track track;
  track.id = DashTrackId(*adaptation_id, *representation_id);
  track.bandwidth_bps =
      Integer(representation_node, "bandwidth")
          .value_or(Integer(adaptation_node, "bandwidth").value_or(0));
  DashTrackExtension extension;
  extension.base_urls = bases;
  ReadTrackFormat(adaptation_node, representation_node, set, track);
  ReadRepresentationDescriptors(representation_node, extension);
  ReadProtection(representation_node, set);
  if (absl::Status status = ReadTrackMetadata(
          adaptation_node, representation_node, extension, track);
      !status.ok()) {
    return status;
  }
  track.timeline.presentation_origin = period.start;
  auto segments = ParseRepresentationIndex(
      mpd_node, period_node, adaptation_node, representation_node, bases,
      period, period_duration, *representation_id,
      track.bandwidth_bps.value_or(0), track.timeline);
  if (!segments.ok()) {
    return segments.status();
  }
  track.segments = std::move(*segments);
  track.extension = std::move(extension);
  return Freeze(std::move(track));
}

absl::Status ReadTrackMetadata(const Node& adaptation,
                               const Node& representation,
                               DashTrackExtension& extension, Track& track) {
  const auto quality = Integer(representation, "qualityRanking");
  if (quality.has_value() &&
      *quality >= std::numeric_limits<std::int32_t>::min() &&
      *quality <= std::numeric_limits<std::int32_t>::max()) {
    extension.quality_ranking = static_cast<std::int32_t>(*quality);
  }
  const std::string* mime = Attribute(representation, "mimeType");
  if (mime == nullptr) {
    mime = Attribute(adaptation, "mimeType");
  }
  if (mime != nullptr) {
    extension.mime_type = *mime;
  }
  return ReadCodecs(adaptation, representation, track);
}

absl::Status ReadCodecs(const Node& adaptation, const Node& representation,
                        Track& track) {
  const std::string* codecs = Attribute(representation, "codecs");
  if (codecs == nullptr) {
    codecs = Attribute(adaptation, "codecs");
  }
  if (codecs == nullptr) {
    return absl::OkStatus();
  }
  auto parsed = codec::ParseCodecList(*codecs);
  if (!parsed.ok()) {
    return InvalidManifest("invalid RFC 6381 codec list");
  }
  track.codecs = std::move(*parsed);
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<const SwitchingSet>> ParseAdaptationSet(
    const Node& mpd_node, const Node& period_node, const Node& adaptation_node,
    const Period& period,
    const std::optional<penumbra::TimeDelta>& period_duration,
    const std::vector<std::string>& period_bases) {
  if (HasUnknownEssentialProperty(adaptation_node)) {
    ALUMBRA_LOG(kManifest, kWarning)
        << "DASH AdaptationSet excluded for unknown EssentialProperty";
    return std::shared_ptr<const SwitchingSet>();
  }
  const std::string* id = Attribute(adaptation_node, "id");
  if (id == nullptr) {
    return InvalidManifest("AdaptationSet requires id");
  }
  const std::vector<std::string> bases =
      ResolveBaseUrls(adaptation_node, period_bases);
  SwitchingSet set;
  set.id = DashSwitchingSetId(*id);
  set.type = ParseStreamType(adaptation_node);
  if (const std::string* lang = Attribute(adaptation_node, "lang")) {
    set.language = *lang;
  }
  ReadDescriptors(adaptation_node, set);
  ReadProtection(adaptation_node, set);
  for (const Node* representation :
       Children(adaptation_node, "Representation")) {
    auto track = ParseRepresentation(mpd_node, period_node, adaptation_node,
                                     *representation, period, period_duration,
                                     bases, set);
    if (!track.ok()) {
      return track.status();
    }
    if (*track != nullptr) {
      set.tracks.push_back(std::move(*track));
    }
  }
  if (set.tracks.empty()) {
    return std::shared_ptr<const SwitchingSet>();
  }
  return Freeze(std::move(set));
}

absl::StatusOr<Period> ParsePeriod(
    const Node& mpd_node, const Node& period_node, std::size_t period_index,
    penumbra::TimeDelta period_start,
    std::optional<penumbra::TimeDelta> period_duration,
    const std::vector<std::string>& root_bases, Presentation& presentation) {
  Period period;
  const std::string* id = Attribute(period_node, "id");
  period.id =
      id == nullptr ? DashPeriodIdByIndex(period_index) : DashPeriodId(*id);
  period.start =
      penumbra::MediaTime::FromMicroseconds(period_start.InMicroseconds());
  period.duration = period_duration.value_or(penumbra::TimeDelta::Unknown());
  const std::vector<std::string> bases =
      ResolveBaseUrls(period_node, root_bases);
  for (const Node* adaptation : Children(period_node, "AdaptationSet")) {
    auto set = ParseAdaptationSet(mpd_node, period_node, *adaptation, period,
                                  period_duration, bases);
    if (!set.ok()) {
      return set.status();
    }
    if (*set != nullptr) {
      period.switching_sets.push_back(std::move(*set));
    }
  }
  if (absl::Status status =
          ParseEventStreams(period_node, period, presentation.ad_breaks);
      !status.ok()) {
    return status;
  }
  return period;
}

absl::Status ReadPeriods(const Node& root, const RootMetadata& metadata,
                         Presentation& presentation) {
  const auto periods = Children(root, "Period");
  std::vector<std::optional<penumbra::TimeDelta>> explicit_starts;
  explicit_starts.reserve(periods.size());
  for (const Node* period : periods) {
    auto start = DurationAttribute(*period, "start", metadata.anchor);
    if (!start.ok()) {
      return start.status();
    }
    explicit_starts.push_back(*start);
  }

  auto next_start = penumbra::TimeDelta::Zero();
  for (std::size_t i = 0; i < periods.size(); ++i) {
    const penumbra::TimeDelta period_start =
        explicit_starts[i].value_or(next_start);
    const absl::Time base_anchor = metadata.anchor.value_or(absl::UnixEpoch());
    const absl::Time period_anchor =
        base_anchor + absl::Microseconds(period_start.InMicroseconds());
    auto duration = DurationAttribute(*periods[i], "duration", period_anchor);
    if (!duration.ok()) {
      return duration.status();
    }
    std::optional<penumbra::TimeDelta> period_duration = *duration;
    if (!period_duration.has_value() && i + 1 < periods.size() &&
        explicit_starts[i + 1].has_value()) {
      const penumbra::TimeDelta inferred =
          *explicit_starts[i + 1] - period_start;
      if (inferred <= penumbra::TimeDelta::Zero()) {
        return InvalidManifest("Period starts must be increasing");
      }
      period_duration = inferred;
    }
    if (!period_duration.has_value() && i + 1 == periods.size() &&
        metadata.duration.has_value()) {
      const std::int64_t remaining =
          metadata.duration->InMicroseconds() - period_start.InMicroseconds();
      if (remaining < 0) {
        return InvalidManifest(
            "last Period starts beyond presentation duration");
      }
      period_duration = penumbra::TimeDelta::FromMicroseconds(remaining);
    }
    auto period =
        ParsePeriod(root, *periods[i], i, period_start, period_duration,
                    metadata.base_urls, presentation);
    if (!period.ok()) {
      return period.status();
    }
    if (period->duration.is_finite()) {
      next_start = penumbra::TimeDelta::FromMicroseconds(
          period->start.InMicroseconds() + period->duration.InMicroseconds());
    }
    presentation.periods.push_back(Freeze(std::move(*period)));
  }
  return absl::OkStatus();
}

absl::Status ReadLiveInfo(const Node& root, const RootMetadata& metadata,
                          Presentation& presentation) {
  if (presentation.type != PresentationType::kDynamic) {
    return absl::OkStatus();
  }
  presentation.duration = penumbra::TimeDelta::Unknown();
  LiveInfo live;
  live.window_start = penumbra::MediaTime::Zero();
  live.window_end = presentation.periods.empty()
                        ? penumbra::MediaTime::Zero()
                        : presentation.periods.back()->start;
  auto shift = DurationAttribute(root, "timeShiftBufferDepth", metadata.anchor);
  auto delay =
      DurationAttribute(root, "suggestedPresentationDelay", metadata.anchor);
  auto update = DurationAttribute(root, "minimumUpdatePeriod", metadata.anchor);
  auto publish_time = DateTimeAttribute(root, "publishTime");
  if (!shift.ok()) {
    return shift.status();
  }
  if (!delay.ok()) {
    return delay.status();
  }
  if (!update.ok()) {
    return update.status();
  }
  if (!publish_time.ok()) {
    return publish_time.status();
  }
  live.time_shift_depth = *shift;
  live.suggested_presentation_delay = *delay;
  live.minimum_update_period = *update;
  if (presentation.dash_extension.has_value()) {
    live.target_segment_duration =
        presentation.dash_extension->max_segment_duration;
  }
  live.availability_start_time = metadata.anchor;
  live.publish_time = *publish_time;
  presentation.live = live;
  return absl::OkStatus();
}

}

absl::StatusOr<Presentation> ParseDashMpd(std::string_view xml,
                                          std::size_t max_bytes) {
  if (xml.size() > max_bytes) {
    return InvalidManifest("MPD exceeds configured byte limit");
  }
  XmlReader reader(xml);
  auto root = reader.Read();
  if (!root.ok()) {
    return root.status();
  }
  if (root->name != "MPD") {
    return InvalidManifest("root element is not MPD");
  }
  Presentation presentation;
  presentation.protocol = Protocol::kDash;
  auto metadata = ReadRootMetadata(*root, presentation);
  if (!metadata.ok()) {
    return metadata.status();
  }
  if (absl::Status status = ReadPeriods(*root, *metadata, presentation);
      !status.ok()) {
    return status;
  }
  if (absl::Status status = ReadLiveInfo(*root, *metadata, presentation);
      !status.ok()) {
    return status;
  }
  auto built = BuildPresentation(std::move(presentation));
  if (!built.ok()) {
    return built.status();
  }
  return **built;
}

}
