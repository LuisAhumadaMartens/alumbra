#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "penumbra/desktop/av_sync_capture.h"
#include "penumbra/desktop/desktop_port.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"
#include "umbra/player/player_access.h"

namespace {

class TaskQueue final {
 public:
  void Post(std::function<void()> task, std::chrono::microseconds delay) {
    const std::scoped_lock lock(mutex_);
    tasks_.push({std::chrono::steady_clock::now() + delay, next_order_++,
                 std::move(task)});
    condition_.notify_one();
  }

  bool RunReady() {
    std::function<void()> task;
    {
      const std::scoped_lock lock(mutex_);
      if (tasks_.empty() ||
          tasks_.top().due > std::chrono::steady_clock::now()) {
        return false;
      }
      task = tasks_.top().task;
      tasks_.pop();
    }
    task();
    return true;
  }

  void Wait() {
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, std::chrono::milliseconds(2));
  }

 private:
  struct Entry {
    std::chrono::steady_clock::time_point due;
    uint64_t order = 0;
    std::function<void()> task;
  };

  struct Later {
    bool operator()(const Entry& left, const Entry& right) const {
      if (left.due != right.due) {
        return left.due > right.due;
      }
      return left.order > right.order;
    }
  };

  std::mutex mutex_;
  std::condition_variable condition_;
  std::priority_queue<Entry, std::vector<Entry>, Later> tasks_;
  uint64_t next_order_ = 0;
};

bool WriteCapture(const std::filesystem::path& path,
                  const penumbra::desktop::AvSyncCapture& capture) {
  const auto video = capture.Video();
  const auto audio = capture.Audio();
  if (video.empty() || audio.empty() || capture.SampleRate() <= 0) {
    return false;
  }
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << R"({
  "schema": 1,
  "metadata": {
    "route": "headless desktop capture",
    "display": "offscreen GPU presenter"
  },
  "calibration": {"offset_ms": 0.0, "uncertainty_ms": 1.0},
  "frames": [
)";
  for (size_t index = 0; index < video.size(); ++index) {
    const auto& frame = video[index];
    output << "    {\"media_time_ms\": "
           << static_cast<double>(frame.media_time_us) / 1000.0
           << R"(, "present_ms": )" << frame.present_ms << R"(, "luma": )"
           << frame.luma << R"(, "phase": ")"
           << (frame.media_time_us < 5'000'000 ? "startup" : "steady_state")
           << R"("})" << (index + 1 == video.size() ? "\n" : ",\n");
  }
  output << R"(  ],
  "audio": {
    "sample_rate_hz": )"
         << capture.SampleRate() << R"(,
    "first_sample_position": 0,
    "first_sample_capture_ms": )"
         << capture.FirstAudioCaptureMs() << R"(,
    "windows": [
)";
  for (size_t index = 0; index < audio.size(); ++index) {
    const auto& window = audio[index];
    output << R"(      {"sample_position": )" << window.sample_position
           << R"(, "level": )" << window.level << "}"
           << (index + 1 == audio.size() ? "\n" : ",\n");
  }
  output << R"(    ]
  }
}
)";
  return output.good();
}

int RunCapture(int argc, char** argv) {
  const std::span<char*> arguments(argv, static_cast<size_t>(argc));
  const bool require_gpu =
      argc == 4 && std::string_view(arguments[1]) == "--require-gpu";
  if (argc != (require_gpu ? 4 : 3)) {
    std::cerr << "usage: av_sync_capture_runner [--require-gpu] <clip> "
                 "<capture.json>\n";
    return 2;
  }
  const size_t clip_index = require_gpu ? 2 : 1;
  const size_t capture_index = clip_index + 1;
  TaskQueue tasks;
  const std::thread::id main_thread = std::this_thread::get_id();
  umbra::player::PlayerOptions options;
  options.headless = true;
  options.post_task = [&tasks](std::function<void()> task,
                               std::chrono::microseconds delay) {
    tasks.Post(std::move(task), delay);
  };
  options.runs_on_sequence = [main_thread] {
    return std::this_thread::get_id() == main_thread;
  };
  auto capture = std::make_shared<penumbra::desktop::AvSyncCapture>();
  penumbra::desktop::DesktopPortOptions port_options;
  port_options.headless = true;
  port_options.av_sync_capture = capture;
  auto port = penumbra::desktop::CreatePort(std::move(port_options));
  if (!port.ok()) {
    std::cerr << port.status() << '\n';
    return 1;
  }
  auto player = umbra::player::internal::PlayerAccess::CreateWithPort(
      std::move(options), std::move(*port));
  if (player == nullptr) {
    std::cerr << "could not create the capture player\n";
    return 1;
  }
  player->set_src(
      "file://" +
      std::filesystem::absolute(arguments[clip_index]).generic_string());
  player->Load();
  player->Play([](const umbra::player::PlayResult&) {});
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!player->ended() && !player->error().has_value() &&
         std::chrono::steady_clock::now() < deadline) {
    while (tasks.RunReady()) {
    }
    player->DispatchEvents();
    tasks.Wait();
  }
  player->DispatchEvents();
  if (player->error().has_value()) {
    const std::string capture_failure = capture->Failure();
    if (!capture_failure.empty()) {
      if (require_gpu) {
        std::cerr << "desktop GPU capture is required but unavailable: "
                  << capture_failure << '\n';
        return 1;
      }
      std::cerr << "SKIP: desktop GPU capture is unavailable: "
                << capture_failure << '\n';
      return 77;
    }
    std::cerr << "playback failed: " << player->error()->message() << '\n';
    for (const auto& [key, value] : player->error()->detail().context) {
      std::cerr << key << ": " << value << '\n';
    }
    return 1;
  }
  if (!player->ended()) {
    std::cerr << "playback did not reach the end of the clip\n";
    return 1;
  }
  player.reset();
  if (!WriteCapture(arguments[capture_index], *capture)) {
    std::cerr << "capture did not contain video and audio observations\n";
    return 1;
  }
  return 0;
}

}

int main(int argc, char** argv) {
  try {
    return RunCapture(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "capture runner failed: " << error.what() << '\n';
    return 1;
  }
}
