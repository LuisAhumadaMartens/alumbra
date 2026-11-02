#include "umbra/base/url_redactor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "umbra/base/sip_hash.h"

namespace umbra::base {
namespace {

std::string Normalize(std::string_view text) {
  std::string result;
  for (unsigned char ch : text) {
    if (ch >= 'A' && ch <= 'Z') {
      ch += 'a' - 'A';
    }
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      result += static_cast<char>(ch);
    }
  }
  return result;
}

bool Prohibited(std::string_view key) {
  const std::string normalized = Normalize(key);
  for (std::string_view const word :
       {"authorization", "cookie", "token", "password", "passwd", "secret",
        "credential", "signature", "apikey", "license", "licence", "drmbody"}) {
    if (normalized.find(word) != std::string::npos) {
      return true;
    }
  }
  return normalized == "auth" || normalized == "key" || normalized == "sig";
}

std::array<std::uint64_t, 2> NewKey() {
  std::random_device random;
  std::uniform_int_distribution<std::uint64_t> distribution;
  return {distribution(random), distribution(random)};
}

std::string Digest(std::string_view url, std::array<std::uint64_t, 2> key) {
  std::uint64_t digest = internal::SipHash24(url, key);
  std::string result(16, '0');
  constexpr std::string_view kHexDigits = "0123456789abcdef";
  for (int i = 15; i >= 0; --i) {
    result[i] = kHexDigits[digest & 15];
    digest >>= 4;
  }
  return result;
}

bool IsKeyChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
}

bool IsSchemeChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.';
}

bool ValidPort(std::string_view port) {
  if (port.empty() || port.size() > 5 ||
      port.find_first_not_of("0123456789") != std::string_view::npos) {
    return false;
  }
  unsigned int number = 0;
  for (const char digit : port) {
    number = (number * 10) + static_cast<unsigned int>(digit - '0');
  }
  return number <= 65535;
}

bool ValidIpLiteral(std::string_view authority, std::string_view& port) {
  const auto end = authority.find(']');
  if (end == std::string_view::npos || end <= 1) {
    return false;
  }
  const std::string_view host = authority.substr(1, end - 1);
  if (host.find(':') == std::string_view::npos ||
      host.find_first_not_of("0123456789abcdefABCDEF:.") !=
          std::string_view::npos) {
    return false;
  }
  authority.remove_prefix(end + 1);
  if (!authority.empty() && !authority.starts_with(':')) {
    return false;
  }
  port = authority;
  return true;
}

bool ValidHostName(std::string_view host) {
  if (host.empty() || host.size() > 253 ||
      host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTU"
                             "VWXYZ0123456789.-") != std::string_view::npos) {
    return false;
  }
  if (host.ends_with('.')) {
    host.remove_suffix(1);
  }
  while (!host.empty()) {
    const auto dot = host.find('.');
    const auto label = host.substr(0, dot);
    if (label.empty() || label.size() > 63 || label.starts_with('-') ||
        label.ends_with('-')) {
      return false;
    }
    if (dot == std::string_view::npos) {
      break;
    }
    host.remove_prefix(dot + 1);
  }
  return true;
}

bool ValidAuthority(std::string_view authority) {
  std::string_view port;
  if (authority.starts_with('[')) {
    if (!ValidIpLiteral(authority, port)) {
      return false;
    }
  } else {
    const auto end = authority.find(':');
    if (!ValidHostName(authority.substr(0, end))) {
      return false;
    }
    port = end == std::string_view::npos ? std::string_view{}
                                         : authority.substr(end);
  }
  return port.empty() || ValidPort(port.substr(1));
}

bool ParseOrigin(std::string_view url, std::string& scheme,
                 std::string_view& authority, std::size_t& authority_end) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return false;
  }
  scheme = Normalize(url.substr(0, scheme_end));
  if ((scheme != "http" && scheme != "https") ||
      url.substr(0, scheme_end).size() != scheme.size()) {
    return false;
  }
  authority_end = url.find_first_of("/?#", scheme_end + 3);
  authority = url.substr(scheme_end + 3, authority_end == std::string_view::npos
                                             ? std::string_view::npos
                                             : authority_end - scheme_end - 3);
  const auto user_end = authority.rfind('@');
  if (user_end != std::string_view::npos) {
    authority.remove_prefix(user_end + 1);
  }
  return !authority.empty() && ValidAuthority(authority) &&
         authority.find_first_not_of(
             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "0123456789.-:[]") == std::string_view::npos &&
         std::ranges::none_of(url, [](unsigned char ch) {
           return ch <= 32 || ch >= 127 || ch == '\\';
         });
}

}

UrlRedactor::UrlRedactor()
    : playback_key_(NewKey()), provenance_key_(NewKey()) {}

UrlRedactor::UrlRedactor(std::array<std::uint64_t, 2> playback_key,
                         const std::set<std::string>& token_headers,
                         UrlDiagnostics mode, DebugUrlAllowlist debug_allowlist)
    : playback_key_(playback_key),
      provenance_key_(NewKey()),
      mode_(mode),
      debug_allowlist_(std::move(debug_allowlist)) {
  for (const auto& header : token_headers) {
    token_headers_.insert(Normalize(header));
  }
}

