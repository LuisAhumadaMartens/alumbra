#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/player.h"

int main() {
  std::vector<std::function<void()>> tasks;
  umbra::player::PlayerOptions options;
  options.post_task = [&tasks](std::function<void()> task,
                               std::chrono::microseconds) {
    tasks.push_back(std::move(task));
  };
  auto player = umbra::player::Player::Create(std::move(options));
  if (player == nullptr) {
    return 1;
  }

  std::vector<std::string> seen;
  umbra::player::MediaErrorCode code = umbra::player::MediaErrorCode::kDecode;
  const umbra::player::Subscription track_change =
      player->AddEventListener<umbra::player::AudioTracksChangeEvent>(
          [&](const umbra::player::AudioTracksChangeEvent&) {
            seen.emplace_back(umbra::player::AudioTracksChangeEvent::kName);
          });
  const umbra::player::Subscription load_start =
      player->AddEventListener<umbra::player::LoadStartEvent>(
          [&](const umbra::player::LoadStartEvent& event) {
            seen.emplace_back(umbra::player::LoadStartEvent::kName);
            if (event.state.network_state !=
                umbra::player::NetworkState::kLoading) {
              seen.emplace_back("unexpected network state");
            }
          });
  const umbra::player::Subscription failure =
      player->AddEventListener<umbra::player::ErrorEvent>(
          [&](const umbra::player::ErrorEvent& event) {
            seen.emplace_back(umbra::player::ErrorEvent::kName);
            code = event.error.code();
          });

  player->set_src("unsupported://example.test/video.mp4");
  player->DispatchEvents();

  if (seen != std::vector<std::string>{"loadstart", "error"} ||
      code != umbra::player::MediaErrorCode::kSrcNotSupported) {
    return 1;
  }
  std::cout << seen[0] << " then " << seen[1] << "\n";
  return 0;
}
