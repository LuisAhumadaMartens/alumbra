#ifndef PENUMBRA_INTERFACE_PORT_H_
#define PENUMBRA_INTERFACE_PORT_H_

#include <string_view>

#include "penumbra/interface/components.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/log_sink.h"
#include "penumbra/interface/stream_provider.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra {

class MemoryPressureObserver {
 public:
  virtual ~MemoryPressureObserver() = default;
  virtual absl::Status OnMemoryPressure(MemoryPressure pressure) = 0;
};

class CaptionPreferencesObserver {
 public:
  virtual ~CaptionPreferencesObserver() = default;
  virtual void OnCaptionPreferencesChanged(
      const CaptionPreferences& preferences) = 0;
};

class CaptionPreferenceProvider {
 public:
  virtual ~CaptionPreferenceProvider() = default;
  [[nodiscard]] virtual absl::StatusOr<CaptionPreferences>
  GetCaptionPreferences() const = 0;
  virtual absl::Status SetCaptionPreferencesObserver(
      std::shared_ptr<CaptionPreferencesObserver> observer) = 0;
};

class Port : public PlayerFactory,
             public CapabilityProvider,
             public ComponentFactory,
             public TaskRunnerProvider,
             public HttpTransportProvider,
             public StreamProviderCatalogue,
             public LogSink,
             public CaptionPreferenceProvider {
 public:
  [[nodiscard]] virtual BufferBudgets GetBufferBudgets() const = 0;
  virtual absl::Status SetMemoryPressureObserver(
      std::shared_ptr<MemoryPressureObserver> observer) = 0;
};

}

#endif