std::string UrlRedactor::RedactUrl(std::string_view url) const {
  const std::string digest = " [url:" + Digest(url, playback_key_) + "]";
  std::string scheme;
  std::string_view authority;
  std::size_t authority_end = std::string_view::npos;
  if (!ParseOrigin(url, scheme, authority, authority_end)) {
    return "[redacted-url]" + digest;
  }
  std::string result = scheme + "://" + std::string(authority);
  if (mode_ != UrlDiagnostics::kDebugFullUrl ||
      authority_end == std::string_view::npos) {
    return result + digest;
  }
  const auto query_start = url.find('?', authority_end);
  const auto fragment_start = url.find('#', authority_end);
  const auto path_end = std::min(query_start, fragment_start);
  const std::string path(url.substr(authority_end, path_end - authority_end));
  result += debug_allowlist_.paths.contains(path) ? path : "/[redacted]";
  if (query_start != std::string_view::npos && query_start < fragment_start) {
    result += '?';
    AppendDebugQuery(
        url.substr(query_start + 1, fragment_start - query_start - 1), result);
  }
  if (fragment_start != std::string_view::npos) {
    const std::string fragment(url.substr(fragment_start + 1));
    result += debug_allowlist_.fragments.contains(fragment) ? "#" + fragment
                                                            : "#[redacted]";
  }
  return result + digest;
}

void UrlRedactor::AppendDebugQuery(std::string_view query,
                                   std::string& result) const {
  while (!query.empty()) {
    const auto end = query.find('&');
    const auto pair = query.substr(0, end);
    const auto equal = pair.find('=');
    const std::string name(pair.substr(0, equal));
    const std::string value(equal == std::string_view::npos
                                ? std::string_view{}
                                : pair.substr(equal + 1));
    const auto allowed = debug_allowlist_.query_values.find(name);
    if (name.find('%') == std::string::npos && !Prohibited(name) &&
        allowed != debug_allowlist_.query_values.end() &&
        allowed->second.contains(value) &&
        !token_headers_.contains(Normalize(name))) {
      result += name;
      result += '=';
      result += value;
    } else {
      result += "[redacted]";
    }
    if (end == std::string_view::npos) {
      break;
    }
    result += '&';
    query.remove_prefix(end + 1);
  }
}

std::string UrlRedactor::RedactText(std::string_view text) const {
  return RedactSecretPairs(RedactUrlsInText(text));
}

std::string UrlRedactor::RedactUrlsInText(std::string_view text) const {
  std::string result;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto delimiter = text.find("://", offset);
    if (delimiter == std::string_view::npos) {
      result += text.substr(offset);
      break;
    }
    auto start = delimiter;
    while (start > offset && IsSchemeChar(text[start - 1])) {
      --start;
    }
    result += text.substr(offset, start - offset);
    auto end = text.find_first_of(" \t\r\n\"<>", delimiter + 3);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    result += RedactUrl(text.substr(start, end - start));
    offset = end;
  }
  return result;
}

std::string UrlRedactor::RedactSecretPairs(std::string_view text) const {
  std::string result;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto separator = text.find_first_of(":=", offset);
    if (separator == std::string_view::npos) {
      break;
    }
    const bool header = text[separator] == ':';
    result += text.substr(offset, separator + 1 - offset);
    offset = separator + 1;
    if (header && text.substr(separator).starts_with("://")) {
      continue;
    }
    auto key_end = separator;
    if (key_end > 0 &&
        (text[key_end - 1] == '"' || text[key_end - 1] == '\'')) {
      --key_end;
    }
    auto key_start = key_end;
    while (key_start > 0 && IsKeyChar(text[key_start - 1])) {
      --key_start;
    }
    const std::string_view key = text.substr(key_start, key_end - key_start);
    if (key.empty() ||
        !(Prohibited(key) || token_headers_.contains(Normalize(key)))) {
      continue;
    }
    while (offset < text.size() &&
           (text[offset] == ' ' || text[offset] == '\t')) {
      result += text[offset++];
    }
    auto value_end =
        text.find_first_of(header ? "\r\n" : " \t\r\n&;,\"'", offset);
    if (value_end == std::string_view::npos) {
      value_end = text.size();
    }
    result += "[redacted]";
    offset = value_end;
  }
  result += text.substr(offset);
  return result;
}

std::string UrlRedactor::RedactField(std::string_view key,
                                     std::string_view value) const {
  const auto normalized = Normalize(key);
  if (Prohibited(key) || token_headers_.contains(normalized) ||
      normalized.find("header") != std::string::npos ||
      normalized.find("body") != std::string::npos) {
    return "[redacted]";
  }
  if (normalized.find("url") != std::string::npos ||
      normalized.find("uri") != std::string::npos) {
    return RedactUrl(value);
  }
  return RedactText(value);
}

std::array<std::uint64_t, 2> UrlRedactor::StatusAuthenticator(
    std::string_view payload, std::string_view message) const {
  std::string input = std::to_string(payload.size()) + ":";
  input += payload;
  input += message;
  input += '0';
  const auto first = internal::SipHash24(input, provenance_key_);
  input.back() = '1';
  return {first, internal::SipHash24(input, provenance_key_)};
}

}
