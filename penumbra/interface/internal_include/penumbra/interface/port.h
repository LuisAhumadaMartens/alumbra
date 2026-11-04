#ifndef PENUMBRA_INTERFACE_PORT_H_
#define PENUMBRA_INTERFACE_PORT_H_

#include <string_view>

#include "penumbra/interface/components.h"
#include "penumbra/interface/crypto_provider.h"
#include "penumbra/interface/fallback_demuxer.h"
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

class Port : public PlayerFactory,
             public CapabilityProvider,
             public ComponentFactory,
             public TaskRunnerProvider,
             public HttpTransportProvider,
             public StreamProviderCatalogue,
             public FallbackDemuxerProvider,
             public LogSink {
 public:
  [[nodiscard]] virtual std::shared_ptr<CryptoProvider> GetCryptoProvider() {
    return nullptr;
  }
  [[nodiscard]] virtual BufferBudgets GetBufferBudgets() const = 0;
  virtual absl::Status SetControlTaskRunner(
      const std::shared_ptr<ControlTaskRunner>& runner) {
    static_cast<void>(runner);
    return absl::UnimplementedError(
        "the port cannot use an embedder control task runner");
  }
  virtual absl::Status SetMemoryPressureObserver(
      std::shared_ptr<MemoryPressureObserver> observer) = 0;
};

}

#endif
