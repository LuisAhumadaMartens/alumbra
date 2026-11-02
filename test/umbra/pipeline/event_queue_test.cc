#include "umbra/pipeline/event_queue.h"

#include <cstddef>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "umbra/pipeline/media_events.h"

namespace umbra::pipeline {
namespace {

MediaEvent Event(MediaEventType type) { return {.type = type, .state = {}}; }

TEST(EventQueueTest, NotifiesOnceWhenTheQueueBecomesNonEmpty) {
  EventQueue queue;
  int notifications = 0;
  queue.SetNotifyHook([&notifications] { ++notifications; });
  queue.Push(Event(MediaEventType::kPlay));
  queue.Push(Event(MediaEventType::kPlaying));
  EXPECT_EQ(notifications, 1);
  EXPECT_EQ(queue.Drain().size(), 2);
  queue.Push(Event(MediaEventType::kPause));
  EXPECT_EQ(notifications, 2);
}

TEST(EventQueueTest, DrainPreservesOrderAndEmptiesTheQueue) {
  EventQueue queue;
  queue.Push(Event(MediaEventType::kLoadStart));
  queue.Push(Event(MediaEventType::kLoadedMetadata));
  auto drained = queue.Drain();
  ASSERT_EQ(drained.size(), 2);
  EXPECT_EQ(std::get<MediaEvent>(drained[0]).type, MediaEventType::kLoadStart);
  EXPECT_EQ(std::get<MediaEvent>(drained[1]).type,
            MediaEventType::kLoadedMetadata);
  EXPECT_TRUE(queue.empty());
}

TEST(EventQueueTest, DropEventsKeepsPlayCompletions) {
  EventQueue queue;
  queue.Push(Event(MediaEventType::kTimeUpdate));
  queue.Push(PlayCompletion{.callback = {}, .result = {}});
  queue.Push(Event(MediaEventType::kPause));
  queue.DropEvents();
  auto drained = queue.Drain();
  ASSERT_EQ(drained.size(), 1);
  EXPECT_TRUE(std::holds_alternative<PlayCompletion>(drained[0]));
}

TEST(EventQueueTest, HookMayDrainWithoutDeadlock) {
  EventQueue queue;
  size_t drained = 0;
  queue.SetNotifyHook([&] { drained += queue.Drain().size(); });
  queue.Push(Event(MediaEventType::kPlay));
  EXPECT_EQ(drained, 1);
  EXPECT_TRUE(queue.empty());
}

}
}
