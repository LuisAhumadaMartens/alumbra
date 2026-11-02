#include "umbra/source/source_type_detector.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/source_error.h"
#include "umbra/source/source_type.h"

namespace umbra::source {

SourceTypeDetector::SourceTypeDetector(
    BufferedReader* reader, std::shared_ptr<const base::UrlRedactor> redactor)
    : reader_(reader), redactor_(std::move(redactor)) {
  sequence_.DetachFromSequence();
}

SourceTypeDetector::~SourceTypeDetector() { Cancel(); }

void SourceTypeDetector::Start(SniffHints hints, DetectCallback callback) {
  assert(sequence_.CalledOnValidSequence());
  Cancel();
  hints_ = std::move(hints);
  callback_ = std::move(callback);
  Fetch(kSniffBytes);
}

void SourceTypeDetector::Cancel() {
  if (request_ != nullptr) {
    request_->Cancel();
    request_.reset();
  }
  callback_ = nullptr;
}

void SourceTypeDetector::Fetch(size_t size) {
  request_ = reader_->Read(
      0, size, [this, size](absl::StatusOr<penumbra::SharedBytes> result) {
        OnBytes(size, std::move(result));
      });
}

void SourceTypeDetector::OnBytes(size_t requested,
                                 absl::StatusOr<penumbra::SharedBytes> result) {
  assert(sequence_.CalledOnValidSequence());
  request_.reset();
  if (!result.ok()) {
    Finish(result.status());
    return;
  }
  const bool end_of_source = result->size() < requested;
  const SniffResult sniffed =
      SniffSourceType(result->span(), end_of_source, hints_);
  if (sniffed.outcome == SniffOutcome::kDecided && sniffed.type.has_value()) {
    Finish(DetectedType{.type = *sniffed.type, .signal = sniffed.signal});
    return;
  }
  if (sniffed.outcome == SniffOutcome::kNeedMoreBytes &&
      sniffed.wanted_bytes > requested) {
    Fetch(sniffed.wanted_bytes);
    return;
  }
  Finish(MakeSourceError(
      base::ErrorCode::kSourceTypeUnsupported, absl::StatusCode::kUnimplemented,
      absl::StrCat("the source type could not be determined from the first ",
                   result->size(), " bytes; set source.type_hint to name it"),
      {{"bytes_inspected", absl::StrCat(result->size())}}, *redactor_));
}

void SourceTypeDetector::Finish(absl::StatusOr<DetectedType> result) {
  DetectCallback callback = std::move(callback_);
  callback_ = nullptr;
  if (callback) {
    std::move(callback)(std::move(result));
  }
}

}
