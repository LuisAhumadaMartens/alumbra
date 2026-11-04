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

#ifdef ALUMBRA_CLI_DESKTOP_UI
#include "alumbra/desktop_ui/desktop_window.h"
#endif

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

std::vector<std::string> SplitCategories(std::string_view categories) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start < categories.size()) {
    const std::size_t separator = categories.find(',', start);
    const std::size_t end =
        separator == std::string_view::npos ? categories.size() : separator;
    result.emplace_back(categories.substr(start, end - start));
    if (separator == std::string_view::npos) {
      break;
    }
    start = separator + 1;
  }
  return result;
}

#ifdef ALUMBRA_CLI_DESKTOP_UI
#endif

int RunPlayback(umbra::player::Player& player, TaskQueue& queue,
                const alumbra::cli::Options& options
#ifdef ALUMBRA_CLI_DESKTOP_UI
                ,
                alumbra::desktop_ui::DesktopWindow* desktop_window
#endif
) {
  bool started = false;
  while (!player.ended()) {
    queue.RunReady();
    player.DispatchEvents();
#ifdef ALUMBRA_CLI_DESKTOP_UI
    if (desktop_window != nullptr) {
      if (!desktop_window->PumpEvents()) {
        return 0;
      }
      static_cast<void>(desktop_window->RenderFrame());
    }
#endif
    if (!started &&
        player.ready_state() >= umbra::player::ReadyState::kHaveMetadata) {
      if (options.start_seconds) {
        player.set_current_time(*options.start_seconds);
      }
      started = true;
      player.Play([](const auto&) {});
    }
    if (const auto error = player.error()) {
      std::cerr << error->message() << '\n';
      return ExitForError(error->code());
    }
#ifdef ALUMBRA_CLI_DESKTOP_UI
    if (!options.headless && desktop_window != nullptr) {
      continue;
    } else {
      queue.Wait();
    }
#else
    queue.Wait();
#endif
  }
  return 0;
}

int Run(const alumbra::cli::Options& options) {
#ifndef ALUMBRA_CLI_DESKTOP_UI
  if (!options.headless) {
    std::cerr << "windowed playback requires the desktop UI build\n";
    return 2;
  }
#endif
  TaskQueue queue;
  std::unique_ptr<umbra::player::Player> player;
#ifdef ALUMBRA_CLI_DESKTOP_UI
  std::unique_ptr<alumbra::desktop_ui::DesktopWindow> desktop_window;
#endif
  umbra::player::PlayerOptions player_options;
  player_options.headless = options.headless;
  player_options.log_sink = [](const umbra::player::LogRecord& record) {
    std::cerr << record.message << '\n';
  };
  player_options.trace_sink = [](const umbra::player::TraceRecord& record) {
    std::cerr << record.name << '\n';
  };
  player_options.post_task = [&queue](std::function<void()> task,
                                      std::chrono::microseconds delay) {
    queue.Post(std::move(task), delay);
  };
  player_options.runs_on_sequence = [&queue] {
    return queue.RunsOnCurrentSequence();
  };
  player_options.on_events_available = [&queue] { queue.Post([] {}, {}); };
  player = umbra::player::Player::Create(std::move(player_options));
  if (!player) {
    std::cerr << "could not create player\n";
    return 1;
  }
  const alumbra::cli::AssignmentParseResult assignments =
      alumbra::cli::ParseAssignments(options);
  if (!assignments.error.empty()) {
    std::cerr << assignments.error << '\n';
    return 2;
  }
  for (const auto& assignment : assignments.assignments) {
    const auto error = player->settings().Set(assignment.key, assignment.value);
    if (error.has_value()) {
      const auto reason = error->context.find("reason");
      std::cerr << (reason == error->context.end() ? "invalid setting"
                                                   : reason->second)
                << '\n';
      return 2;
    }
  }
  const auto log_level_error =
      player->settings().Set("observability.log.level", options.log_level);
  const auto trace_categories_error =
      player->settings().Set("observability.trace.categories",
                             SplitCategories(options.trace_categories));
  if (log_level_error.has_value() || trace_categories_error.has_value()) {
    std::cerr << "invalid observability setting\n";
    return 2;
  }
  queue.RunReady();
#ifdef ALUMBRA_CLI_DESKTOP_UI
  if (!options.headless) {
    desktop_window = alumbra::desktop_ui::DesktopWindow::Create(*player);
    if (desktop_window == nullptr) {
      std::cerr << "could not create the desktop window\n";
      return 1;
    }
  }
#endif
  player->set_src(ResolveSource(options.source));
  player->Load();
#ifdef ALUMBRA_CLI_DESKTOP_UI
  const int exit_code =
      RunPlayback(*player, queue, options, desktop_window.get());
#else
  const int exit_code = RunPlayback(*player, queue, options);
#endif
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
  player.reset();
  desktop_window.reset();
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
    return Run(parsed.options);
  } catch (...) {
    std::cerr << "unexpected command-line failure\n";
    return 1;
  }
}
