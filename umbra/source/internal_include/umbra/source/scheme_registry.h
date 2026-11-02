#ifndef UMBRA_SOURCE_SCHEME_REGISTRY_H_
#define UMBRA_SOURCE_SCHEME_REGISTRY_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "penumbra/interface/stream_provider.h"

namespace umbra::source {

enum class ProviderKind { kByteStream, kElementaryStream };

struct SchemeEntry {
  ProviderKind kind = ProviderKind::kByteStream;
  std::shared_ptr<penumbra::ByteStreamProvider> byte_stream;
  std::shared_ptr<penumbra::ElementaryStreamProvider> elementary_stream;
};

class SchemeRegistry {
 public:
  absl::Status RegisterByteStream(
      const std::shared_ptr<penumbra::ByteStreamProvider>& provider);
  absl::Status RegisterElementaryStream(
      const std::shared_ptr<penumbra::ElementaryStreamProvider>& provider);
  absl::Status RegisterFrom(const penumbra::StreamProviderCatalogue& catalogue);

  [[nodiscard]] const SchemeEntry* Find(std::string_view scheme) const;
  [[nodiscard]] std::vector<std::string> Schemes() const;

 private:
  absl::Status Reserve(const std::vector<std::string>& schemes);

  std::map<std::string, SchemeEntry> entries_;
};

}

#endif
