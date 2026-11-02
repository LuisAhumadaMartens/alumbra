#ifndef UMBRA_PIPELINE_EVENT_QUEUE_H_
#define UMBRA_PIPELINE_EVENT_QUEUE_H_

#include <deque>
#include <functional>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "umbra/base/lock.h"
#include "umbra/pipeline/media_events.h"

namespace umbra::pipeline {

class EventQueue {
 public:
  using NotifyHook = std::function<void()>;

  EventQueue() = default;
  EventQueue(const EventQueue&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;
  ~EventQueue() = default;

  void SetNotifyHook(NotifyHook hook);
  void Push(QueueEntry entry);
  [[nodiscard]] std::vector<QueueEntry> Drain();
  void DropEvents();
  [[nodiscard]] bool empty() const;

 private:
  mutable base::Lock lock_;
  std::deque<QueueEntry> entries_ ABSL_GUARDED_BY(lock_);
  NotifyHook hook_ ABSL_GUARDED_BY(lock_);
};

}

#endif
