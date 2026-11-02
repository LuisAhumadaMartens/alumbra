#include "umbra/source/file_url.h"

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace umbra::source {
namespace {

bool IsUnreserved(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
         c == '~' || c == '/';
}

}

absl::StatusOr<std::string> FileUrlFromPath(std::string_view absolute_path) {
  if (absolute_path.empty() || absolute_path.front() != '/') {
    return absl::InvalidArgumentError("the path must be absolute");
  }
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string url = "file://";
  for (const char c : absolute_path) {
    if (c == '\0') {
      return absl::InvalidArgumentError("the path contains a NUL byte");
    }
    if (IsUnreserved(c)) {
      url.push_back(c);
    } else {
      const auto byte = static_cast<unsigned char>(c);
      url.push_back('%');
      url.push_back(kHex[byte >> 4]);
      url.push_back(kHex[byte & 0x0F]);
    }
  }
  return url;
}

}
