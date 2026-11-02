#include "umbra/source/url.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace umbra::source {
namespace {

constexpr std::string_view kSubDelims = "!$&'()*+,;=";
constexpr std::string_view kGenDelims = ":/?#[]@";
constexpr std::string_view kUnreservedMarks = "-._~";
constexpr int kMaxPort = 65535;
constexpr std::size_t kMaxPortDigits = 5;

bool IsAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsHex(char c) {
  return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char ToLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    c = ToLower(c);
  }
  return result;
}

bool IsUnreserved(char c) {
  return IsAlpha(c) || IsDigit(c) ||
         kUnreservedMarks.find(c) != std::string_view::npos;
}

absl::Status Invalid(std::string_view reason) {
  return absl::InvalidArgumentError(std::string("invalid URL: ") +
                                    std::string(reason));
}

absl::Status ValidateCharacters(std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '%') {
      if (i + 2 >= text.size()) {
        return Invalid("truncated percent escape");
      }
      if (!IsHex(text[i + 1]) || !IsHex(text[i + 2])) {
        return Invalid("malformed percent escape");
      }
      i += 2;
      continue;
    }
    if (!IsUnreserved(c) && kSubDelims.find(c) == std::string_view::npos &&
        kGenDelims.find(c) == std::string_view::npos) {
      return Invalid("character is not allowed");
    }
  }
  return absl::OkStatus();
}

bool IsValidScheme(std::string_view scheme) {
  if (scheme.empty() || !IsAlpha(scheme.front())) {
    return false;
  }
  return std::ranges::all_of(scheme, [](char c) {
    return IsAlpha(c) || IsDigit(c) || c == '+' || c == '-' || c == '.';
  });
}

bool IsValidRegName(std::string_view host) {
  for (std::size_t i = 0; i < host.size(); ++i) {
    const char c = host[i];
    if (c == '%') {
      i += 2;
      continue;
    }
    if (!IsUnreserved(c) && kSubDelims.find(c) == std::string_view::npos) {
      return false;
    }
  }
  return true;
}

bool IsValidIpLiteral(std::string_view host) {
  if (host.size() < 3 || host.front() != '[' || host.back() != ']') {
    return false;
  }
  return std::ranges::all_of(host.substr(1, host.size() - 2), [](char c) {
    return IsHex(c) || c == ':' || c == '.' || c == 'v' || c == 'V' ||
           c == '%' || IsUnreserved(c);
  });
}

bool IsValidPort(std::string_view port) {
  if (port.size() > kMaxPortDigits) {
    return false;
  }
  int value = 0;
  for (const char c : port) {
    if (!IsDigit(c)) {
      return false;
    }
    value = (value * 10) + (c - '0');
  }
  return value <= kMaxPort;
}

absl::Status ParseAuthority(std::string_view authority, UrlReference& url) {
  std::string_view host_port = authority;
  const std::size_t at = authority.rfind('@');
  if (at != std::string_view::npos) {
    url.userinfo = std::string(authority.substr(0, at));
    host_port = authority.substr(at + 1);
  }
  std::string_view host = host_port;
  std::optional<std::string_view> port;
  if (!host_port.empty() && host_port.front() == '[') {
    const std::size_t close = host_port.find(']');
    if (close == std::string_view::npos) {
      return Invalid("unterminated IP literal");
    }
    host = host_port.substr(0, close + 1);
    const std::string_view rest = host_port.substr(close + 1);
    if (!rest.empty()) {
      if (rest.front() != ':') {
        return Invalid("unexpected text after IP literal");
      }
      port = rest.substr(1);
    }
    if (!IsValidIpLiteral(host)) {
      return Invalid("malformed IP literal");
    }
  } else {
    const std::size_t colon = host_port.find(':');
    if (colon != std::string_view::npos) {
      host = host_port.substr(0, colon);
      port = host_port.substr(colon + 1);
    }
    if (!IsValidRegName(host)) {
      return Invalid("malformed host");
    }
  }
  if (port.has_value() && !IsValidPort(*port)) {
    return Invalid("malformed port");
  }
  url.host = Lower(host);
  if (port.has_value()) {
    url.port = std::string(*port);
  }
  return absl::OkStatus();
}

void PopLastSegment(std::string& output) {
  const std::size_t slash = output.rfind('/');
  output.erase(slash == std::string::npos ? 0 : slash);
}

std::string MergePaths(const UrlReference& base, std::string_view reference) {
  if (base.has_authority() && base.path.empty()) {
    return std::string("/") + std::string(reference);
  }
  const std::size_t slash = base.path.rfind('/');
  if (slash == std::string::npos) {
    return std::string(reference);
  }
  return base.path.substr(0, slash + 1) + std::string(reference);
}

}

