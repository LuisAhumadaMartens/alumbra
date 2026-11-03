#include "umbra/ads/vast_parser.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "umbra/settings/setting_types.h"
#include "umbra/settings/settings_keys.h"

namespace umbra::ads {
namespace {

struct XmlNode {
  std::string name;
  std::string text;
  std::map<std::string, std::string, std::less<>> attributes;
  std::vector<XmlNode> children;
  std::string raw_xml;
};

void TrimText(std::string& value);

absl::Status Error(VastErrorCode code, std::string_view detail) {
  return absl::InvalidArgumentError(
      absl::StrCat("VAST-", static_cast<int>(code), ": ", detail));
}

std::int64_t SettingDefault(std::size_t index) {
  const settings::Registry registry = settings::GetRegistry();
  return registry.settings[index].default_integer;
}

VastParserOptions ParserOptions(VastParserOptions options) {
  if (options.max_response_bytes == 0) {
    options.max_response_bytes = static_cast<std::size_t>(
        SettingDefault(settings::kAdsVastMaxResponseBytes.Index()));
  }
  return options;
}

VastResolverOptions ResolverOptions(VastResolverOptions options) {
  if (options.max_wrapper_depth == 0) {
    options.max_wrapper_depth = static_cast<std::size_t>(
        SettingDefault(settings::kAdsVastMaxWrapperDepth.Index()));
  }
  if (options.max_response_bytes == 0) {
    options.max_response_bytes = static_cast<std::size_t>(
        SettingDefault(settings::kAdsVastMaxResponseBytes.Index()));
  }
  if (options.request_timeout_ms == 0) {
    options.request_timeout_ms = static_cast<std::uint32_t>(
        SettingDefault(settings::kAdsVastRequestTimeoutMs.Index()));
  }
  return options;
}

std::string_view LocalName(std::string_view name) {
  const std::size_t colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

class XmlReader {
 public:
  XmlReader(std::string_view input, VastParserOptions options)
      : input_(input), options_(options) {}

  absl::StatusOr<XmlNode> Read() {
    SkipSpace();
    if (input_.substr(position_, 3) == "\xef\xbb\xbf") {
      position_ += 3;
    }
    SkipSpace();
    if (input_.substr(position_, 5) == "<?xml") {
      const std::size_t end = input_.find("?>", position_ + 5);
      if (end == std::string_view::npos) {
        return Error(VastErrorCode::kXmlParse, "invalid XML declaration");
      }
      position_ = end + 2;
    }
    if (absl::Status status = SkipMisc(); !status.ok()) {
      return status;
    }
    if (input_.substr(position_, 9) == "<!DOCTYPE" ||
        input_.substr(position_, 8) == "<!ENTITY") {
      return Error(VastErrorCode::kXmlParse,
                   "DTD and entity declarations are disabled");
    }
    auto root = ReadNode(0);
    if (!root.ok()) {
      return root.status();
    }
    if (absl::Status status = SkipMisc(); !status.ok()) {
      return status;
    }
    if (position_ != input_.size()) {
      return Error(VastErrorCode::kXmlParse, "trailing XML data");
    }
    return root;
  }

 private:
  void SkipSpace() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t')) {
      ++position_;
    }
  }

  absl::Status SkipMisc() {
    while (true) {
      SkipSpace();
      if (input_.substr(position_, 4) == "<!--") {
        const std::size_t end = input_.find("-->", position_ + 4);
        if (end == std::string_view::npos) {
          return Error(VastErrorCode::kXmlParse, "unterminated XML comment");
        }
        position_ = end + 3;
      } else if (input_.substr(position_, 2) == "<?") {
        const std::size_t end = input_.find("?>", position_ + 2);
        if (end == std::string_view::npos) {
          return Error(VastErrorCode::kXmlParse,
                       "unterminated processing instruction");
        }
        position_ = end + 2;
      } else {
        return absl::OkStatus();
      }
    }
  }

  absl::StatusOr<std::string> Name() {
    const std::size_t start = position_;
    while (position_ < input_.size()) {
      const char value = input_[position_];
      if ((value < 'a' || value > 'z') && (value < 'A' || value > 'Z') &&
          (value < '0' || value > '9') && value != ':' && value != '_' &&
          value != '-' && value != '.') {
        break;
      }
      ++position_;
    }
    if (start == position_) {
      return Error(VastErrorCode::kXmlParse, "invalid XML name");
    }
    return std::string(input_.substr(start, position_ - start));
  }

