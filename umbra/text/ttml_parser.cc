#include "umbra/text/ttml_parser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"
#include "umbra/player/text_track.h"

namespace umbra::text {
namespace {

struct Node {
  std::string name;
  std::map<std::string, std::string, std::less<>> attributes;
  std::string text;
  std::vector<Node> children;
};

std::string_view LocalName(std::string_view name) {
  const std::size_t colon = name.rfind(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

absl::Status Invalid(std::string reason) {
  return base::MakeError(base::ErrorCode::kMediaCorrupt, base::UrlRedactor{},
                         {{"reason", std::move(reason)}});
}

class XmlReader {
 public:
  XmlReader(std::string_view input, std::size_t max_depth,
            std::size_t max_nodes)
      : input_(input), max_depth_(max_depth), max_nodes_(max_nodes) {}

  absl::StatusOr<Node> Parse() {
    SkipSpace();
    if (StartsWith("<?xml")) {
      absl::Status status = SkipProcessingInstruction();
      if (!status.ok()) {
        return status;
      }
    }
    Node root;
    std::vector<Node*> stack;
    while (position_ < input_.size()) {
      if (StartsWith("<!") || StartsWith("&")) {
        return Invalid("DTD, declarations and entity references are disabled");
      }
      absl::Status status;
      if (StartsWith("<!--")) {
        status = SkipComment();
      } else if (StartsWith("<?")) {
        status = SkipProcessingInstruction();
      } else if (StartsWith("</")) {
        status = CloseElement(stack);
      } else if (StartsWith("<")) {
        status = OpenElement(root, stack);
      } else {
        status = ReadText(stack);
      }
      if (!status.ok()) {
        return status;
      }
    }
    if (!stack.empty() || root.name.empty()) {
      return Invalid("incomplete XML document");
    }
    return root;
  }

 private:
  absl::Status SkipComment() {
    const std::size_t end = input_.find("-->", position_ + 4);
    if (end == std::string_view::npos) {
      return Invalid("unterminated XML comment");
    }
    position_ = end + 3;
    return absl::OkStatus();
  }

  absl::Status SkipProcessingInstruction() {
    const std::size_t end = input_.find("?>", position_ + 2);
    if (end == std::string_view::npos) {
      return Invalid("unterminated processing instruction");
    }
    position_ = end + 2;
    return absl::OkStatus();
  }

  absl::Status CloseElement(std::vector<Node*>& stack) {
    position_ += 2;
    auto name = ReadName();
    if (!name.ok()) {
      return name.status();
    }
    SkipSpace();
    if (!Consume(">") || stack.empty() || stack.back()->name != *name) {
      return Invalid("mismatched XML closing tag");
    }
    stack.pop_back();
    return absl::OkStatus();
  }

  absl::Status OpenElement(Node& root, std::vector<Node*>& stack) {
    if (stack.size() >= max_depth_) {
      return Invalid("XML nesting depth limit exceeded");
    }
    if (++node_count_ > max_nodes_) {
      return Invalid("XML node limit exceeded");
    }
    position_++;
    auto name = ReadName();
    if (!name.ok()) {
      return name.status();
    }
    Node node;
    node.name = std::move(*name);
    bool self_closing = false;
    while (true) {
      SkipSpace();
      if (Consume("/>")) {
        self_closing = true;
        break;
      }
      if (Consume(">")) {
        break;
      }
      const absl::Status status = ReadAttribute(node);
      if (!status.ok()) {
        return status;
      }
    }
    if (stack.empty()) {
      if (!root.name.empty()) {
        return Invalid("multiple XML root elements");
      }
      root = std::move(node);
      if (!self_closing) {
        stack.push_back(&root);
      }
    } else {
      stack.back()->children.push_back(std::move(node));
      if (!self_closing) {
        stack.push_back(&stack.back()->children.back());
      }
    }
    return absl::OkStatus();
  }

  absl::Status ReadAttribute(Node& node) {
    auto key = ReadName();
    if (!key.ok()) {
      return key.status();
    }
    SkipSpace();
    if (!Consume("=")) {
      return Invalid("XML attribute has no value");
    }
    SkipSpace();
    auto value = ReadQuotedValue();
    if (!value.ok()) {
      return value.status();
    }
    const std::string local(LocalName(*key));
    if (!node.attributes.emplace(local, std::move(*value)).second) {
      return Invalid("duplicate XML attribute");
    }
    return absl::OkStatus();
  }

  absl::Status ReadText(std::vector<Node*>& stack) {
    const std::size_t end = input_.find('<', position_);
    const std::size_t length =
        (end == std::string_view::npos ? input_.size() : end) - position_;
    auto text = Decode(input_.substr(position_, length));
    if (!text.ok()) {
      return text.status();
    }
    if (!stack.empty()) {
      stack.back()->text.append(*text);
    } else if (!text->empty() && !OnlySpace(*text)) {
      return Invalid("text outside the XML root element");
    }
    position_ += length;
    return absl::OkStatus();
  }

  [[nodiscard]] bool StartsWith(std::string_view value) const {
    return input_.substr(position_).starts_with(value);
  }

  bool Consume(std::string_view value) {
    if (!StartsWith(value)) {
      return false;
    }
    position_ += value.size();
    return true;
  }

  void SkipSpace() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\r' || input_[position_] == '\n')) {
      position_++;
    }
  }

  absl::StatusOr<std::string> ReadName() {
    const std::size_t begin = position_;
    while (position_ < input_.size()) {
      const char value = input_[position_];
      if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '-' ||
          value == ':' || value == '.') {
        position_++;
      } else {
        break;
      }
    }
    if (position_ == begin) {
      return Invalid("invalid XML name");
    }
    return std::string(input_.substr(begin, position_ - begin));
  }

