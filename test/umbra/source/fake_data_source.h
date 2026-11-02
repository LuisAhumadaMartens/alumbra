#ifndef TEST_UMBRA_SOURCE_FAKE_DATA_SOURCE_H_
#define TEST_UMBRA_SOURCE_FAKE_DATA_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source::test {

struct ReadLogEntry {
  uint64_t offset = 0;
  size_t size = 0;
};

class FakeDataSource final : public DataSource {
 public:
  FakeDataSource(std::vector<uint8_t> content, bool seekable, bool size_known,
                 std::shared_ptr<base::SequencedTaskRunner> runner);

  [[nodiscard]] const DataSourceInfo& Info() const override;
  std::shared_ptr<ReadRequest> Read(uint64_t offset, size_t size,
                                    ReadRequest::Callback callback) override;
  void Close() override;

  void DeliverNext();
  void DeliverAll();
  void FailNext(absl::Status status);

  [[nodiscard]] size_t InflightCount() const { return inflight_.size(); }
  [[nodiscard]] const std::vector<ReadLogEntry>& Log() const { return log_; }
  [[nodiscard]] bool Closed() const { return closed_; }

 private:
  struct Inflight {
    uint64_t offset = 0;
    size_t size = 0;
    std::shared_ptr<ReadRequest> request;
    std::optional<absl::Status> failure = std::nullopt;
  };

  std::vector<uint8_t> content_;
  DataSourceInfo info_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::deque<Inflight> inflight_;
  std::vector<ReadLogEntry> log_;
  std::optional<absl::Status> next_failure_;
  uint64_t next_sequential_ = 0;
  bool closed_ = false;
};

}

#endif
