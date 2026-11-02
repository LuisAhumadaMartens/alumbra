#ifndef PENUMBRA_DESKTOP_FILE_BYTE_STREAM_PROVIDER_H_
#define PENUMBRA_DESKTOP_FILE_BYTE_STREAM_PROVIDER_H_

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/stream_provider.h"

namespace penumbra::desktop {

[[nodiscard]] absl::StatusOr<std::filesystem::path> PathFromFileUrl(
    std::string_view url);

class FileByteStreamProvider final : public ByteStreamProvider {
 public:
  [[nodiscard]] std::vector<std::string> Schemes() const override;
  absl::StatusOr<std::unique_ptr<ByteStream>> Open(
      ByteStreamRequest request,
      std::shared_ptr<ByteStreamCallbacks> callbacks) override;
};

}

#endif