  absl::StatusOr<std::string> ReadQuotedValue() {
    if (position_ >= input_.size() ||
        (input_[position_] != '\'' && input_[position_] != '"')) {
      return Invalid("XML attribute is not quoted");
    }
    const char quote = input_[position_++];
    const std::size_t end = input_.find(quote, position_);
    if (end == std::string_view::npos) {
      return Invalid("unterminated XML attribute");
    }
    auto value = Decode(input_.substr(position_, end - position_));
    position_ = end + 1;
    return value;
  }

  static bool OnlySpace(std::string_view value) {
    return std::ranges::all_of(value, [](char character) {
      return character == ' ' || character == '\t' || character == '\r' ||
             character == '\n';
    });
  }

  static absl::StatusOr<std::string> Decode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); index++) {
      if (value[index] != '&') {
        result.push_back(value[index]);
        continue;
      }
      const std::size_t end = value.find(';', index + 1);
      if (end == std::string_view::npos) {
        return Invalid("unterminated XML entity reference");
      }
      const std::string_view entity = value.substr(index + 1, end - index - 1);
      if (entity == "amp") {
        result.push_back('&');
      } else if (entity == "lt") {
        result.push_back('<');
      } else if (entity == "gt") {
        result.push_back('>');
      } else if (entity == "quot") {
        result.push_back('"');
      } else if (entity == "apos") {
        result.push_back('\'');
      } else {
        return Invalid("unsupported XML entity reference");
      }
      index = end;
    }
    return result;
  }

  std::string_view input_;
  std::size_t max_depth_;
  std::size_t max_nodes_;
  std::size_t node_count_ = 0;
  std::size_t position_ = 0;
};

std::optional<double> Number(std::string_view value) {
  std::string buffer(value);
  const char* first = buffer.data();
  const char* last = first;
  std::advance(last, static_cast<std::ptrdiff_t>(buffer.size()));
  double result = 0.0;
  const auto parsed = std::from_chars(first, last, result);
  if (parsed.ec != std::errc() || parsed.ptr != last ||
      !std::isfinite(result)) {
    return std::nullopt;
  }
  return result;
}

std::optional<double> ParseClockTime(std::string_view value,
                                     double frame_rate) {
  std::array<double, 4> parts{};
  std::size_t start = 0;
  std::size_t count = 0;
  while (count < parts.size()) {
    const std::size_t end = value.find(':', start);
    const std::size_t stop = end == std::string_view::npos ? value.size() : end;
    const auto parsed = Number(value.substr(start, stop - start));
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    parts.at(count++) = *parsed;
    start = stop + 1;
    if (end == std::string_view::npos) {
      break;
    }
  }
  if ((count != 3 && count != 4) || start <= value.size()) {
    return std::nullopt;
  }
  const double clock = (parts[0] * 3600.0) + (parts[1] * 60.0) + parts[2];
  if (count == 3) {
    return clock;
  }
  return frame_rate > 0.0
             ? std::optional<double>(clock + (parts[3] / frame_rate))
             : std::nullopt;
}

