#include "umbra/base/url_redactor.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <random>
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
  for (std::string_view word :
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
  for (int i = 15; i >= 0; --i) {
    result[i] = "0123456789abcdef"[digest & 15];
    digest >>= 4;
  }
  return result;
}

bool IsSchemeChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.';
}

bool ValidAuthority(std::string_view authority) {
  std::string_view host;
  std::string_view port;
  if (authority.starts_with('[')) {
    const auto end = authority.find(']');
    if (end == std::string_view::npos || end <= 1) {
      return false;
    }
    host = authority.substr(1, end - 1);
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
  } else {
    const auto end = authority.find(':');
    host = authority.substr(0, end);
    port = end == std::string_view::npos ? std::string_view{}
                                         : authority.substr(end);
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
  }
  if (!port.empty()) {
    port.remove_prefix(1);
    unsigned int number = 0;
    const auto [end, error] =
        std::from_chars(port.data(), port.data() + port.size(), number);
    if (error != std::errc{} || end != port.data() + port.size() ||
        number > 65535) {
      return false;
    }
  }
  return true;
}

}

UrlRedactor::UrlRedactor() : playback_key_(NewKey()) {}

UrlRedactor::UrlRedactor(std::array<std::uint64_t, 2> playback_key,
                         std::set<std::string> token_headers,
                         UrlDiagnostics mode, DebugUrlAllowlist debug_allowlist)
    : playback_key_(playback_key),
      mode_(mode),
      debug_allowlist_(std::move(debug_allowlist)) {
  for (const auto& header : token_headers) {
    token_headers_.insert(Normalize(header));
  }
}

std::string UrlRedactor::RedactUrl(std::string_view url) const {
  const std::string digest = " [url:" + Digest(url, playback_key_) + "]";
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return "[redacted-url]" + digest;
  }
  const std::string scheme = Normalize(url.substr(0, scheme_end));
  if ((scheme != "http" && scheme != "https") ||
      url.substr(0, scheme_end).size() != scheme.size()) {
    return "[redacted-url]" + digest;
  }
  const auto authority_end = url.find_first_of("/?#", scheme_end + 3);
  std::string_view authority =
      url.substr(scheme_end + 3, authority_end == std::string_view::npos
                                     ? std::string_view::npos
                                     : authority_end - scheme_end - 3);
  const auto user_end = authority.rfind('@');
  if (user_end != std::string_view::npos) {
    authority.remove_prefix(user_end + 1);
  }
  if (authority.empty() || !ValidAuthority(authority) ||
      authority.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
          "0123456789.-:[]") != std::string_view::npos ||
      std::any_of(url.begin(), url.end(), [](unsigned char ch) {
        return ch <= 32 || ch >= 127 || ch == '\\';
      })) {
    return "[redacted-url]" + digest;
  }
  const std::string origin = scheme + "://" + std::string(authority);
  if (mode_ != UrlDiagnostics::kDebugFullUrl ||
      authority_end == std::string_view::npos) {
    return origin + digest;
  }
  const auto query_start = url.find('?', authority_end);
  const auto fragment_start = url.find('#', authority_end);
  const auto path_end = std::min(query_start, fragment_start);
  const std::string path(url.substr(authority_end, path_end - authority_end));
  std::string result = origin;
  result += debug_allowlist_.paths.contains(path) ? path : "/[redacted]";
  if (query_start != std::string_view::npos && query_start < fragment_start) {
    auto query = url.substr(query_start + 1, fragment_start - query_start - 1);
    result += '?';
    while (!query.empty()) {
      const auto end = query.find('&');
      const auto pair = query.substr(0, end);
      const auto equal = pair.find('=');
      const std::string name(pair.substr(0, equal));
      const std::string value(equal == std::string_view::npos
                                  ? std::string_view{}
                                  : pair.substr(equal + 1));
      const auto allowed = debug_allowlist_.query_values.find(name);
      if (!Prohibited(name) && allowed != debug_allowlist_.query_values.end() &&
          allowed->second.contains(value) &&
          !token_headers_.contains(Normalize(name))) {
        result += name + "=" + value;
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
  if (fragment_start != std::string_view::npos) {
    const std::string fragment(url.substr(fragment_start + 1));
    result += debug_allowlist_.fragments.contains(fragment) ? "#" + fragment
                                                            : "#[redacted]";
  }
  return result + digest;
}

std::string UrlRedactor::RedactText(std::string_view text) const {
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
    auto end = text.find_first_of(" \t\r\n\"'<>", delimiter + 3);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    result += RedactUrl(text.substr(start, end - start));
    offset = end;
  }
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

}
