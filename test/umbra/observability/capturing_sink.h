#ifndef TEST_UMBRA_OBSERVABILITY_CAPTURING_SINK_H_
#define TEST_UMBRA_OBSERVABILITY_CAPTURING_SINK_H_

#include <mutex>
#include <vector>

#include "umbra/observability/log.h"

namespace umbra::observability {

class CapturingSink final : public Sink {
 public:
  void Write(const LogRecord& record) override {
    const std::scoped_lock lock(mutex_);
    records_.push_back(record);
  }

  [[nodiscard]] std::vector<LogRecord> Records() const {
    const std::scoped_lock lock(mutex_);
    return records_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<LogRecord> records_;
};

}

#endif
