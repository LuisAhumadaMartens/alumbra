#ifndef UMBRA_PLAYER_PLAYER_H_
#define UMBRA_PLAYER_PLAYER_H_

#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/observability.h"
#include "umbra/player/play_result.h"
#include "umbra/player/settings.h"
#include "umbra/player/statistics.h"
#include "umbra/player/text_track.h"
#include "umbra/player/time_ranges.h"

namespace umbra::player {

namespace internal {
class PlayerAccess;
}

enum class FastSeekSnap { kPrevious, kNearest };

struct PlayerOptions {
  std::function<void(std::function<void()> task,
                     std::chrono::microseconds delay)>
      post_task;
  std::function<bool()> runs_on_sequence;
  std::function<void()> on_events_available;
  std::function<bool()> allow_playback;
  std::function<bool()> qoe_enabled;
  LogSink log_sink;
  TraceSink trace_sink;
  bool headless = false;
  int preroll_ms = 500;
  FastSeekSnap fast_seek_snap = FastSeekSnap::kPrevious;
  int text_cue_memory_budget_kb = 1024;
  double minimum_audible_rate = 0.25;
  double maximum_audible_rate = 4.0;
};

class Player {
 public:
  static std::unique_ptr<Player> Create(PlayerOptions options);

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  ~Player();

  [[nodiscard]] const std::string& src() const;
  void set_src(std::string src);
  void Load();
  [[nodiscard]] CanPlayTypeResult CanPlayType(std::string_view type) const;
  [[nodiscard]] NetworkState network_state() const;

  void Play(PlayCallback done);
  void Pause();
  [[nodiscard]] bool paused() const;
  [[nodiscard]] bool ended() const;
  [[nodiscard]] bool autoplay() const;
  void set_autoplay(bool autoplay);
  [[nodiscard]] bool loop() const;
  void set_loop(bool loop);

  [[nodiscard]] double current_time() const;
  bool set_current_time(double seconds);
  bool FastSeek(double seconds);
  [[nodiscard]] bool seeking() const;
  [[nodiscard]] double duration() const;
  [[nodiscard]] TimeRanges buffered() const;
  [[nodiscard]] TimeRanges seekable() const;

  [[nodiscard]] ReadyState ready_state() const;
  [[nodiscard]] Preload preload() const;
  void set_preload(Preload preload);

  [[nodiscard]] double playback_rate() const;
  bool set_playback_rate(double rate);
  [[nodiscard]] double default_playback_rate() const;
  bool set_default_playback_rate(double rate);
  [[nodiscard]] bool preserves_pitch() const;
  void set_preserves_pitch(bool preserves_pitch);
  [[nodiscard]] double volume() const;
  bool set_volume(double volume);
  [[nodiscard]] bool muted() const;
  void set_muted(bool muted);

  [[nodiscard]] std::optional<MediaError> error() const;
  [[nodiscard]] TextTrackList& text_tracks();
  std::shared_ptr<TextTrack> AddTextTrack(TextTrackKind kind, std::string label,
                                          std::string language);

  [[nodiscard]] StatisticsSnapshot Statistics(
      bool include_unstable_debug = false) const;
  [[nodiscard]] Settings& settings();
  [[nodiscard]] const Settings& settings() const;

  void DispatchEvents();

  template <typename Event>
  [[nodiscard]] Subscription AddEventListener(
      std::function<void(const Event&)> listener) {
    return AddListener(Event::kType,
                       [listener = std::move(listener)](const std::any& event) {
                         listener(std::any_cast<const Event&>(event));
                       });
  }

 private:
  class Impl;
  friend class internal::PlayerAccess;

  explicit Player(std::unique_ptr<Impl> impl);

  Subscription AddListener(EventType type,
                           std::function<void(const std::any&)> listener);

  std::unique_ptr<Impl> impl_;
};

}

#endif
