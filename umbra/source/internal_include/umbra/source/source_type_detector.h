#ifndef UMBRA_SOURCE_SOURCE_TYPE_DETECTOR_H_
#define UMBRA_SOURCE_SOURCE_TYPE_DETECTOR_H_

#include <cstddef>
#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/read_request.h"
#include "umbra/source/source_type.h"

namespace umbra::source {

struct DetectedType {
  SourceType type = SourceType::kMp4;
  TypeSignal signal = TypeSignal::kSignature;
};

using DetectCallback =
    absl::AnyInvocable<void(absl::StatusOr<DetectedType>) &&>;

class SourceTypeDetector {
 public:
  SourceTypeDetector(BufferedReader* reader,
                     std::shared_ptr<const base::UrlRedactor> redactor);
  SourceTypeDetector(const SourceTypeDetector&) = delete;
  SourceTypeDetector& operator=(const SourceTypeDetector&) = delete;
  ~SourceTypeDetector();

  void Start(SniffHints hints, DetectCallback callback);
  void Cancel();

 private:
  void Fetch(size_t size);
  void OnBytes(size_t requested, absl::StatusOr<penumbra::SharedBytes> result);
  void Finish(absl::StatusOr<DetectedType> result);

  BufferedReader* reader_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  SniffHints hints_;
  DetectCallback callback_;
  std::shared_ptr<ReadRequest> request_;
  base::SequenceChecker sequence_;
};

}

#endif