absl::StatusOr<UrlReference> UrlReference::Parse(std::string_view text) {
  if (absl::Status status = ValidateCharacters(text); !status.ok()) {
    return status;
  }
  UrlReference url;
  std::string_view rest = text;
  const std::size_t first_delimiter = rest.find_first_of(":/?#");
  if (first_delimiter != std::string_view::npos &&
      rest[first_delimiter] == ':') {
    const std::string_view scheme = rest.substr(0, first_delimiter);
    if (!IsValidScheme(scheme)) {
      return Invalid("malformed scheme");
    }
    url.scheme = Lower(scheme);
    rest.remove_prefix(first_delimiter + 1);
  }
  const std::size_t hash = rest.find('#');
  if (hash != std::string_view::npos) {
    url.fragment = std::string(rest.substr(hash + 1));
    rest = rest.substr(0, hash);
  }
  const std::size_t question = rest.find('?');
  if (question != std::string_view::npos) {
    url.query = std::string(rest.substr(question + 1));
    rest = rest.substr(0, question);
  }
  if (rest.starts_with("//")) {
    rest.remove_prefix(2);
    const std::size_t slash = rest.find('/');
    const std::string_view authority =
        slash == std::string_view::npos ? rest : rest.substr(0, slash);
    if (absl::Status status = ParseAuthority(authority, url); !status.ok()) {
      return status;
    }
    rest = slash == std::string_view::npos ? std::string_view()
                                           : rest.substr(slash);
  }
  url.path = std::string(rest);
  return url;
}

absl::StatusOr<UrlReference> UrlReference::ParseAbsolute(
    std::string_view text) {
  absl::StatusOr<UrlReference> url = Parse(text);
  if (!url.ok()) {
    return url;
  }
  if (!url->is_absolute()) {
    return Invalid("a scheme is required");
  }
  return url;
}

std::string UrlReference::ToString() const {
  std::string result;
  if (scheme.has_value()) {
    result += *scheme;
    result += ':';
  }
  if (host.has_value()) {
    result += "//";
    if (userinfo.has_value()) {
      result += *userinfo;
      result += '@';
    }
    result += *host;
    if (port.has_value()) {
      result += ':';
      result += *port;
    }
  } else if (path.starts_with("//")) {
    result += "/.";
  }
  result += path;
  if (query.has_value()) {
    result += '?';
    result += *query;
  }
  if (fragment.has_value()) {
    result += '#';
    result += *fragment;
  }
  return result;
}

std::string UrlReference::Origin() const {
  if (!scheme.has_value() || !host.has_value()) {
    return {};
  }
  std::string result = *scheme + "://" + *host;
  if (port.has_value()) {
    result += ':';
    result += *port;
  }
  return result;
}

std::string UrlReference::PathExtension() const {
  const std::size_t slash = path.rfind('/');
  const std::string_view name = slash == std::string::npos
                                    ? std::string_view(path)
                                    : std::string_view(path).substr(slash + 1);
  const std::size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot + 1 >= name.size()) {
    return {};
  }
  return Lower(name.substr(dot + 1));
}

UrlReference UrlReference::WithoutUserInfo() const {
  UrlReference copy = *this;
  copy.userinfo.reset();
  return copy;
}

std::string RemoveDotSegments(std::string_view path) {
  std::string output;
  std::string_view input = path;
  while (!input.empty()) {
    if (input.starts_with("../")) {
      input.remove_prefix(3);
    } else if (input.starts_with("./") || input.starts_with("/./")) {
      input.remove_prefix(2);
    } else if (input == "/.") {
      input.remove_suffix(1);
    } else if (input.starts_with("/../")) {
      input.remove_prefix(3);
      PopLastSegment(output);
    } else if (input == "/..") {
      input.remove_suffix(2);
      PopLastSegment(output);
    } else if (input == "." || input == "..") {
      input = std::string_view();
    } else {
      const std::size_t next = input.find('/', input.front() == '/' ? 1 : 0);
      const std::size_t length =
          next == std::string_view::npos ? input.size() : next;
      output.append(input.substr(0, length));
      input.remove_prefix(length);
    }
  }
  return output;
}

absl::StatusOr<UrlReference> ResolveReference(const UrlReference& base,
                                              const UrlReference& reference) {
  if (!base.is_absolute()) {
    return Invalid("the base URL needs a scheme");
  }
  UrlReference target;
  if (reference.is_absolute()) {
    target = reference;
    target.path = RemoveDotSegments(reference.path);
  } else {
    if (reference.has_authority()) {
      target = reference;
      target.path = RemoveDotSegments(reference.path);
    } else {
      target.userinfo = base.userinfo;
      target.host = base.host;
      target.port = base.port;
      if (reference.path.empty()) {
        target.path = base.path;
        target.query =
            reference.query.has_value() ? reference.query : base.query;
      } else {
        target.path = reference.path.front() == '/'
                          ? RemoveDotSegments(reference.path)
                          : RemoveDotSegments(MergePaths(base, reference.path));
        target.query = reference.query;
      }
    }
    target.scheme = base.scheme;
  }
  target.fragment = reference.fragment;
  return target;
}

absl::StatusOr<UrlReference> ResolveReference(std::string_view base,
                                              std::string_view reference) {
  absl::StatusOr<UrlReference> parsed_base = UrlReference::ParseAbsolute(base);
  if (!parsed_base.ok()) {
    return parsed_base;
  }
  absl::StatusOr<UrlReference> parsed_reference =
      UrlReference::Parse(reference);
  if (!parsed_reference.ok()) {
    return parsed_reference;
  }
  return ResolveReference(*parsed_base, *parsed_reference);
}

}
