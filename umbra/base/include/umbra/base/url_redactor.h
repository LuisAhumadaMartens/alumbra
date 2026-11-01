#ifndef UMBRA_BASE_URL_REDACTOR_H_
#define UMBRA_BASE_URL_REDACTOR_H_

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace umbra::base {

enum class UrlDiagnostics { kOriginAndDigest, kDebugFullUrl };

struct DebugUrlAllowlist {
  std::set<std::string> paths;
  std::map<std::string, std::set<std::string>> query_values;
  std::set<std::string> fragments;
};

class UrlRedactor {
 public:
  UrlRedactor();
  explicit UrlRedactor(std::array<std::uint64_t, 2> playback_key,
                       std::set<std::string> token_headers = {},
                       UrlDiagnostics mode = UrlDiagnostics::kOriginAndDigest,
                       DebugUrlAllowlist debug_allowlist = {});
  UrlRedactor(const UrlRedactor&) = delete;
  UrlRedactor& operator=(const UrlRedactor&) = delete;

  [[nodiscard]] std::string RedactUrl(std::string_view url) const;
  [[nodiscard]] std::string RedactText(std::string_view text) const;
  [[nodiscard]] std::string RedactField(std::string_view key,
                                        std::string_view value) const;

 private:
  std::array<std::uint64_t, 2> playback_key_;
  std::set<std::string> token_headers_;
  UrlDiagnostics mode_ = UrlDiagnostics::kOriginAndDigest;
  DebugUrlAllowlist debug_allowlist_;
};

}

#endif