std::optional<double> ParseOffsetTime(std::string_view value, double frame_rate,
                                      double tick_rate) {
  if (value.ends_with("ms")) {
    const auto number = Number(value.substr(0, value.size() - 2));
    return number.has_value() ? std::optional<double>(*number / 1000.0)
                              : std::nullopt;
  }
  if (value.ends_with("h") || value.ends_with("m") || value.ends_with("s") ||
      value.ends_with("f") || value.ends_with("t")) {
    const char unit = value.back();
    const auto number = Number(value.substr(0, value.size() - 1));
    if (!number.has_value()) {
      return std::nullopt;
    }
    if (unit == 'h') {
      return *number * 3600.0;
    }
    if (unit == 'm') {
      return *number * 60.0;
    }
    if (unit == 'f') {
      return frame_rate > 0.0 ? std::optional<double>(*number / frame_rate)
                              : std::nullopt;
    }
    if (unit == 't') {
      return tick_rate > 0.0 ? std::optional<double>(*number / tick_rate)
                             : std::nullopt;
    }
    return number;
  }
  return Number(value);
}

std::optional<double> ParseTime(std::string_view value, double frame_rate,
                                double tick_rate) {
  if (value.find(':') != std::string_view::npos) {
    return ParseClockTime(value, frame_rate);
  }
  return ParseOffsetTime(value, frame_rate, tick_rate);
}

std::string Attribute(const Node& node, std::string_view name) {
  const auto found = node.attributes.find(name);
  return found == node.attributes.end() ? std::string() : found->second;
}

