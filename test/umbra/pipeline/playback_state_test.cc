#include "umbra/pipeline/playback_state.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace umbra::pipeline {
namespace {

using S = PlaybackState;

const std::set<std::pair<S, S>>& ExpectedEdges() {
  static const std::set<std::pair<S, S>> edges{
      {S::kIdle, S::kLoading},       {S::kLoading, S::kPrerolling},
      {S::kLoading, S::kError},      {S::kPrerolling, S::kReady},
      {S::kPrerolling, S::kPlaying}, {S::kPrerolling, S::kSeeking},
      {S::kPrerolling, S::kError},   {S::kReady, S::kPlaying},
      {S::kReady, S::kSeeking},      {S::kReady, S::kBuffering},
      {S::kReady, S::kEnded},        {S::kReady, S::kError},
      {S::kPlaying, S::kPaused},     {S::kPlaying, S::kSeeking},
      {S::kPlaying, S::kBuffering},  {S::kPlaying, S::kEnded},
      {S::kPlaying, S::kError},      {S::kPaused, S::kPlaying},
      {S::kPaused, S::kSeeking},     {S::kPaused, S::kBuffering},
      {S::kPaused, S::kEnded},       {S::kPaused, S::kError},
      {S::kSeeking, S::kPrerolling}, {S::kSeeking, S::kPlaying},
      {S::kSeeking, S::kPaused},     {S::kSeeking, S::kBuffering},
      {S::kSeeking, S::kError},      {S::kBuffering, S::kPlaying},
      {S::kBuffering, S::kPaused},   {S::kBuffering, S::kSeeking},
      {S::kBuffering, S::kEnded},    {S::kBuffering, S::kError},
      {S::kEnded, S::kSeeking},      {S::kEnded, S::kError},
  };
  return edges;
}

TEST(PlaybackStateTest, TableMatchesTheDocumentedTransitions) {
  for (const S from : kAllPlaybackStates) {
    for (const S to : kAllPlaybackStates) {
      const bool expected = ExpectedEdges().contains({from, to}) ||
                            (to == S::kIdle && from != S::kIdle);
      EXPECT_EQ(IsLegalTransition(from, to), expected)
          << PlaybackStateName(from) << " -> " << PlaybackStateName(to);
    }
  }
}

TEST(PlaybackStateTest, EveryStateCanBeReset) {
  for (const S from : kAllPlaybackStates) {
    if (from != S::kIdle) {
      EXPECT_TRUE(IsLegalTransition(from, S::kIdle));
    }
  }
}

TEST(PlaybackStateTest, SelfTransitionsAreIllegal) {
  for (const S state : kAllPlaybackStates) {
    EXPECT_FALSE(IsLegalTransition(state, state));
  }
}

TEST(PlaybackStateTest, ErrorOnlyLeavesThroughIdle) {
  for (const S to : kAllPlaybackStates) {
    EXPECT_EQ(IsLegalTransition(S::kError, to), to == S::kIdle);
  }
}

TEST(PlaybackStateTest, NamesAreDistinct) {
  std::set<std::string> names;
  for (const S state : kAllPlaybackStates) {
    names.insert(std::string(PlaybackStateName(state)));
  }
  EXPECT_EQ(names.size(), kPlaybackStateCount);
}

TEST(StateTrackerTest, RecordsLegalTransitionsInOrder) {
  std::vector<StateTransition> seen;
  StateTracker tracker([&seen](const StateTransition& transition) {
    seen.push_back(transition);
  });
  tracker.Transition(S::kLoading);
  tracker.Transition(S::kPrerolling);
  tracker.Transition(S::kReady);
  ASSERT_EQ(seen.size(), 3);
  EXPECT_EQ(seen[0].from, S::kIdle);
  EXPECT_EQ(seen[0].to, S::kLoading);
  EXPECT_EQ(seen[2].from, S::kPrerolling);
  EXPECT_EQ(seen[2].to, S::kReady);
  EXPECT_EQ(tracker.GetState(), S::kReady);
}

TEST(StateTrackerDeathTest, IllegalTransitionIsAProgrammingError) {
  StateTracker tracker;
  EXPECT_DEBUG_DEATH(tracker.Transition(S::kPlaying), "");
}

TEST(StateTrackerDeathTest, LeavingErrorForAnythingButIdleIsRejected) {
  StateTracker tracker;
  tracker.Transition(S::kLoading);
  tracker.Transition(S::kError);
  EXPECT_DEBUG_DEATH(tracker.Transition(S::kPlaying), "");
}

}
}
