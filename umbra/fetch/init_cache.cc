#include "umbra/fetch/init_cache.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/manifest/segment.h"

namespace umbra::fetch {

std::string InitCacheKey(const std::string& url,
                         const std::optional<manifest::ByteRange>& range) {
  if (!range.has_value()) {
    return url;
  }
  return absl::StrCat(url, "#bytes=", range->offset, "+", range->length);
}

std::optional<penumbra::SharedBytes> InitCache::Find(const std::string& key) {
  const auto found = entries_.find(key);
  if (found == entries_.end()) {
    return std::nullopt;
  }
  order_.splice(order_.begin(), order_, found->second.position);
  return found->second.bytes;
}

void InitCache::Insert(const std::string& key, penumbra::SharedBytes bytes) {
  const auto found = entries_.find(key);
  if (found != entries_.end()) {
    found->second.bytes = std::move(bytes);
    order_.splice(order_.begin(), order_, found->second.position);
    return;
  }
  order_.push_front(key);
  entries_.emplace(
      key, Entry{.bytes = std::move(bytes), .position = order_.begin()});
  while (entries_.size() > max_entries_) {
    entries_.erase(order_.back());
    order_.pop_back();
  }
}

void InitCache::Clear() {
  entries_.clear();
  order_.clear();
}

}