  absl::StatusOr<std::string> Value() {
    if (position_ == input_.size() ||
        (input_[position_] != '\'' && input_[position_] != '"')) {
      return Error(VastErrorCode::kXmlParse, "invalid XML attribute");
    }
    const char quote = input_[position_++];
    std::string result;
    while (position_ < input_.size() && input_[position_] != quote) {
      if (input_[position_] == '<') {
        return Error(VastErrorCode::kXmlParse, "invalid XML attribute value");
      }
      if (input_[position_] == '&') {
        auto entity = Entity();
        if (!entity.ok()) {
          return entity.status();
        }
        result.append(*entity);
      } else {
        result.push_back(input_[position_++]);
      }
    }
    if (position_ == input_.size()) {
      return Error(VastErrorCode::kXmlParse, "unterminated XML attribute");
    }
    ++position_;
    return result;
  }

  absl::StatusOr<std::string> Entity() {
    const std::size_t end = input_.find(';', position_ + 1);
    if (end == std::string_view::npos) {
      return Error(VastErrorCode::kXmlParse, "invalid XML entity reference");
    }
    const std::string_view value =
        input_.substr(position_, end - position_ + 1);
    position_ = end + 1;
    if (value == "&amp;") {
      return std::string("&");
    }
    if (value == "&lt;") {
      return std::string("<");
    }
    if (value == "&gt;") {
      return std::string(">");
    }
    if (value == "&quot;") {
      return std::string("\"");
    }
    if (value == "&apos;") {
      return std::string("'");
    }
    if (value.size() > 3 && value[1] == '#') {
      const bool hexadecimal = value[2] == 'x' || value[2] == 'X';
      const std::string_view digits = value.substr(
          hexadecimal ? 3 : 2, value.size() - (hexadecimal ? 4 : 3));
      std::uint32_t code_point = 0;
      const bool parsed = hexadecimal ? absl::SimpleHexAtoi(digits, &code_point)
                                      : absl::SimpleAtoi(digits, &code_point);
      if (digits.empty() || !parsed || code_point == 0 ||
          code_point > 0x10ffff ||
          (code_point >= 0xd800 && code_point <= 0xdfff)) {
        return Error(VastErrorCode::kXmlParse,
                     "invalid numeric XML entity reference");
      }
      std::string decoded;
      if (code_point <= 0x7f) {
        decoded.push_back(static_cast<char>(code_point));
      } else if (code_point <= 0x7ff) {
        decoded.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        decoded.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
      } else if (code_point <= 0xffff) {
        decoded.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        decoded.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        decoded.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
      } else {
        decoded.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        decoded.push_back(
            static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        decoded.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        decoded.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
      }
      return decoded;
    }
    return Error(VastErrorCode::kXmlParse, "unsupported XML entity reference");
  }

  absl::StatusOr<XmlNode> ReadNode(std::size_t depth) {
    if (depth > options_.max_element_depth ||
        ++nodes_ > options_.max_elements) {
      return Error(VastErrorCode::kXmlParse, "XML structure exceeds limits");
    }
    const std::size_t start = position_;
    if (position_ == input_.size()) {
      return Error(VastErrorCode::kXmlParse, "expected XML element");
    }
    const char opening = input_[position_];
    ++position_;
    if (opening != '<' || position_ == input_.size() ||
        input_[position_] == '/' || input_[position_] == '!' ||
        input_[position_] == '?') {
      return Error(VastErrorCode::kXmlParse, "expected XML element");
    }
    auto full_name = Name();
    if (!full_name.ok()) {
      return full_name.status();
    }
    XmlNode node;
    node.name = std::string(LocalName(*full_name));
    auto empty = ReadAttributes(node, start);
    if (!empty.ok()) {
      return empty.status();
    }
    if (*empty) {
      return node;
    }
    absl::Status status = ReadContent(node, depth, start);
    if (!status.ok()) {
      return status;
    }
    return node;
  }

  absl::StatusOr<bool> ReadAttributes(XmlNode& node, std::size_t start) {
    while (true) {
      SkipSpace();
      if (input_.substr(position_, 2) == "/>") {
        position_ += 2;
        node.raw_xml = std::string(input_.substr(start, position_ - start));
        return true;
      }
      if (position_ < input_.size() && input_[position_] == '>') {
        ++position_;
        return false;
      }
      auto key = Name();
      if (!key.ok()) {
        return key.status();
      }
      SkipSpace();
      if (position_ == input_.size()) {
        return Error(VastErrorCode::kXmlParse,
                     "expected XML attribute assignment");
      }
      const char assignment = input_[position_];
      ++position_;
      if (assignment != '=') {
        return Error(VastErrorCode::kXmlParse,
                     "expected XML attribute assignment");
      }
      SkipSpace();
      auto value = Value();
      if (!value.ok()) {
        return value.status();
      }
      if (!node.attributes
               .emplace(std::string(LocalName(*key)), std::move(*value))
               .second) {
        return Error(VastErrorCode::kXmlParse, "duplicate XML attribute");
      }
    }
  }

  absl::Status ReadContent(XmlNode& node, std::size_t depth,
                           std::size_t start) {
    while (true) {
      SkipSpace();
      if (input_.substr(position_, 4) == "<!--") {
        absl::Status status = SkipMisc();
        if (!status.ok()) {
          return status;
        }
        continue;
      }
      if (input_.substr(position_, 9) == "<![CDATA[") {
        absl::Status status = ReadCdata(node);
        if (!status.ok()) {
          return status;
        }
        continue;
      }
      if (input_.substr(position_, 2) == "</") {
        return ReadClosing(node, start);
      }
      if (position_ == input_.size()) {
        return Error(VastErrorCode::kXmlParse, "unterminated XML element");
      }
      if (input_[position_] == '<') {
        auto child = ReadNode(depth + 1);
        if (!child.ok()) {
          return child.status();
        }
        node.children.push_back(std::move(*child));
        continue;
      }
      absl::Status status = ReadText(node);
      if (!status.ok()) {
        return status;
      }
    }
  }

  absl::Status ReadCdata(XmlNode& node) {
    const std::size_t end = input_.find("]]>", position_ + 9);
    if (end == std::string_view::npos) {
      return Error(VastErrorCode::kXmlParse, "unterminated CDATA section");
    }
    node.text.append(input_.substr(position_ + 9, end - position_ - 9));
    position_ = end + 3;
    return absl::OkStatus();
  }

  absl::Status ReadClosing(XmlNode& node, std::size_t start) {
    position_ += 2;
    auto close_name = Name();
    if (!close_name.ok() || LocalName(*close_name) != node.name) {
      return Error(VastErrorCode::kXmlParse, "mismatched XML closing element");
    }
    SkipSpace();
    if (position_ == input_.size()) {
      return Error(VastErrorCode::kXmlParse, "invalid XML closing element");
    }
    const char close_character = input_[position_];
    ++position_;
    if (close_character != '>') {
      return Error(VastErrorCode::kXmlParse, "invalid XML closing element");
    }
    TrimText(node.text);
    node.raw_xml = std::string(input_.substr(start, position_ - start));
    return absl::OkStatus();
  }

  absl::Status ReadText(XmlNode& node) {
    const std::size_t end = input_.find('<', position_);
    if (end == std::string_view::npos) {
      return Error(VastErrorCode::kXmlParse, "unterminated XML text");
    }
    while (position_ < end) {
      if (input_[position_] == '&') {
        auto entity = Entity();
        if (!entity.ok()) {
          return entity.status();
        }
        node.text.append(*entity);
      } else {
        node.text.push_back(input_[position_]);
        ++position_;
      }
    }
    return absl::OkStatus();
  }

  std::string_view input_;
  VastParserOptions options_;
  std::size_t position_ = 0;
  std::size_t nodes_ = 0;
};

const XmlNode* Child(const XmlNode& node, std::string_view name) {
  for (const XmlNode& child : node.children) {
    if (child.name == name) {
      return &child;
    }
  }
  return nullptr;
}

std::vector<const XmlNode*> Children(const XmlNode& node,
                                     std::string_view name) {
  std::vector<const XmlNode*> result;
  for (const XmlNode& child : node.children) {
    if (child.name == name) {
      result.push_back(&child);
    }
  }
  return result;
}

std::string Attribute(const XmlNode& node, std::string_view key) {
  const auto found = node.attributes.find(key);
  return found == node.attributes.end() ? std::string() : found->second;
}

std::optional<std::uint64_t> Number(std::string_view value) {
  std::uint64_t result = 0;
  if (!absl::SimpleAtoi(value, &result)) {
    return std::nullopt;
  }
  return result;
}

std::optional<bool> BooleanAttribute(const XmlNode& node,
                                     std::string_view key) {
  const std::string value = Attribute(node, key);
  if (value == "true" || value == "1") {
    return true;
  }
  if (value == "false" || value == "0") {
    return false;
  }
  return std::nullopt;
}

void TrimText(std::string& value) {
  const auto first = value.find_first_not_of(" \n\r\t");
  if (first == std::string::npos) {
    value.clear();
    return;
  }
  const auto last = value.find_last_not_of(" \n\r\t");
  value = value.substr(first, last - first + 1);
}

void ReadUrls(const XmlNode& node, std::string_view element,
              std::vector<std::string>& output) {
  for (const XmlNode* child : Children(node, element)) {
    if (!child->text.empty()) {
      output.push_back(child->text);
    }
  }
}

void ParseVerifications(const XmlNode& content, VastAd& ad) {
  const XmlNode* verifications = Child(content, "AdVerifications");
  if (verifications == nullptr) {
    return;
  }
  for (const XmlNode* verification : Children(*verifications, "Verification")) {
    VastVerification item;
    item.vendor = Attribute(*verification, "vendor");
    if (const XmlNode* resource =
            Child(*verification, "VerificationParameters")) {
      item.parameters.push_back(resource->text);
    }
    for (const XmlNode* resource :
         Children(*verification, "JavaScriptResource")) {
      item.java_script_resource = resource->text;
    }
    for (const XmlNode* resource :
         Children(*verification, "ExecutableResource")) {
      item.resource = resource->text;
    }
    for (const XmlNode* event : Children(*verification, "Tracking")) {
      if (!event->text.empty()) {
        item.tracking_events.push_back(event->text);
      }
    }
    ad.verifications.push_back(std::move(item));
  }
}

void ParseExtensions(const XmlNode& content, VastAd& ad) {
  const XmlNode* extensions = Child(content, "Extensions");
  if (extensions == nullptr) {
    return;
  }
  for (const XmlNode& extension : extensions->children) {
    ad.extensions_xml.push_back(extension.raw_xml);
  }
}

void ParseUniversalIds(const XmlNode& content, VastAd& ad) {
  const XmlNode* creatives = Child(content, "Creatives");
  if (creatives == nullptr) {
    return;
  }
  for (const XmlNode* creative : Children(*creatives, "Creative")) {
    const XmlNode* universal_id = Child(*creative, "UniversalAdId");
    if (universal_id == nullptr) {
      continue;
    }
    const std::string id = universal_id->text.empty()
                               ? Attribute(*universal_id, "idValue")
                               : universal_id->text;
    std::string registry = Attribute(*universal_id, "idRegistry");
    if (registry.empty()) {
      registry = "unknown";
    }
    ad.universal_ad_ids.push_back({id, registry});
  }
}

void ParseMediaFiles(const XmlNode& linear, VastCreative& creative) {
  const XmlNode* media_files = Child(linear, "MediaFiles");
  if (media_files == nullptr) {
    return;
  }
  for (const XmlNode* media : Children(*media_files, "MediaFile")) {
    VastMediaFile file;
    file.url = media->text;
    file.delivery = Attribute(*media, "delivery");
    file.type = Attribute(*media, "type");
    file.codec = Attribute(*media, "codec");
    file.bitrate_bps = Number(Attribute(*media, "bitrate"));
    file.width = Number(Attribute(*media, "width"));
    file.height = Number(Attribute(*media, "height"));
    file.api_framework = Attribute(*media, "apiFramework");
    file.scalable = BooleanAttribute(*media, "scalable").value_or(false);
    file.maintain_aspect_ratio =
        BooleanAttribute(*media, "maintainAspectRatio").value_or(false);
    creative.media_files.push_back(std::move(file));
  }
  for (const XmlNode* media : Children(*media_files, "Mezzanine")) {
    VastMediaFile file;
    file.url = media->text;
    file.delivery = Attribute(*media, "delivery");
    file.type = Attribute(*media, "type");
    file.codec = Attribute(*media, "codec");
    file.width = Number(Attribute(*media, "width"));
    file.height = Number(Attribute(*media, "height"));
    file.mezzanine = true;
    creative.media_files.push_back(std::move(file));
  }
}

void ParseCreativeTracking(const XmlNode& linear, VastCreative& creative) {
  const XmlNode* tracking = Child(linear, "TrackingEvents");
  if (tracking != nullptr) {
    for (const XmlNode* event : Children(*tracking, "Tracking")) {
      creative.tracking_events.push_back(
          {Attribute(*event, "event"), event->text});
    }
  }
  const XmlNode* video_clicks = Child(linear, "VideoClicks");
  if (video_clicks != nullptr) {
    ReadUrls(*video_clicks, "ClickThrough", creative.click_through_urls);
    ReadUrls(*video_clicks, "ClickTracking", creative.click_tracking_urls);
  }
}

void ParseCreatives(const XmlNode& content, VastAd& ad) {
  const XmlNode* creatives = Child(content, "Creatives");
  if (creatives == nullptr) {
    return;
  }
  for (const XmlNode* creative_node : Children(*creatives, "Creative")) {
    const XmlNode* linear = Child(*creative_node, "Linear");
    if (linear == nullptr) {
      continue;
    }
    VastCreative creative;
    creative.id = Attribute(*creative_node, "id");
    creative.ad_id = Attribute(*creative_node, "adId");
    creative.sequence = Number(Attribute(*creative_node, "sequence"));
    creative.skip_offset = Attribute(*linear, "skipoffset");
    ParseMediaFiles(*linear, creative);
    ParseCreativeTracking(*linear, creative);
    if (creative.ad_id.empty()) {
      creative.ad_id = ad.id;
    }
    ad.creatives.push_back(std::move(creative));
  }
}

absl::StatusOr<VastAd> ParseAd(const XmlNode& node) {
  VastAd ad;
  ad.id = Attribute(node, "id");
  ad.sequence = Number(Attribute(node, "sequence"));
  ad.ad_type = Attribute(node, "adType");
  const XmlNode* content = Child(node, "InLine");
  if (content == nullptr) {
    content = Child(node, "Wrapper");
    ad.is_wrapper = content != nullptr;
  }
  if (content == nullptr) {
    return Error(VastErrorCode::kSchema, "Ad has no InLine or Wrapper");
  }
  if (ad.is_wrapper) {
    ad.follow_additional_wrappers =
        BooleanAttribute(*content, "followAdditionalWrappers").value_or(true);
    ad.allow_multiple_ads =
        BooleanAttribute(*content, "allowMultipleAds").value_or(false);
    ad.fallback_on_no_ad =
        BooleanAttribute(*content, "fallbackOnNoAd").value_or(false);
    const XmlNode* uri = Child(*content, "VASTAdTagURI");
    if (uri == nullptr || uri->text.empty()) {
      return Error(VastErrorCode::kSchema, "Wrapper has no VASTAdTagURI");
    }
    ad.wrapper_uri = uri->text;
  }
  ReadUrls(*content, "Impression", ad.impressions);
  ReadUrls(*content, "Error", ad.error_urls);
  ParseVerifications(*content, ad);
  ParseExtensions(*content, ad);
  ParseUniversalIds(*content, ad);
  ParseCreatives(*content, ad);
  return ad;
}

void Merge(VastAd& target, const VastAd& wrapper) {
  target.impressions.insert(target.impressions.begin(),
                            wrapper.impressions.begin(),
                            wrapper.impressions.end());
  target.error_urls.insert(target.error_urls.begin(),
                           wrapper.error_urls.begin(),
                           wrapper.error_urls.end());
  for (VastCreative& creative : target.creatives) {
    for (const VastCreative& outer : wrapper.creatives) {
      creative.tracking_events.insert(creative.tracking_events.begin(),
                                      outer.tracking_events.begin(),
                                      outer.tracking_events.end());
      creative.click_through_urls.insert(creative.click_through_urls.begin(),
                                         outer.click_through_urls.begin(),
                                         outer.click_through_urls.end());
      creative.click_tracking_urls.insert(creative.click_tracking_urls.begin(),
                                          outer.click_tracking_urls.begin(),
                                          outer.click_tracking_urls.end());
    }
  }
  target.verifications.insert(target.verifications.begin(),
                              wrapper.verifications.begin(),
                              wrapper.verifications.end());
  target.extensions_xml.insert(target.extensions_xml.begin(),
                               wrapper.extensions_xml.begin(),
                               wrapper.extensions_xml.end());
}

void MergeWrapperChain(std::vector<VastAd>& ads,
                       const std::vector<VastAd>& wrapper_chain) {
  for (VastAd& ad : ads) {
    for (const VastAd& wrapper : std::ranges::reverse_view(wrapper_chain)) {
      Merge(ad, wrapper);
    }
  }
}

absl::StatusOr<VastDocument> FetchWrapperDocument(
    const VastAd& wrapper, const VastFetch& fetch,
    const VastResolverOptions& options,
    const VastParserOptions& parser_options) {
  auto response = fetch(wrapper.wrapper_uri, options.request_timeout_ms);
  if (!response.ok()) {
    return Error(VastErrorCode::kWrapperTimeout, response.status().message());
  }
  if (response->size() > options.max_response_bytes) {
    return Error(VastErrorCode::kXmlParse, "response exceeds byte limit");
  }
  auto next = ParseVast(*response, parser_options);
  if (!next.ok()) {
    return next.status();
  }
  if (next->ads.empty()) {
    const std::string_view detail = wrapper.fallback_on_no_ad
                                        ? "no ads after wrapper resolution"
                                        : "wrapper response contains no ads";
    return Error(VastErrorCode::kNoAds, detail);
  }
  return next;
}

}

absl::StatusOr<VastDocument> ParseVast(std::string_view xml,
                                       VastParserOptions options) {
  options = ParserOptions(options);
  if (xml.size() > options.max_response_bytes) {
    return Error(VastErrorCode::kXmlParse, "response exceeds byte limit");
  }
  XmlReader reader(xml, options);
  auto root = reader.Read();
  if (!root.ok()) {
    return root.status();
  }
  if (root->name != "VAST") {
    return Error(VastErrorCode::kSchema, "root element is not VAST");
  }
  VastDocument document;
  document.version = Attribute(*root, "version");
  if (document.version.empty()) {
    return Error(VastErrorCode::kSchema, "VAST version is missing");
  }
  const bool legacy_version =
      document.version == "2.0" || document.version == "3.0";
  bool version_four = document.version.starts_with("4.");
  if (version_four) {
    bool previous_dot = true;
    for (std::size_t index = 2; index < document.version.size(); ++index) {
      const char value = document.version[index];
      if (value == '.') {
        if (previous_dot) {
          version_four = false;
        }
        previous_dot = true;
      } else if (value >= '0' && value <= '9') {
        previous_dot = false;
      } else {
        version_four = false;
      }
    }
    if (previous_dot) {
      version_four = false;
    }
  }
  if (!legacy_version && !version_four) {
    return Error(VastErrorCode::kUnsupportedVersion,
                 "VAST version is not supported");
  }
  for (const XmlNode* node : Children(*root, "Ad")) {
    auto ad = ParseAd(*node);
    if (!ad.ok()) {
      return ad.status();
    }
    document.ads.push_back(std::move(*ad));
  }
  return document;
}

absl::StatusOr<VastDocument> ResolveVastWrappers(std::string_view xml,
                                                 const VastFetch& fetch,
                                                 VastResolverOptions options) {
  options = ResolverOptions(options);
  if (!fetch || options.max_wrapper_depth == 0 ||
      options.max_wrapper_depth > 10 || options.request_timeout_ms < 1000 ||
      options.request_timeout_ms > 30000) {
    return Error(VastErrorCode::kSchema, "invalid wrapper resolver options");
  }
  VastParserOptions parser_options;
  parser_options.max_response_bytes = options.max_response_bytes;
  auto document = ParseVast(xml, parser_options);
  if (!document.ok()) {
    return document.status();
  }
  std::set<std::string, std::less<>> visited;
  std::vector<VastAd> wrapper_chain;
  std::size_t depth = 0;
  while (true) {
    std::vector<VastAd> inline_ads;
    std::vector<VastAd> wrappers;
    for (VastAd& ad : document->ads) {
      (ad.is_wrapper ? wrappers : inline_ads).push_back(std::move(ad));
    }
    if (wrappers.empty()) {
      if (inline_ads.empty()) {
        return Error(VastErrorCode::kNoAds, "no ads after wrapper resolution");
      }
      MergeWrapperChain(inline_ads, wrapper_chain);
      document->ads = std::move(inline_ads);
      return document;
    }
    if (wrappers.size() != 1 || !inline_ads.empty()) {
      return Error(VastErrorCode::kSchema,
                   "wrapper response must contain one wrapper ad");
    }
    VastAd const wrapper = std::move(wrappers.front());
    if (depth >= options.max_wrapper_depth) {
      return Error(VastErrorCode::kWrapperLimit,
                   "wrapper depth limit exceeded");
    }
    if (!visited.insert(wrapper.wrapper_uri).second) {
      return Error(VastErrorCode::kSchema, "wrapper loop detected");
    }
    wrapper_chain.push_back(wrapper);
    auto next = FetchWrapperDocument(wrapper, fetch, options, parser_options);
    if (!next.ok()) {
      return next.status();
    }
    ++depth;
    document = std::move(next);
  }
}

}
