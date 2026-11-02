#ifndef UMBRA_OBSERVABILITY_PORT_SINK_H_
#define UMBRA_OBSERVABILITY_PORT_SINK_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "penumbra/interface/log_sink.h"
#include "umbra/observability/log.h"

namespace umbra::observability {

[[nodiscard]] std::string FormatRecord(const LogRecord& record);

class PortSink final : public Sink {
 public:
  explicit PortSink(std::shared_ptr<penumbra::LogSink> port)
      : port_(std::move(port)) {}

  void Write(const LogRecord& record) override;

  [[nodiscard]] std::uint64_t Rejected() const {
    return rejected_.load(std::memory_order_relaxed);
  }

 private:
  std::shared_ptr<penumbra::LogSink> port_;
  std::atomic<std::uint64_t> rejected_{0};
};

}

#endif
