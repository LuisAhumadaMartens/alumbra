#ifndef UMBRA_FETCH_INIT_CACHE_H_
#define UMBRA_FETCH_INIT_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "penumbra/interface/shared_bytes.h"
#include "umbra/manifest/segment.h"

namespace umbra::fetch {

[[nodiscard]] std::string InitCacheKey(
    const std::string& url, const std::optional<manifest::ByteRange>& range);

class InitCache {
 public:
  explicit InitCache(size_t max_entries) : max_entries_(max_entries) {}

  [[nodiscard]] std::optional<penumbra::SharedBytes> Find(
      const std::string& key);
  void Insert(const std::string& key, penumbra::SharedBytes bytes);
  void Clear();

  [[nodiscard]] size_t size() const { return entries_.size(); }

 private:
  using Order = std::list<std::string>;
  struct Entry {
    penumbra::SharedBytes bytes;
    Order::iterator position;
  };

  size_t max_entries_;
  Order order_;
  std::map<std::string, Entry> entries_;
};

}

#endif
