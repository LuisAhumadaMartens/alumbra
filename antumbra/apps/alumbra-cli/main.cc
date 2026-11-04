#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "alumbra/cli/args.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/player.h"
#include "umbra/player/statistics.h"

namespace {

class TaskQueue {
 public:
  void Post(std::function<void()> task, std::chrono::microseconds delay) {
    const std::scoped_lock lock(mutex_);
    tasks_.emplace(
        Clock::now() + std::max(delay, std::chrono::microseconds::zero()),
        std::move(task));
    condition_.notify_one();
  }

  [[nodiscard]] bool RunsOnCurrentSequence() const {
    return std::this_thread::get_id() == sequence_;
  }

  void Wait() {
    std::unique_lock lock(mutex_);
    if (tasks_.empty()) {
      condition_.wait(lock);
      return;
    }
    condition_.wait_until(lock, tasks_.begin()->first);
  }

  void RunReady() {
    for (;;) {
      std::function<void()> task;
      {
        const std::scoped_lock lock(mutex_);
        if (tasks_.empty() || tasks_.begin()->first > Clock::now()) {
          return;
        }
        task = std::move(tasks_.begin()->second);
        tasks_.erase(tasks_.begin());
      }
      task();
    }
  }

 private:
  using Clock = std::chrono::steady_clock;
  std::thread::id sequence_ = std::this_thread::get_id();
  std::mutex mutex_;
  std::condition_variable condition_;
  std::multimap<Clock::time_point, std::function<void()>> tasks_;
};

std::string FileUrl(const std::string& path) {
  std::string url = "file://";
  const std::string absolute = std::filesystem::absolute(path).generic_string();
  constexpr std::string_view kHex = "0123456789ABCDEF";
  for (const unsigned char character : absolute) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '/' ||
        character == '-' || character == '_' || character == '.' ||
        character == '~') {
      url.push_back(static_cast<char>(character));
    } else {
      url.push_back('%');
      url.push_back(kHex.at(character >> 4));
      url.push_back(kHex.at(character & 0x0f));
    }
  }
  return url;
}

std::string ResolveSource(const std::string& source) {
  if (source.find("://") != std::string::npos) {
    return source;
  }
  return FileUrl(source);
}

int ExitForError(umbra::player::MediaErrorCode code) {
  return 10 + static_cast<int>(code);
}

int Run(const alumbra::cli::Options& options) {
  if (!options.headless) {
    std::cerr << "windowed playback is not available in this build\n";
    return 2;
  }
  TaskQueue queue;
  umbra::player::PlayerOptions player_options;
  player_options.headless = true;
  player_options.post_task = [&queue](std::function<void()> task,
                                      std::chrono::microseconds delay) {
    queue.Post(std::move(task), delay);
  };
  player_options.runs_on_sequence = [&queue] {
    return queue.RunsOnCurrentSequence();
  };
  player_options.on_events_available = [&queue] { queue.Post([] {}, {}); };
  std::unique_ptr<umbra::player::Player> player =
      umbra::player::Player::Create(std::move(player_options));
  if (!player) {
    std::cerr << "could not create player\n";
    return 1;
  }
  bool started = false;
  int exit_code = 0;
  player->set_src(ResolveSource(options.source));
  player->Load();
  while (!player->ended()) {
    queue.RunReady();
    player->DispatchEvents();
    if (!started &&
        player->ready_state() >= umbra::player::ReadyState::kHaveMetadata) {
      if (options.start_seconds) {
        player->set_current_time(*options.start_seconds);
      }
      started = true;
      player->Play([](const auto&) {});
    }
    if (const auto error = player->error()) {
      std::cerr << error->message() << '\n';
      exit_code = ExitForError(error->code());
      break;
    }
    queue.Wait();
  }
  if (options.stats) {
    const auto stats = player->Statistics();
    if (stats.average_playback_bitrate_kbps.value) {
      std::cout << "average_playback_bitrate_kbps="
                << *stats.average_playback_bitrate_kbps.value << '\n';
    }
    if (stats.dropped_frames.value) {
      std::cout << "dropped_frames=" << *stats.dropped_frames.value << '\n';
    }
  }
  return exit_code;
}

}

int main(int argc, char** argv) noexcept {
  try {
    std::vector<std::string> arguments;
    bool executable_name = true;
    for (const char* argument :
         std::span<char*>(argv, static_cast<std::size_t>(argc))) {
      if (executable_name) {
        executable_name = false;
      } else {
        arguments.emplace_back(argument);
      }
    }
    const alumbra::cli::ParseResult parsed =
        alumbra::cli::ParseArguments(arguments);
    if (!parsed.error.empty()) {
      std::cerr << parsed.error << '\n';
      return 2;
    }
    if (parsed.options.help) {
      std::cout << alumbra::cli::HelpText();
      return 0;
    }
    if (parsed.options.version) {
      std::cout << alumbra::cli::VersionText();
      return 0;
    }
    if (parsed.options.list_settings) {
      std::cout << alumbra::cli::ListSettings();
      return 0;
    }
    const std::string setting_error =
        alumbra::cli::ValidateAssignments(parsed.options);
    if (!setting_error.empty()) {
      std::cerr << setting_error << '\n';
      return 2;
    }
    if (!parsed.options.assignments.empty()) {
      std::cerr
          << "settings cannot yet be applied through the public player API\n";
      return 2;
    }
    return Run(parsed.options);
  } catch (...) {
    std::cerr << "unexpected command-line failure\n";
    return 1;
  }
}
