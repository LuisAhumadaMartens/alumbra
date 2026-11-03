#ifndef UMBRA_ADS_PROVIDER_H_
#define UMBRA_ADS_PROVIDER_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "umbra/ads/ad_manager.h"

namespace umbra::ads {

struct ProviderCapabilities {
  bool renders_own_ui = false;
  bool owns_skip_control = false;
  bool fires_own_beacons = false;
  bool supports_preloading = false;
  bool needs_content_video_element = false;
};

struct AdConsent {
  std::string tcf_string;
  std::string gpp_string;
  bool limited_ads_enabled = false;
  bool child_directed_enabled = false;
};

struct AdRequest {
  std::string ad_tag_url;
  AdConsent consent;
};

struct AdSize {
  int width = 0;
  int height = 0;
};

struct AdProviderEvent {
  enum class Type : uint8_t {
    kAdsLoaded,
    kAdBreakStart,
    kAdStart,
    kAdProgress,
    kAdQuartile,
    kAdClick,
    kAdSkippableStateChange,
    kAdSkip,
    kAdComplete,
    kAdBreakEnd,
    kAllAdsCompleted,
    kAdError
  };

  Type type = Type::kAdsLoaded;
  std::string break_id;
  std::string ad_id;
  std::string creative_id;
  std::size_t pod_position = 0;
  std::size_t pod_size = 0;
  std::uint8_t quartile = 0;
  bool skippable = false;
  penumbra::TimeDelta duration = penumbra::TimeDelta::Unknown();
  penumbra::TimeDelta current_time = penumbra::TimeDelta::Unknown();
  penumbra::TimeDelta skip_offset = penumbra::TimeDelta::Unknown();
  std::string provider_code;
};

using AdProviderEventCallback = std::function<void(const AdProviderEvent&)>;

class ProviderLifecycle {
 public:
  virtual ~ProviderLifecycle() = default;
  [[nodiscard]] virtual ProviderCapabilities Capabilities() const = 0;
  [[nodiscard]] virtual ProviderResult RequestAds(const AdRequest& request) = 0;
  [[nodiscard]] virtual ProviderResult StartAd(const Ad& ad) = 0;
  [[nodiscard]] virtual ProviderResult PauseAd() = 0;
  [[nodiscard]] virtual ProviderResult ResumeAd() = 0;
  [[nodiscard]] virtual ProviderResult SkipAd() = 0;
  [[nodiscard]] virtual ProviderResult Resize(AdSize size) = 0;
  virtual void ContentProgress(penumbra::MediaTime position) = 0;
  virtual void ContentComplete() = 0;
  virtual void SetEventCallback(AdProviderEventCallback callback) = 0;
  virtual void Destroy() = 0;
};

class BuiltInAdProvider final : public ProviderLifecycle {
 public:
  using RequestHandler =
      std::function<ProviderResult(const AdRequest&, std::vector<AdBreak>&)>;

  explicit BuiltInAdProvider(RequestHandler request_handler);
  [[nodiscard]] ProviderCapabilities Capabilities() const override;
  [[nodiscard]] ProviderResult RequestAds(const AdRequest& request) override;
  [[nodiscard]] ProviderResult StartAd(const Ad& ad) override;
  [[nodiscard]] ProviderResult PauseAd() override;
  [[nodiscard]] ProviderResult ResumeAd() override;
  [[nodiscard]] ProviderResult SkipAd() override;
  [[nodiscard]] ProviderResult Resize(AdSize size) override;
  void ContentProgress(penumbra::MediaTime position) override;
  void ContentComplete() override;
  void SetEventCallback(AdProviderEventCallback callback) override;
  void Destroy() override;
  void ReportProgress(penumbra::TimeDelta elapsed);
  void ReportClick();
  void SetSkippable(bool skippable);
  void CompleteAd();
  void EndBreak();
  void ReportError(std::string provider_code);
  [[nodiscard]] const std::vector<AdBreak>& Breaks() const;

 private:
  void Emit(const AdProviderEvent& event) const;
  [[nodiscard]] AdProviderEvent Event(AdProviderEvent::Type type) const;

  RequestHandler request_handler_;
  AdProviderEventCallback event_callback_;
  std::vector<AdBreak> breaks_;
  std::optional<Ad> active_ad_;
  std::string active_break_id_;
  std::size_t active_pod_position_ = 0;
  std::size_t active_pod_size_ = 0;
  std::uint8_t last_quartile_ = 0;
  bool skippable_ = false;
  bool active_break_ = false;
  bool destroyed_ = false;
};

}

#endif
