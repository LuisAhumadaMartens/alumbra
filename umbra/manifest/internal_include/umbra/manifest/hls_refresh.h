#ifndef UMBRA_MANIFEST_HLS_REFRESH_H_
#define UMBRA_MANIFEST_HLS_REFRESH_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/media_time.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

struct HlsReloadDecision {
  bool reload = false;
  bool failover = false;
  absl::Duration delay = absl::InfiniteDuration();
};

class HlsPlaylistReloadPolicy {
 public:
  [[nodiscard]] HlsReloadDecision OnResponse(
      const Presentation& presentation, const TrackId& playlist_track,
      bool changed, absl::Time received_at,
      bool stale_playlist_failover = true);

 private:
  std::optional<absl::Time> last_change_;
};

[[nodiscard]] std::vector<std::string> SelectedHlsPlaylistUris(
    const Presentation& presentation,
    const std::vector<TrackId>& selected_tracks);

[[nodiscard]] absl::StatusOr<penumbra::MediaTime> HlsLiveStartPosition(
    const Presentation& presentation, std::int64_t start_offset_ms = -1);

}

#endif
