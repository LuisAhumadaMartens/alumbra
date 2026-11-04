#include "umbra/ads/vast_tracking.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "penumbra/interface/media_time.h"
#include "umbra/ads/beacon_dispatcher.h"
#include "umbra/ads/provider.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {
namespace {

std::string Encode(std::string_view value) {
  constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char character : value) {
    const bool unreserved = (character >= 'A' && character <= 'Z') ||
                            (character >= 'a' && character <= 'z') ||
                            (character >= '0' && character <= '9') ||
                            character == '-' || character == '.' ||
                            character == '_' || character == '~';
    if (unreserved) {
      encoded.push_back(static_cast<char>(character));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex.at(character >> 4));
      encoded.push_back(kHex.at(character & 0x0f));
    }
  }
  return encoded;
}

std::string CacheBusting() {
  std::random_device source;
  std::mt19937 generator(source());
  std::uniform_int_distribution<std::uint32_t> distribution(10000000, 99999999);
  return std::to_string(distribution(generator));
}

std::string Timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch());
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(milliseconds);
  const auto fraction = milliseconds - seconds;
  const auto time = static_cast<std::time_t>(seconds.count());
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
         << std::setw(3) << fraction.count() << 'Z';
  return output.str();
}

std::string TimeValue(penumbra::TimeDelta value) {
  if (!value.is_finite() || value < penumbra::TimeDelta::Zero()) {
    return "-1";
  }
  const std::int64_t milliseconds = value.InMicroseconds() / 1000;
  const std::int64_t hours = milliseconds / 3600000;
  const std::int64_t minutes = (milliseconds / 60000) % 60;
  const std::int64_t seconds = (milliseconds / 1000) % 60;
  const std::int64_t fraction = milliseconds % 1000;
  std::ostringstream output;
  output << std::setfill('0') << std::setw(2) << hours << ':' << std::setw(2)
         << minutes << ':' << std::setw(2) << seconds << '.' << std::setw(3)
         << fraction;
  return output.str();
}

std::string ValueOrUnknown(const std::string& value) {
  return value.empty() ? "-1" : value;
}

std::string ValueOrWithheld(const std::string& value) {
  return value.empty() ? "-2" : value;
}

std::string Replace(std::string input, std::string_view macro,
                    std::string_view value) {
  const std::string marker = "[" + std::string(macro) + "]";
  const std::string encoded = Encode(value);
  std::size_t position = 0;
  while ((position = input.find(marker, position)) != std::string::npos) {
    input.replace(position, marker.size(), encoded);
    position += encoded.size();
  }
  return input;
}

bool ReachedOffset(std::string_view offset, penumbra::TimeDelta current,
                   penumbra::TimeDelta duration) {
  if (offset.empty() || !current.is_finite()) {
    return false;
  }
  if (offset.back() == '%') {
    std::uint32_t percentage = 0;
    for (const char digit : offset.substr(0, offset.size() - 1)) {
      if (digit < '0' || digit > '9') {
        return false;
      }
      percentage = (percentage * 10) + static_cast<std::uint32_t>(digit - '0');
    }
    return duration.is_finite() && current.InMicroseconds() * 100 >=
                                       duration.InMicroseconds() * percentage;
  }
  std::int64_t hours = 0;
  std::int64_t minutes = 0;
  std::int64_t seconds = 0;
  std::int64_t milliseconds = 0;
  char first = 0;
  char second = 0;
  char decimal = 0;
  std::istringstream input{std::string(offset)};
  if (!(input >> hours >> first >> minutes >> second >> seconds >> decimal >>
        milliseconds) ||
      first != ':' || second != ':' || decimal != '.') {
    return false;
  }
  const std::int64_t threshold_us =
      (((hours * 60 + minutes) * 60 + seconds) * 1000 + milliseconds) * 1000;
  return current.InMicroseconds() >= threshold_us;
}

std::string EventName(const AdProviderEvent& event) {
  switch (event.type) {
    case AdProviderEvent::Type::kAdStart:
      return "start";
    case AdProviderEvent::Type::kAdProgress:
      return "progress";
    case AdProviderEvent::Type::kAdQuartile:
      if (event.quartile == 1) {
        return "firstQuartile";
      }
      if (event.quartile == 2) {
        return "midpoint";
      }
      if (event.quartile == 3) {
        return "thirdQuartile";
      }
      return {};
    case AdProviderEvent::Type::kAdPause:
      return "pause";
    case AdProviderEvent::Type::kAdResume:
      return "resume";
    case AdProviderEvent::Type::kAdMute:
      return "mute";
    case AdProviderEvent::Type::kAdUnmute:
      return "unmute";
    case AdProviderEvent::Type::kAdSkip:
      return "skip";
    case AdProviderEvent::Type::kAdComplete:
      return "complete";
    case AdProviderEvent::Type::kAdClick:
      return "clickTracking";
    default:
      return {};
  }
}

}

