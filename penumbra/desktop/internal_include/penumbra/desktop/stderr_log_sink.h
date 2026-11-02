#ifndef PENUMBRA_DESKTOP_STDERR_LOG_SINK_H_
#define PENUMBRA_DESKTOP_STDERR_LOG_SINK_H_

#include <iostream>
#include <mutex>
#include <ostream>
#include <string_view>

#include "absl/status/status.h"
#include "penumbra/interface/log_sink.h"
#include "penumbra/interface/port_types.h"

namespace penumbra::desktop {

class StderrLogSink final : public LogSink {
 public:
  StderrLogSink() = default;
  explicit StderrLogSink(std::ostream& stream) : stream_(stream) {}
  StderrLogSink(const StderrLogSink&) = delete;
  StderrLogSink& operator=(const StderrLogSink&) = delete;
  ~StderrLogSink() override = default;

  absl::Status Log(LogLevel level, std::string_view redacted_message) override;

 private:
  std::ostream& stream_ = std::cerr;
  std::mutex mutex_;
};

}

#endif
