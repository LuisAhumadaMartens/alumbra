#ifndef PENUMBRA_INTERFACE_PORT_H_
#define PENUMBRA_INTERFACE_PORT_H_

#include <string_view>

#include "penumbra/interface/components.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra {

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual absl::Status Log(LogLevel level,
                           std::string_view redacted_message) = 0;
};

class MemoryPressureObserver {
 public:
  virtual ~MemoryPressureObserver() = default;
  virtual absl::Status OnMemoryPressure(MemoryPressure pressure) = 0;
};

class Port : public PlayerFactory,
             public CapabilityProvider,
             public ComponentFactory,
             public TaskRunnerProvider,
             public LogSink {
 public:
  virtual absl::Status SetMemoryPressureObserver(
      std::shared_ptr<MemoryPressureObserver> observer) = 0;
};

}

#endif
