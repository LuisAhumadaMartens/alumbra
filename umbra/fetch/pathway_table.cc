#include "umbra/fetch/pathway_table.h"

#include <string>

#include "absl/time/time.h"
#include "umbra/base/time_ticks.h"

namespace umbra::fetch {

void PathwayTable::Exclude(const std::string& key, base::TimeTicks now,
                           absl::Duration exclusion, bool until_refresh) {
  Entry& entry = entries_[key];
  const base::TimeTicks end = now + exclusion;
  if (!entry.exclusion_end.has_value() || *entry.exclusion_end < end) {
    entry.exclusion_end = end;
  }
  entry.until_refresh = until_refresh;
}

void PathwayTable::HoldUntil(const std::string& key, base::TimeTicks deadline) {
  Entry& entry = entries_[key];
  if (!entry.hold_end.has_value() || *entry.hold_end < deadline) {
    entry.hold_end = deadline;
  }
}

void PathwayTable::OnSteeringRefresh() {
  for (auto& [key, entry] : entries_) {
    if (entry.until_refresh) {
      entry.exclusion_end.reset();
      entry.until_refresh = false;
    }
  }
}

void PathwayTable::Clear(const std::string& key) { entries_.erase(key); }

bool PathwayTable::IsExcluded(const std::string& key,
                              base::TimeTicks now) const {
  const auto found = entries_.find(key);
  if (found == entries_.end()) {
    return false;
  }
  const Entry& entry = found->second;
  const bool excluded =
      entry.exclusion_end.has_value() && now < *entry.exclusion_end;
  const bool held = entry.hold_end.has_value() && now < *entry.hold_end;
  return excluded || held;
}

}
