#ifndef UMBRA_ADS_BEACON_DISPATCHER_H_
#define UMBRA_ADS_BEACON_DISPATCHER_H_

#include <functional>
#include <memory>
#include <string>

#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "umbra/base/task_runner.h"

namespace umbra::ads {

class BeaconDispatcher {
 public:
  using DebugLogger = std::function<void(bool, int)>;

  BeaconDispatcher(std::shared_ptr<penumbra::HttpTransport> transport,
                   std::shared_ptr<base::SequencedTaskRunner> network_runner,
                   absl::Duration timeout, DebugLogger logger);
  ~BeaconDispatcher();
  BeaconDispatcher(const BeaconDispatcher&) = delete;
  BeaconDispatcher& operator=(const BeaconDispatcher&) = delete;

  void Dispatch(std::string url);

 private:
  struct State;
  std::shared_ptr<State> state_;
};

}

#endif
