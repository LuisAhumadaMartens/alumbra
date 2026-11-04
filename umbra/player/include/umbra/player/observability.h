#ifndef UMBRA_PLAYER_OBSERVABILITY_H_
#define UMBRA_PLAYER_OBSERVABILITY_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace umbra::player {

enum class LogLevel : std::uint8_t {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

enum class TraceCategory : std::uint8_t {
  kNet,
  kManifest,
  kAbr,
  kBuffer,
  kDemux,
  kDecode,
  kRender,
  kAudio,
  kText,
  kAds,
  kDrm,
  kSettings,
  kPlayer,
  kPort,
};

enum class TracePhase : std::uint8_t {
  kBegin,
  kEnd,
  kInstant,
  kCounter,
};

struct LogRecord {
  TraceCategory category = TraceCategory::kPlayer;
  LogLevel level = LogLevel::kInfo;
  std::string message;
  std::map<std::string, std::string> fields;
};

struct TraceRecord {
  TraceCategory category = TraceCategory::kPlayer;
  TracePhase phase = TracePhase::kInstant;
  std::string name;
  std::int64_t value = 0;
};

using LogSink = std::function<void(const LogRecord&)>;
using TraceSink = std::function<void(const TraceRecord&)>;

}

#endif
