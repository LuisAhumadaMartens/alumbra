#include "umbra/pipeline/playback_state.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>
#include <utility>

namespace umbra::pipeline {
namespace {

using S = PlaybackState;

constexpr std::array kEdges{
    std::pair{S::kIdle, S::kLoading},
    std::pair{S::kLoading, S::kPrerolling},
    std::pair{S::kLoading, S::kError},
    std::pair{S::kPrerolling, S::kReady},
    std::pair{S::kPrerolling, S::kPlaying},
    std::pair{S::kPrerolling, S::kSeeking},
    std::pair{S::kPrerolling, S::kError},
    std::pair{S::kReady, S::kPlaying},
    std::pair{S::kReady, S::kSeeking},
    std::pair{S::kReady, S::kBuffering},
    std::pair{S::kReady, S::kEnded},
    std::pair{S::kReady, S::kError},
    std::pair{S::kPlaying, S::kPaused},
    std::pair{S::kPlaying, S::kSeeking},
    std::pair{S::kPlaying, S::kBuffering},
    std::pair{S::kPlaying, S::kEnded},
    std::pair{S::kPlaying, S::kError},
    std::pair{S::kPaused, S::kPlaying},
    std::pair{S::kPaused, S::kSeeking},
    std::pair{S::kPaused, S::kBuffering},
    std::pair{S::kPaused, S::kEnded},
    std::pair{S::kPaused, S::kError},
    std::pair{S::kSeeking, S::kPrerolling},
    std::pair{S::kSeeking, S::kPlaying},
    std::pair{S::kSeeking, S::kPaused},
    std::pair{S::kSeeking, S::kBuffering},
    std::pair{S::kSeeking, S::kError},
    std::pair{S::kBuffering, S::kPlaying},
    std::pair{S::kBuffering, S::kPaused},
    std::pair{S::kBuffering, S::kSeeking},
    std::pair{S::kBuffering, S::kEnded},
    std::pair{S::kBuffering, S::kError},
    std::pair{S::kEnded, S::kSeeking},
    std::pair{S::kEnded, S::kError},
};

constexpr auto BuildTable() {
  std::array<std::array<bool, kPlaybackStateCount>, kPlaybackStateCount>
      table{};
  for (const auto& [from, to] : kEdges) {
    table.at(static_cast<size_t>(from)).at(static_cast<size_t>(to)) = true;
  }
  for (size_t from = 1; from < kPlaybackStateCount; ++from) {
    table.at(from).at(static_cast<size_t>(S::kIdle)) = true;
  }
  return table;
}

constexpr auto kTable = BuildTable();

}

std::string_view PlaybackStateName(PlaybackState state) {
  switch (state) {
    case PlaybackState::kIdle:
      return "idle";
    case PlaybackState::kLoading:
      return "loading";
    case PlaybackState::kPrerolling:
      return "prerolling";
    case PlaybackState::kReady:
      return "ready";
    case PlaybackState::kPlaying:
      return "playing";
    case PlaybackState::kPaused:
      return "paused";
    case PlaybackState::kSeeking:
      return "seeking";
    case PlaybackState::kBuffering:
      return "buffering";
    case PlaybackState::kEnded:
      return "ended";
    case PlaybackState::kError:
      return "error";
  }
  return "unknown";
}

bool IsLegalTransition(PlaybackState from, PlaybackState to) {
  return kTable.at(static_cast<size_t>(from)).at(static_cast<size_t>(to));
}

StateTracker::StateTracker(Observer observer)
    : observer_(std::move(observer)) {}

void StateTracker::Transition(PlaybackState to) {
  assert(IsLegalTransition(state_, to));
  const StateTransition transition{.from = state_, .to = to};
  state_ = to;
  if (observer_) {
    observer_(transition);
  }
}

}
