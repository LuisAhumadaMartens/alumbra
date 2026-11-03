#ifndef UMBRA_MANIFEST_DASH_LIVE_H_
#define UMBRA_MANIFEST_DASH_LIVE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "umbra/base/task_runner.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

using DashUtcTimingCallback =
    std::function<void(absl::StatusOr<absl::Duration>)>;

void ResolveDashUtcOffset(const Presentation& presentation,
                          std::shared_ptr<penumbra::HttpTransport> transport,
                          std::shared_ptr<base::SequencedTaskRunner> runner,
                          std::function<absl::Time()> wall_clock,
                          std::string fallback_url,
                          DashUtcTimingCallback callback);

struct DashAvailabilityWindow {
  penumbra::MediaTime start;
  penumbra::MediaTime end;
  penumbra::MediaTime available_end;
};

struct DashRefreshPlan {
  bool refresh = false;
  bool end_playback = false;
  absl::Duration delay = absl::InfiniteDuration();
  std::string manifest_url;
};

[[nodiscard]] DashRefreshPlan PlanDashRefresh(const Presentation& presentation,
                                              std::string_view manifest_url);

struct DashMpdRefreshUpdate {
  std::shared_ptr<const Presentation> presentation;
  std::string manifest_url;
  bool ended = false;
};

struct DashMpdRefresherConfig {
  std::shared_ptr<const Presentation> presentation;
  std::string manifest_url;
  std::shared_ptr<penumbra::HttpTransport> transport;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::size_t max_mpd_bytes = kDefaultDashMpdMaxBytes;
  std::function<void(absl::StatusOr<DashMpdRefreshUpdate>)> on_update;
};

class DashMpdRefresher {
 public:
  static absl::StatusOr<std::unique_ptr<DashMpdRefresher>> Create(
      DashMpdRefresherConfig config);
  ~DashMpdRefresher();
  DashMpdRefresher(const DashMpdRefresher&) = delete;
  DashMpdRefresher& operator=(const DashMpdRefresher&) = delete;

  void Start();
  void Stop();

 private:
  class Impl;
  explicit DashMpdRefresher(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

[[nodiscard]] absl::StatusOr<DashAvailabilityWindow> DashAvailableWindow(
    const Presentation& presentation, absl::Time utc_now,
    absl::Duration utc_offset = absl::ZeroDuration());

[[nodiscard]] absl::StatusOr<penumbra::MediaTime> DashLiveStartPosition(
    const Presentation& presentation, absl::Time utc_now,
    absl::Duration utc_offset = absl::ZeroDuration(),
    std::int64_t start_delay_ms = -1);

}

#endif