std::optional<std::array<double, 2>> ParseOrigin(std::string_view value) {
  const std::size_t separator = value.find_first_of(" \t");
  if (separator == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t y_start = value.find_first_not_of(" \t", separator);
  if (y_start == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view x_value = value.substr(0, separator);
  std::string_view y_value = value.substr(y_start);
  if (!x_value.ends_with('%') || !y_value.ends_with('%')) {
    return std::nullopt;
  }
  x_value.remove_suffix(1);
  y_value.remove_suffix(1);
  const auto x = Number(x_value);
  const auto y = Number(y_value);
  if (!x.has_value() || !y.has_value()) {
    return std::nullopt;
  }
  return std::array<double, 2>{*x, *y};
}

struct Timing {
  double begin = 0.0;
  std::optional<double> end;
  double frame_rate = 30.0;
  double tick_rate = 1.0;
};

bool ResolveTiming(const Node& node, const Timing& parent, Timing* result) {
  *result = parent;
  const std::string begin = Attribute(node, "begin");
  if (!begin.empty()) {
    const auto offset = ParseTime(begin, parent.frame_rate, parent.tick_rate);
    if (!offset.has_value()) {
      return false;
    }
    result->begin = parent.begin + *offset;
  }
  const std::string end = Attribute(node, "end");
  const std::string dur = Attribute(node, "dur");
  if (!end.empty()) {
    const auto offset = ParseTime(end, parent.frame_rate, parent.tick_rate);
    if (!offset.has_value()) {
      return false;
    }
    result->end = parent.begin + *offset;
  } else if (!dur.empty()) {
    const auto duration = ParseTime(dur, parent.frame_rate, parent.tick_rate);
    if (!duration.has_value()) {
      return false;
    }
    result->end = result->begin + *duration;
  }
  if (parent.end.has_value() &&
      (!result->end.has_value() || *result->end > *parent.end)) {
    result->end = parent.end;
  }
  return true;
}

void CollectText(const Node& node, player::TextCue* cue,
                 player::StyledTextRun style, std::size_t* next_id) {
  if (!node.text.empty()) {
    style.text = node.text;
    cue->runs.push_back(style);
  }
  for (const Node& child : node.children) {
    if (LocalName(child.name) == "br") {
      player::StyledTextRun newline = style;
      newline.text = "\n";
      cue->runs.push_back(std::move(newline));
      continue;
    }
    player::StyledTextRun child_style = style;
    const std::string name(LocalName(child.name));
    child_style.bold =
        style.bold || name == "b" || Attribute(child, "fontWeight") == "bold";
    child_style.italic = style.italic || name == "i" ||
                         Attribute(child, "fontStyle") == "italic";
    child_style.underline =
        style.underline || name == "u" ||
        Attribute(child, "textDecoration").find("underline") !=
            std::string::npos;
    const std::string ruby = Attribute(child, "ruby");
    if (ruby == "text") {
      cue->ruby_annotation += child.text;
    }
    child_style.language = Attribute(child, "lang");
    CollectText(child, cue, std::move(child_style), next_id);
  }
  static_cast<void>(next_id);
}

absl::Status Walk(const Node& node, const Timing& parent,
                  TtmlDocument* document, const TtmlParserOptions& options,
                  std::size_t* next_id, std::string region) {
  Timing timing;
  if (!ResolveTiming(node, parent, &timing)) {
    return Invalid("invalid TTML timing expression");
  }
  if (LocalName(node.name) == "region") {
    region = Attribute(node, "id");
  }
  if (LocalName(node.name) == "p") {
    if (!timing.end.has_value()) {
      return Invalid("TTML paragraph has no resolved end time");
    }
    if (document->text_cues.size() >= options.max_cues) {
      return Invalid("TTML cue count limit exceeded");
    }
    if (*timing.end < timing.begin) {
      return Invalid("TTML paragraph ends before it begins");
    }
    player::TextCue cue;
    cue.id = ++*next_id;
    cue.start_time = timing.begin + options.sample_time_offset;
    cue.end_time = *timing.end + options.sample_time_offset;
    cue.region = Attribute(node, "region");
    if (cue.region.empty()) {
      cue.region = region;
    }
    cue.right_to_left = Attribute(node, "direction") == "rtl";
    const std::string writing_mode = Attribute(node, "writingMode");
    cue.vertical =
        writing_mode.starts_with("tbr") || writing_mode.starts_with("tbl");
    const std::string origin = Attribute(node, "origin");
    if (!origin.empty()) {
      const auto parsed_origin = ParseOrigin(origin);
      if (parsed_origin.has_value()) {
        cue.position = std::clamp((*parsed_origin)[0], 0.0, 100.0);
        cue.line = std::clamp((*parsed_origin)[1], 0.0, 100.0);
      }
    }
    player::StyledTextRun style;
    style.language = Attribute(node, "lang");
    CollectText(node, &cue, std::move(style), next_id);
    if (!cue.runs.empty()) {
      document->text_cues.push_back(std::move(cue));
    }
    return absl::OkStatus();
  }
  for (const Node& child : node.children) {
    absl::Status status =
        Walk(child, timing, document, options, next_id, region);
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

}

absl::StatusOr<TtmlDocument> ParseTtml(std::string_view document,
                                       const TtmlParserOptions& options) {
  if (document.size() > options.max_document_bytes) {
    return Invalid("TTML document byte limit exceeded");
  }
  if (!std::isfinite(options.sample_time_offset) ||
      options.sample_time_offset < 0.0 || options.max_depth == 0 ||
      options.max_nodes == 0 || options.max_cues == 0) {
    return Invalid("invalid TTML parser limits");
  }
  if (document.find("<!DOCTYPE") != std::string_view::npos ||
      document.find("<!ENTITY") != std::string_view::npos ||
      document.find("<!doctype") != std::string_view::npos ||
      document.find("<!entity") != std::string_view::npos) {
    return Invalid("DTD and entity declarations are disabled");
  }
  XmlReader reader(document, options.max_depth, options.max_nodes);
  auto root = reader.Parse();
  if (!root.ok()) {
    return root.status();
  }
  if (LocalName(root->name) != "tt") {
    return Invalid("root element is not TTML");
  }
  Timing timing;
  double frame_rate = 30.0;
  const std::string frame_rate_value = Attribute(*root, "frameRate");
  if (!frame_rate_value.empty()) {
    const auto parsed = Number(frame_rate_value);
    if (!parsed.has_value() || *parsed <= 0.0) {
      return Invalid("invalid TTML frame rate");
    }
    frame_rate = *parsed;
  }
  const std::string multiplier = Attribute(*root, "frameRateMultiplier");
  if (!multiplier.empty()) {
    const std::size_t split = multiplier.find(' ');
    if (split == std::string::npos) {
      return Invalid("invalid TTML frame rate multiplier");
    }
    const auto numerator =
        Number(std::string_view(multiplier).substr(0, split));
    const auto denominator =
        Number(std::string_view(multiplier).substr(split + 1));
    if (!numerator.has_value() || !denominator.has_value() ||
        *denominator == 0.0) {
      return Invalid("invalid TTML frame rate multiplier");
    }
    frame_rate *= *numerator / *denominator;
  }
  timing.frame_rate = frame_rate;
  const std::string tick_rate_value = Attribute(*root, "tickRate");
  if (!tick_rate_value.empty()) {
    const auto parsed = Number(tick_rate_value);
    if (!parsed.has_value() || *parsed <= 0.0) {
      return Invalid("invalid TTML tick rate");
    }
    timing.tick_rate = *parsed;
  } else {
    timing.tick_rate = frame_rate;
  }
  const std::string time_base = Attribute(*root, "timeBase");
  if (!time_base.empty() && time_base != "media" && time_base != "clock") {
    return Invalid("unsupported TTML time base");
  }
  TtmlDocument result;
  std::size_t next_id = 0;
  absl::Status status = Walk(*root, timing, &result, options, &next_id, {});
  if (!status.ok()) {
    return status;
  }
  return result;
}

}
