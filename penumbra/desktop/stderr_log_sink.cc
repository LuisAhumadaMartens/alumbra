#include "penumbra/desktop/stderr_log_sink.h"

#include <mutex>
#include <string_view>

#include "absl/status/status.h"
#include "penumbra/interface/port_types.h"

namespace penumbra::desktop {
namespace {

std::string_view LevelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kVerbose:
      return "V";
    case LogLevel::kDebug:
      return "D";
    case LogLevel::kInfo:
      return "I";
    case LogLevel::kWarning:
      return "W";
    case LogLevel::kError:
      return "E";
  }
  return "?";
}

}

absl::Status StderrLogSink::Log(LogLevel level,
                                std::string_view redacted_message) {
  const std::scoped_lock lock(mutex_);
  stream_ << LevelTag(level) << ' ' << redacted_message << '\n';
  stream_.flush();
  if (!stream_.good()) {
    return absl::UnavailableError("stderr log sink write failed");
  }
  return absl::OkStatus();
}

}
