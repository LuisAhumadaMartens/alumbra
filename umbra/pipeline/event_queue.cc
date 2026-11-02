#include "umbra/pipeline/event_queue.h"

#include <iterator>
#include <utility>
#include <variant>
#include <vector>

#include "umbra/base/lock.h"
#include "umbra/pipeline/media_events.h"

namespace umbra::pipeline {

void EventQueue::SetNotifyHook(NotifyHook hook) {
  base::AutoLock const lock(lock_);
  hook_ = std::move(hook);
}

void EventQueue::Push(QueueEntry entry) {
  NotifyHook notify;
  {
    base::AutoLock const lock(lock_);
    const bool was_empty = entries_.empty();
    entries_.push_back(std::move(entry));
    if (was_empty) {
      notify = hook_;
    }
  }
  if (notify) {
    notify();
  }
}

std::vector<QueueEntry> EventQueue::Drain() {
  base::AutoLock const lock(lock_);
  std::vector<QueueEntry> drained(std::make_move_iterator(entries_.begin()),
                                  std::make_move_iterator(entries_.end()));
  entries_.clear();
  return drained;
}

void EventQueue::DropEvents() {
  base::AutoLock const lock(lock_);
  std::erase_if(entries_, [](const QueueEntry& entry) {
    return std::holds_alternative<MediaEvent>(entry);
  });
}

bool EventQueue::empty() const {
  base::AutoLock const lock(lock_);
  return entries_.empty();
}

}
