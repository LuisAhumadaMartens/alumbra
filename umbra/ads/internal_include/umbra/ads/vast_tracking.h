#ifndef UMBRA_ADS_VAST_TRACKING_H_
#define UMBRA_ADS_VAST_TRACKING_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "penumbra/interface/media_time.h"
#include "umbra/ads/beacon_dispatcher.h"
#include "umbra/ads/provider.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {

struct TrackingMacroContext {
  std::string cache_busting;
  std::string timestamp;
  std::string asset_uri;
  std::string gdpr_consent;
  std::string app_bundle;
  std::string device_id;
  std::string advertising_id;
  std::string player_width;
  std::string player_height;
  std::string error_code;
  std::string reason;
  std::string limit_ad_tracking = "0";
  penumbra::TimeDelta ad_playhead = penumbra::TimeDelta::Unknown();
  penumbra::TimeDelta media_playhead = penumbra::TimeDelta::Unknown();
};

using BeaconSend = std::function<void(std::string)>;

class VastTrackingSession {
 public:
  explicit VastTrackingSession(BeaconSend send);
  explicit VastTrackingSession(BeaconDispatcher& dispatcher);

  void HandleEvent(const AdProviderEvent& event, const VastAd& ad,
                   const VastCreative& creative, TrackingMacroContext context);
  void ReportError(const VastAd& ad, std::string error_code,
                   TrackingMacroContext context);

 private:
  void Dispatch(std::string_view event, const VastAd& ad,
                const VastCreative* creative,
                const TrackingMacroContext& context);

  BeaconSend send_;
  std::unordered_set<std::string> fired_once_;
};

[[nodiscard]] std::string ExpandVastMacros(std::string_view url,
                                           const TrackingMacroContext& context);

}

#endif
