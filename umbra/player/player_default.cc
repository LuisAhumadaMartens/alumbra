#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/default_port.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/player/player.h"
#include "umbra/player/player_access.h"

namespace umbra::player {
namespace {

#ifndef ALUMBRA_DEFAULT_DESKTOP_PORT
class UnavailableFactory final : public penumbra::PlayerFactory {
 public:
  [[nodiscard]] absl::StatusOr<penumbra::PortLimits> GetLimits()
      const override {
    return absl::UnimplementedError("no Penumbra port is linked");
  }
  absl::StatusOr<std::unique_ptr<penumbra::Player>> CreatePlayer(
      [[maybe_unused]] penumbra::PlayerConfig config,
      [[maybe_unused]] std::shared_ptr<penumbra::PlayerCallbacks> callbacks)
      override {
    return absl::UnimplementedError("no Penumbra port is linked");
  }
};
#endif

class PostedRunner final : public penumbra::ControlTaskRunner {
 public:
  explicit PostedRunner(PlayerOptions options)
      : post_(std::move(options.post_task)),
        on_sequence_(std::move(options.runs_on_sequence)) {}

  absl::Status PostTask(Task task, absl::Duration delay) override {
    auto shared = std::make_shared<Task>(std::move(task));
    post_([shared] { std::move (*shared)(); },
          absl::ToChronoMicroseconds(delay));
    return absl::OkStatus();
  }

  [[nodiscard]] bool RunsTasksInCurrentSequence() const override {
    return !on_sequence_ || on_sequence_();
  }

 private:
  std::function<void(std::function<void()>, std::chrono::microseconds)> post_;
  std::function<bool()> on_sequence_;
};

}

std::unique_ptr<Player> Player::Create(PlayerOptions options) {
  if (!options.post_task) {
    return nullptr;
  }
  auto control = std::make_shared<PostedRunner>(options);
  internal::PlayerDependencies dependencies;
#ifdef ALUMBRA_DEFAULT_DESKTOP_PORT
  auto created = penumbra::CreateDefaultPort(options.headless);
  if (!created.ok()) {
    return nullptr;
  }
  auto port = std::shared_ptr<penumbra::Port>(std::move(*created));
  dependencies.factory = std::shared_ptr<penumbra::PlayerFactory>(
      port, static_cast<penumbra::PlayerFactory*>(port.get()));
  dependencies.capabilities = std::shared_ptr<penumbra::CapabilityProvider>(
      port, static_cast<penumbra::CapabilityProvider*>(port.get()));
#else
  dependencies.factory = std::make_shared<UnavailableFactory>();
#endif
  return internal::PlayerAccess::Create(std::move(options), std::move(control),
                                        std::move(dependencies));
}

}
