#ifndef PENUMBRA_INTERFACE_STREAM_PROVIDER_H_
#define PENUMBRA_INTERFACE_STREAM_PROVIDER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra {

struct Credential {
  std::string username;
  std::string password;
};

struct ByteStreamInfo {
  bool seekable = false;
  std::optional<uint64_t> size = std::nullopt;
  std::string content_type;
  std::string final_url;
};

struct ByteStreamRequest {
  std::string url;
  uint64_t offset = 0;
  std::optional<uint64_t> length = std::nullopt;
  std::optional<Credential> credential = std::nullopt;
  RequestClass request_class = RequestClass::kMedia;
};

struct ByteStreamChunk {
  SharedBytes data;
  absl::Duration arrival_time;
};

class ByteStreamCallbacks {
 public:
  virtual ~ByteStreamCallbacks() = default;
  virtual absl::Status OnOpen(ByteStreamInfo info) = 0;
  virtual absl::Status OnData(ByteStreamChunk chunk) = 0;
  virtual absl::Status OnEnd() = 0;
  virtual absl::Status OnError(absl::Status error) = 0;
};

class StreamControl {
 public:
  virtual ~StreamControl() = default;
  virtual void Cancel() = 0;
  virtual absl::Status Pause() = 0;
  virtual absl::Status Resume() = 0;
};

class ByteStream : public StreamControl {};

class ByteStreamProvider {
 public:
  virtual ~ByteStreamProvider() = default;
  [[nodiscard]] virtual std::vector<std::string> Schemes() const = 0;
  virtual absl::StatusOr<std::unique_ptr<ByteStream>> Open(
      ByteStreamRequest request,
      std::shared_ptr<ByteStreamCallbacks> callbacks) = 0;
};

struct ElementaryStreamRequest {
  std::string url;
  std::optional<Credential> credential = std::nullopt;
};

class ElementaryStreamCallbacks {
 public:
  virtual ~ElementaryStreamCallbacks() = default;
  virtual absl::Status OnStreams(std::vector<StreamInfo> streams) = 0;
  virtual absl::Status OnSample(EncodedSample sample,
                                absl::Duration arrival_time) = 0;
  virtual absl::Status OnEnd() = 0;
  virtual absl::Status OnError(absl::Status error) = 0;
};

class ElementaryStream : public StreamControl {};

class ElementaryStreamProvider {
 public:
  virtual ~ElementaryStreamProvider() = default;
  [[nodiscard]] virtual std::vector<std::string> Schemes() const = 0;
  virtual absl::StatusOr<std::unique_ptr<ElementaryStream>> Open(
      ElementaryStreamRequest request,
      std::shared_ptr<ElementaryStreamCallbacks> callbacks) = 0;
};

class StreamProviderCatalogue {
 public:
  virtual ~StreamProviderCatalogue() = default;
  [[nodiscard]] virtual std::vector<std::shared_ptr<ByteStreamProvider>>
  ByteStreamProviders() const = 0;
  [[nodiscard]] virtual std::vector<std::shared_ptr<ElementaryStreamProvider>>
  ElementaryStreamProviders() const = 0;
};

}

#endif
