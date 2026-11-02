#ifndef UMBRA_PIPELINE_PLAYBACK_STATE_H_
#define UMBRA_PIPELINE_PLAYBACK_STATE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace umbra::pipeline {

enum class PlaybackState : uint8_t {
  kIdle,
  kLoading,
  kPrerolling,
  kReady,
  kPlaying,
  kPaused,
  kSeeking,
  kBuffering,
  kEnded,
  kError,
};

inline constexpr size_t kPlaybackStateCount = 10;

inline constexpr std::array<PlaybackState, kPlaybackStateCount>
    kAllPlaybackStates{
        PlaybackState::kIdle,       PlaybackState::kLoading,
        PlaybackState::kPrerolling, PlaybackState::kReady,
        PlaybackState::kPlaying,    PlaybackState::kPaused,
        PlaybackState::kSeeking,    PlaybackState::kBuffering,
        PlaybackState::kEnded,      PlaybackState::kError,
    };

std::string_view PlaybackStateName(PlaybackState state);

bool IsLegalTransition(PlaybackState from, PlaybackState to);

struct StateTransition {
  PlaybackState from = PlaybackState::kIdle;
  PlaybackState to = PlaybackState::kIdle;
};

class StateTracker {
 public:
  using Observer = std::function<void(const StateTransition&)>;

  explicit StateTracker(Observer observer = {});

  [[nodiscard]] PlaybackState GetState() const { return state_; }
  void Transition(PlaybackState to);

 private:
  PlaybackState state_ = PlaybackState::kIdle;
  Observer observer_;
};

}

#endif
