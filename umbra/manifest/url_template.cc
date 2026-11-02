#include "umbra/manifest/url_template.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "umbra/manifest/manifest_error.h"

namespace umbra::manifest {
namespace {

constexpr int kMaxWidth = 20;

std::string Pad(std::int64_t value, int width) {
  const bool negative = value < 0;
  std::string digits = std::to_string(value);
  if (negative) {
    digits.erase(0, 1);
  }
  const auto target = static_cast<std::size_t>(width);
  std::string result = negative ? "-" : "";
  if (digits.size() < target) {
    result.append(target - digits.size(), '0');
  }
  result += digits;
  return result;
}

}

absl::StatusOr<UrlTemplate::Part> UrlTemplate::ParseToken(
    std::string_view token) {
  const std::size_t percent = token.find('%');
  const std::string_view name = token.substr(0, percent);
  Part part;
  if (name == "Number") {
    part.kind = PartKind::kNumber;
  } else if (name == "Time") {
    part.kind = PartKind::kTime;
  } else if (name == "Bandwidth") {
    part.kind = PartKind::kBandwidth;
  } else if (name == "RepresentationID") {
    part.kind = PartKind::kRepresentationId;
  } else {
    return InvalidManifest(
        absl::StrCat("segment template identifier '", name, "' is unknown"));
  }
  if (percent == std::string_view::npos) {
    return part;
  }
  if (part.kind == PartKind::kRepresentationId) {
    return InvalidManifest(
        "$RepresentationID$ in a segment template takes no format tag");
  }
  std::string_view format = token.substr(percent);
  if (format.size() < 4 || !format.starts_with("%0") || format.back() != 'd') {
    return InvalidManifest("segment template format tag is malformed");
  }
  format = format.substr(2, format.size() - 3);
  int width = 0;
  if (!absl::SimpleAtoi(format, &width) || width < 1 || width > kMaxWidth ||
      format.front() == '+' || format.front() == '-') {
    return InvalidManifest("segment template format width is invalid");
  }
  part.width = width;
  return part;
}

absl::StatusOr<UrlTemplate> UrlTemplate::Parse(std::string_view text) {
  UrlTemplate parsed;
  parsed.text_ = std::string(text);
  std::string literal;
  const auto flush = [&parsed, &literal] {
    if (!literal.empty()) {
      Part text_part;
      text_part.literal = std::move(literal);
      literal.clear();
      parsed.parts_.push_back(std::move(text_part));
    }
  };
  std::size_t position = 0;
  while (position < text.size()) {
    const std::size_t open = text.find('$', position);
    if (open == std::string_view::npos) {
      literal.append(text.substr(position));
      break;
    }
    literal.append(text.substr(position, open - position));
    const std::size_t close = text.find('$', open + 1);
    if (close == std::string_view::npos) {
      return InvalidManifest("segment template has an unterminated $");
    }
    position = close + 1;
    const std::string_view token = text.substr(open + 1, close - open - 1);
    if (token.empty()) {
      literal.push_back('$');
      continue;
    }
    absl::StatusOr<Part> part = ParseToken(token);
    if (!part.ok()) {
      return part.status();
    }
    flush();
    parsed.uses_number_ =
        parsed.uses_number_ || part->kind == PartKind::kNumber;
    parsed.uses_time_ = parsed.uses_time_ || part->kind == PartKind::kTime;
    parsed.parts_.push_back(*std::move(part));
  }
  flush();
  if (!parsed.uses_number_ && !parsed.uses_time_) {
    return InvalidManifest("segment template needs $Number$ or $Time$");
  }
  return parsed;
}

std::string UrlTemplate::Expand(const TemplateValues& values) const {
  std::string result;
  for (const Part& part : parts_) {
    switch (part.kind) {
      case PartKind::kLiteral:
        result += part.literal;
        break;
      case PartKind::kNumber:
        result += Pad(values.number, part.width);
        break;
      case PartKind::kTime:
        result += Pad(values.time, part.width);
        break;
      case PartKind::kBandwidth:
        result += Pad(values.bandwidth, part.width);
        break;
      case PartKind::kRepresentationId:
        result += values.representation_id;
        break;
    }
  }
  return result;
}

}