VastTrackingSession::VastTrackingSession(BeaconSend send)
    : send_(std::move(send)) {}

VastTrackingSession::VastTrackingSession(BeaconDispatcher& dispatcher)
    : send_([&dispatcher](std::string url) {
        dispatcher.Dispatch(std::move(url));
      }) {}

void VastTrackingSession::HandleEvent(const AdProviderEvent& event,
                                      const VastAd& ad,
                                      const VastCreative& creative,
                                      TrackingMacroContext context) {
  if (event.type == AdProviderEvent::Type::kAdStart) {
    Dispatch("impression", ad, nullptr, context);
    Dispatch("creativeView", ad, &creative, context);
  }
  if (event.type == AdProviderEvent::Type::kAdProgress) {
    context.ad_playhead = event.current_time;
    for (const VastTrackingEvent& tracking : creative.tracking_events) {
      if (tracking.event == "progress" &&
          ReachedOffset(tracking.offset, event.current_time, event.duration)) {
        Dispatch("progress:" + tracking.offset, ad, &creative, context);
      }
    }
  }
  if (event.type == AdProviderEvent::Type::kAdError) {
    context.error_code = event.provider_code;
    Dispatch("error", ad, nullptr, context);
    return;
  }
  const std::string event_name = EventName(event);
  if (!event_name.empty()) {
    Dispatch(event_name, ad, &creative, context);
  }
}

void VastTrackingSession::ReportError(const VastAd& ad, std::string error_code,
                                      TrackingMacroContext context) {
  context.error_code = std::move(error_code);
  Dispatch("error", ad, nullptr, context);
}

void VastTrackingSession::Dispatch(std::string_view event, const VastAd& ad,
                                   const VastCreative* creative,
                                   const TrackingMacroContext& context) {
  if (!send_) {
    return;
  }
  const bool repeatable = event == "pause" || event == "resume";
  const auto send_url = [&](const std::string& url) {
    if (url.empty()) {
      return;
    }
    const std::string key = ad.id + "\n" + std::string(event) + "\n" + url;
    if (!repeatable && !fired_once_.insert(key).second) {
      return;
    }
    send_(ExpandVastMacros(url, context));
  };
  if (event == "impression") {
    for (const std::string& url : ad.impressions) {
      send_url(url);
    }
  }
  if (event == "error") {
    for (const std::string& url : ad.error_urls) {
      send_url(url);
    }
  }
  if (creative == nullptr) {
    return;
  }
  if (event == "clickTracking") {
    for (const std::string& url : creative->click_tracking_urls) {
      send_url(url);
    }
  }
  for (const VastTrackingEvent& tracking : creative->tracking_events) {
    const std::string tracking_event =
        tracking.event == "progress" && !tracking.offset.empty()
            ? "progress:" + tracking.offset
            : tracking.event;
    if (tracking_event == event) {
      send_url(tracking.url);
    }
  }
}

std::string ExpandVastMacros(std::string_view url,
                             const TrackingMacroContext& context) {
  const std::string cache_busting =
      context.cache_busting.empty() ? CacheBusting() : context.cache_busting;
  const std::string timestamp =
      context.timestamp.empty() ? Timestamp() : context.timestamp;
  std::string expanded(url);
  const std::array<std::pair<std::string_view, std::string>, 16> macros = {{
      {"CACHEBUSTING", cache_busting},
      {"TIMESTAMP", timestamp},
      {"ADPLAYHEAD", TimeValue(context.ad_playhead)},
      {"MEDIAPLAYHEAD", TimeValue(context.media_playhead)},
      {"CONTENTPLAYHEAD", TimeValue(context.media_playhead)},
      {"ERRORCODE", ValueOrUnknown(context.error_code)},
      {"ASSETURI", ValueOrUnknown(context.asset_uri)},
      {"PLAYERSIZE",
       ValueOrUnknown(
           context.player_width.empty() || context.player_height.empty()
               ? std::string()
               : context.player_width + "x" + context.player_height)},
      {"GDPRCONSENT", ValueOrUnknown(context.gdpr_consent)},
      {"LIMITADTRACKING", context.limit_ad_tracking},
      {"REASON", ValueOrUnknown(context.reason)},
      {"APPBUNDLE", ValueOrWithheld(context.app_bundle)},
      {"IFA", ValueOrWithheld(context.advertising_id)},
      {"DEVICEID", ValueOrWithheld(context.device_id)},
      {"DEVICEUA", "-1"},
      {"LATLONG", "-1"},
  }};
  for (const auto& [name, value] : macros) {
    expanded = Replace(std::move(expanded), name, value);
  }
  return expanded;
}

}
