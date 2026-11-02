#ifndef PENUMBRA_INTERFACE_PORT_H_
#define PENUMBRA_INTERFACE_PORT_H_

#include <string_view>

#include "penumbra/interface/components.h"
#include "penumbra/interface/log_sink.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra {

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
