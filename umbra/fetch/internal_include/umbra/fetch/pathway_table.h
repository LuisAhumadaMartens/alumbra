#ifndef UMBRA_FETCH_PATHWAY_TABLE_H_
#define UMBRA_FETCH_PATHWAY_TABLE_H_

#include <map>
#include <optional>
#include <string>

#include "absl/time/time.h"
#include "umbra/base/time_ticks.h"

namespace umbra::fetch {

class PathwayTable {
 public:
  void Exclude(const std::string& key, base::TimeTicks now,
               absl::Duration exclusion, bool until_refresh);
  void HoldUntil(const std::string& key, base::TimeTicks deadline);
  void OnSteeringRefresh();
  void Clear(const std::string& key);

  [[nodiscard]] bool IsExcluded(const std::string& key,
                                base::TimeTicks now) const;

 private:
  struct Entry {
    std::optional<base::TimeTicks> exclusion_end;
    bool until_refresh = false;
    std::optional<base::TimeTicks> hold_end;
  };

  std::map<std::string, Entry> entries_;
};

}

#endif
