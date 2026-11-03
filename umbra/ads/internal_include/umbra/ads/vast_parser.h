#ifndef UMBRA_ADS_VAST_PARSER_H_
#define UMBRA_ADS_VAST_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace umbra::ads {

enum class VastErrorCode {
  kXmlParse = 100,
  kSchema = 101,
  kUnsupportedVersion = 102,
  kWrapperTimeout = 301,
  kWrapperLimit = 302,
  kNoAds = 303
};

struct VastTrackingEvent {
  std::string event;
  std::string url;
};

struct VastMediaFile {
  std::string url;
  std::string delivery;
  std::string type;
  std::string codec;
  std::optional<std::uint64_t> bitrate_bps;
  std::optional<std::uint32_t> width;
  std::optional<std::uint32_t> height;
  std::string api_framework;
  bool scalable = false;
  bool maintain_aspect_ratio = false;
  bool mezzanine = false;
};

struct VastCreative {
  std::string id;
  std::string ad_id;
  std::optional<std::uint64_t> sequence;
  std::vector<VastMediaFile> media_files;
  std::vector<VastTrackingEvent> tracking_events;
  std::vector<std::string> click_through_urls;
  std::vector<std::string> click_tracking_urls;
  std::optional<std::string> skip_offset;
};

struct VastVerification {
  std::string vendor;
  std::string resource;
  std::string java_script_resource;
  std::vector<std::string> tracking_events;
  std::vector<std::string> parameters;
  std::uint32_t not_executed_reason = 2;
};

struct VastUniversalAdId {
  std::string id;
  std::string registry = "unknown";
};

struct VastAd {
  std::string id;
  std::optional<std::uint64_t> sequence;
  std::string ad_type;
  bool is_wrapper = false;
  bool follow_additional_wrappers = true;
  bool allow_multiple_ads = false;
  bool fallback_on_no_ad = false;
  std::string wrapper_uri;
  std::vector<std::string> impressions;
  std::vector<std::string> error_urls;
  std::vector<VastCreative> creatives;
  std::vector<VastUniversalAdId> universal_ad_ids;
  std::vector<VastVerification> verifications;
  std::vector<std::string> extensions_xml;
};

struct VastDocument {
  std::string version;
  std::vector<VastAd> ads;
};

struct VastParserOptions {
  std::size_t max_response_bytes = 0;
  std::size_t max_element_depth = 64;
  std::size_t max_elements = 200000;
};

struct VastResolverOptions {
  std::size_t max_wrapper_depth = 0;
  std::size_t max_response_bytes = 0;
  std::uint32_t request_timeout_ms = 0;
};

using VastFetch =
    std::function<absl::StatusOr<std::string>(std::string_view, std::uint32_t)>;

absl::StatusOr<VastDocument> ParseVast(std::string_view xml,
                                       VastParserOptions options = {});

absl::StatusOr<VastDocument> ResolveVastWrappers(
    std::string_view xml, const VastFetch& fetch,
    VastResolverOptions options = {});

}

#endif
