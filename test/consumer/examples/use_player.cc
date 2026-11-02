#include <chrono>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

#include "umbra/base/error_catalogue.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
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

  const bool maybe = player->CanPlayType("video/mp4") ==
                     umbra::player::CanPlayTypeResult::kMaybe;

  umbra::player::PlayResult result;
  bool completed = false;
  player->set_src("https://example.test/video.mp4");
  player->Play([&](umbra::player::PlayResult done) {
    result = std::move(done);
    completed = true;
  });
  player->DispatchEvents();

  const auto error = player->error();
  if (!maybe || !completed || !error.has_value() || !player->paused() ||
      result.code != umbra::player::PlayResultCode::kNotSupported ||
      error->code() != umbra::player::MediaErrorCode::kSrcNotSupported ||
      error->message().rfind("ALUMBRA-", 0) != 0) {
    return 1;
  }
  std::cout << error->message() << "\n";
  return 0;
}
