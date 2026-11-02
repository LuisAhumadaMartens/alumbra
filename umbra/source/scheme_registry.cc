#include "umbra/source/scheme_registry.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/stream_provider.h"

namespace umbra::source {
namespace {

bool IsValidScheme(std::string_view scheme) {
  if (scheme.empty() || scheme.front() < 'a' || scheme.front() > 'z') {
    return false;
  }
  return std::ranges::all_of(scheme, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
           c == '-' || c == '.';
  });
}

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return result;
}

}

absl::Status SchemeRegistry::Reserve(const std::vector<std::string>& schemes) {
  if (schemes.empty()) {
    return absl::InvalidArgumentError("a provider must declare a scheme");
  }
  std::set<std::string> seen;
  for (const std::string& scheme : schemes) {
    if (!IsValidScheme(scheme)) {
      return absl::InvalidArgumentError(
          absl::StrCat("invalid provider scheme '", scheme, "'"));
    }
    if (entries_.contains(scheme) || !seen.insert(scheme).second) {
      return absl::AlreadyExistsError(
          absl::StrCat("scheme '", scheme, "' already has a provider"));
    }
  }
  return absl::OkStatus();
}

absl::Status SchemeRegistry::RegisterByteStream(
    const std::shared_ptr<penumbra::ByteStreamProvider>& provider) {
  if (provider == nullptr) {
    return absl::InvalidArgumentError("provider is null");
  }
  const std::vector<std::string> schemes = provider->Schemes();
  if (absl::Status status = Reserve(schemes); !status.ok()) {
    return status;
  }
  for (const std::string& scheme : schemes) {
    entries_[scheme] = {.kind = ProviderKind::kByteStream,
                        .byte_stream = provider,
                        .elementary_stream = nullptr};
  }
  return absl::OkStatus();
}

absl::Status SchemeRegistry::RegisterElementaryStream(
    const std::shared_ptr<penumbra::ElementaryStreamProvider>& provider) {
  if (provider == nullptr) {
    return absl::InvalidArgumentError("provider is null");
  }
  const std::vector<std::string> schemes = provider->Schemes();
  if (absl::Status status = Reserve(schemes); !status.ok()) {
    return status;
  }
  for (const std::string& scheme : schemes) {
    entries_[scheme] = {.kind = ProviderKind::kElementaryStream,
                        .byte_stream = nullptr,
                        .elementary_stream = provider};
  }
  return absl::OkStatus();
}

absl::Status SchemeRegistry::RegisterFrom(
    const penumbra::StreamProviderCatalogue& catalogue) {
  for (auto& provider : catalogue.ByteStreamProviders()) {
    if (absl::Status status = RegisterByteStream(provider); !status.ok()) {
      return status;
    }
  }
  for (auto& provider : catalogue.ElementaryStreamProviders()) {
    if (absl::Status status = RegisterElementaryStream(provider);
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

const SchemeEntry* SchemeRegistry::Find(std::string_view scheme) const {
  const auto it = entries_.find(Lower(scheme));
  return it == entries_.end() ? nullptr : &it->second;
}

std::vector<std::string> SchemeRegistry::Schemes() const {
  std::vector<std::string> result;
  result.reserve(entries_.size());
  for (const auto& entry : entries_) {
    result.push_back(entry.first);
  }
  return result;
}

}
