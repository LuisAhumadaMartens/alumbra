#ifndef UMBRA_SOURCE_BUFFERED_READER_H_
#define UMBRA_SOURCE_BUFFERED_READER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/task_runner.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source {

inline constexpr size_t kDefaultReadAheadBytes = 2097152;
inline constexpr size_t kMinReadAheadBytes = 65536;
inline constexpr size_t kMaxReadAheadBytes = 67108864;
inline constexpr size_t kDefaultChunkBytes = 65536;

struct BufferedReaderOptions {
  size_t read_ahead_bytes = kDefaultReadAheadBytes;
  size_t chunk_bytes = kDefaultChunkBytes;
};

class BufferedReader {
 public:
  static absl::StatusOr<std::unique_ptr<BufferedReader>> Create(
      std::shared_ptr<DataSource> source,
      std::shared_ptr<base::SequencedTaskRunner> runner,
      BufferedReaderOptions options = {});

  BufferedReader(const BufferedReader&) = delete;
  BufferedReader& operator=(const BufferedReader&) = delete;
  ~BufferedReader();

  std::shared_ptr<ReadRequest> Read(uint64_t offset, size_t size,
                                    ReadRequest::Callback callback);
  void Close();

  [[nodiscard]] const DataSourceInfo& Info() const;
  [[nodiscard]] size_t BufferedBytes() const;

 private:
  struct Pending {
    uint64_t offset = 0;
    size_t size = 0;
    std::shared_ptr<ReadRequest> request;
  };

  BufferedReader(std::shared_ptr<DataSource> source,
                 std::shared_ptr<base::SequencedTaskRunner> runner,
                 BufferedReaderOptions options);

  void Pump();
  void MaybeFetch();
  void OnFetched(size_t requested,
                 absl::StatusOr<penumbra::SharedBytes> result);
  void Deliver(const std::shared_ptr<ReadRequest>& request,
               absl::StatusOr<penumbra::SharedBytes> result);
  void ResetBufferAt(uint64_t offset);
  void TrimBefore(uint64_t offset);
  [[nodiscard]] uint64_t BufferEnd() const;

  std::shared_ptr<DataSource> source_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  BufferedReaderOptions options_;
  std::vector<uint8_t> buffer_;
  uint64_t buffer_start_ = 0;
  uint64_t cursor_ = 0;
  std::optional<uint64_t> end_of_source_;
  std::shared_ptr<ReadRequest> fetch_;
  std::optional<Pending> pending_;
  absl::Status deferred_error_;
  bool closed_ = false;
  base::SequenceChecker sequence_;
};

}

#endif
