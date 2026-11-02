#ifndef PENUMBRA_INTERFACE_LOG_SINK_H_
#define PENUMBRA_INTERFACE_LOG_SINK_H_

#include <string_view>

#include "absl/status/status.h"
#include "penumbra/interface/port_types.h"

namespace penumbra {

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual absl::Status Log(LogLevel level,
                           std::string_view redacted_message) = 0;
};

}

#endif
