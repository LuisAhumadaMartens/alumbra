#ifndef TEST_UMBRA_SOURCE_FAKE_STREAM_PROVIDER_H_
#define TEST_UMBRA_SOURCE_FAKE_STREAM_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/stream_provider.h"

namespace umbra::source::test {

struct FakeStreamState {
  penumbra::ByteStreamRequest request;
  std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks;
  uint64_t next = 0;
  bool opened = false;
  bool ended = false;
  bool paused = false;
  bool cancelled = false;
  int pauses = 0;
  int resumes = 0;
};

class FakeStreamProvider final : public penumbra::ByteStreamProvider {
 public:
  FakeStreamProvider(std::vector<uint8_t> content, bool seekable,
                     bool size_known, size_t chunk_bytes);

  [[nodiscard]] std::vector<std::string> Schemes() const override;
  absl::StatusOr<std::unique_ptr<penumbra::ByteStream>> Open(
      penumbra::ByteStreamRequest request,
      std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks) override;

  bool Step(size_t stream_index);
  size_t StepAll();
  void FailOpenWith(absl::Status status);
  void FailStreamAfterOpen(size_t stream_index, absl::Status status);
  void RejectOpens(absl::Status status);

  [[nodiscard]] const std::vector<std::shared_ptr<FakeStreamState>>& Streams()
      const {
    return streams_;
  }

 private:
  std::vector<uint8_t> content_;
  bool seekable_;
  bool size_known_;
  size_t chunk_bytes_;
  std::vector<std::shared_ptr<FakeStreamState>> streams_;
  std::optional<absl::Status> open_failure_;
  std::optional<absl::Status> reject_;
};

}

#endif
