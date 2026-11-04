#ifndef UMBRA_PLAYER_PLAYER_ACCESS_H_
#define UMBRA_PLAYER_PLAYER_ACCESS_H_

#include <functional>
#include <memory>
#include <string_view>

#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/observability/metrics.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/player.h"

namespace umbra::player::internal {

using SourceResolver = std::function<std::unique_ptr<pipeline::SampleSource>(
    std::string_view url, const std::vector<std::string>& preferred_languages,
    bool prefer_descriptions)>;

struct PlayerDependencies {
  std::shared_ptr<penumbra::PlayerFactory> factory;
  std::shared_ptr<penumbra::CapabilityProvider> capabilities;
  std::shared_ptr<penumbra::Port> port;
  SourceResolver resolve_source;
};

class PlayerAccess {
 public:
  static std::unique_ptr<Player> Create(
      PlayerOptions options,
      std::shared_ptr<penumbra::ControlTaskRunner> control,
      PlayerDependencies dependencies);
  static void ReportMetric(Player& player,
                           const observability::MetricsEvent& event);
  static std::shared_ptr<penumbra::Port> GetPort(Player& player);
};

}

#endif
