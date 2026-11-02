#ifndef UMBRA_MANIFEST_PRESENTATION_H_
#define UMBRA_MANIFEST_PRESENTATION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/time/time.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {

enum class Protocol : std::uint8_t {
  kHls,
  kDash,
};

enum class PresentationType : std::uint8_t {
  kStatic,
  kDynamic,
};

enum class VideoRange : std::uint8_t {
  kUnspecified,
  kSdr,
  kPq,
  kHlg,
};

enum class HdcpLevel : std::uint8_t {
  kUnspecified,
  kNone,
  kType0,
  kType1,
};

enum class Role : std::uint8_t {
  kMain,
  kAlternate,
  kSupplementary,
  kCommentary,
  kDub,
  kEmergency,
  kCaption,
  kSubtitle,
  kDescription,
  kEnhancedAudioIntelligibility,
  kSign,
};

struct TimelineMapping {
  penumbra::MediaTime media_origin;
  penumbra::MediaTime presentation_origin;

  [[nodiscard]] penumbra::MediaTime ToPresentation(
      penumbra::MediaTime media) const {
    return presentation_origin + (media - media_origin);
  }
  [[nodiscard]] penumbra::MediaTime ToMedia(
      penumbra::MediaTime presentation) const {
    return media_origin + (presentation - presentation_origin);
  }

  friend bool operator==(const TimelineMapping&,
                         const TimelineMapping&) = default;
};

struct VideoTrackFormat {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::optional<penumbra::Rational> frame_rate;
  std::int32_t bit_depth = 0;
  VideoRange range = VideoRange::kUnspecified;

  friend bool operator==(const VideoTrackFormat&,
                         const VideoTrackFormat&) = default;
};

struct ChannelConfiguration {
  std::int32_t channel_count = 0;
  bool object_based = false;

  friend bool operator==(const ChannelConfiguration&,
                         const ChannelConfiguration&) = default;
};

struct AudioTrackFormat {
  std::int32_t sample_rate = 0;
  ChannelConfiguration channels;

  friend bool operator==(const AudioTrackFormat&,
                         const AudioTrackFormat&) = default;
};

struct TextTrackFormat {
  friend bool operator==(const TextTrackFormat&,
                         const TextTrackFormat&) = default;
};

struct HlsTrackExtension {
  std::optional<std::string> audio_group;
  std::optional<std::string> subtitle_group;
  std::optional<std::string> closed_captions_group;
  HdcpLevel hdcp_level = HdcpLevel::kUnspecified;

  friend bool operator==(const HlsTrackExtension&,
                         const HlsTrackExtension&) = default;
};

struct DashTrackExtension {
  std::optional<std::int32_t> quality_ranking;
  std::optional<std::string> mime_type;
  std::vector<std::string> dependency_ids;

  friend bool operator==(const DashTrackExtension&,
                         const DashTrackExtension&) = default;
};

using TrackExtension =
    std::variant<std::monostate, HlsTrackExtension, DashTrackExtension>;

struct Track {
  TrackId id;
  std::int64_t bandwidth_bps = 0;
  std::optional<std::int64_t> average_bandwidth_bps;
  std::vector<codec::CodecDescription> codecs;
  std::variant<VideoTrackFormat, AudioTrackFormat, TextTrackFormat> format;
  TimelineMapping timeline;
  SegmentIndexProvider segments;
  std::vector<SwitchingSetId> allowed_audio;
  TrackExtension extension;

  [[nodiscard]] penumbra::StreamType type() const;

  friend bool operator==(const Track&, const Track&) = default;
};

using SystemId = std::array<std::uint8_t, 16>;

struct InitData {
  SystemId system_id{};
  std::vector<std::uint8_t> data;

  friend bool operator==(const InitData&, const InitData&) = default;
};

struct ProtectionInfo {
  penumbra::EncryptionScheme scheme = penumbra::EncryptionScheme::kCenc;
  std::vector<SystemId> system_ids;
  std::vector<InitData> init_data;
  std::optional<std::string> key_uri;
  std::optional<std::array<std::uint8_t, 16>> default_key_id;

  friend bool operator==(const ProtectionInfo&,
                         const ProtectionInfo&) = default;
};

struct SwitchingSet {
  SwitchingSetId id;
  penumbra::StreamType type = penumbra::StreamType::kVideo;
  std::string language;
  std::vector<Role> roles;
  std::string label;
  ChannelConfiguration channels;
  bool is_default = false;
  bool autoselect = false;
  bool forced = false;
  std::optional<ProtectionInfo> protection;
  std::vector<std::shared_ptr<const Track>> tracks;

  [[nodiscard]] std::vector<penumbra::CodecId> CodecIds() const;
  [[nodiscard]] const Track* FindTrack(const TrackId& track_id) const;
};

struct Period {
  PeriodId id;
  penumbra::MediaTime start;
  penumbra::TimeDelta duration = penumbra::TimeDelta::Unknown();
  std::vector<std::shared_ptr<const SwitchingSet>> switching_sets;
  std::vector<std::shared_ptr<const Track>> trick_play;

  [[nodiscard]] penumbra::MediaTime End() const;
  [[nodiscard]] const SwitchingSet* FindSwitchingSet(
      const SwitchingSetId& set_id) const;
  [[nodiscard]] const Track* FindTrack(const TrackId& track_id) const;
  [[nodiscard]] const SwitchingSet* FindSwitchingSetOf(
      const TrackId& track_id) const;
};

struct AdBreak {
  std::string id;
  penumbra::MediaTime start;
  penumbra::TimeDelta duration = penumbra::TimeDelta::Unknown();
  std::optional<penumbra::TimeDelta> resume_offset;
  std::optional<penumbra::TimeDelta> playout_limit;

  friend bool operator==(const AdBreak&, const AdBreak&) = default;
};

struct LiveInfo {
  std::optional<absl::Time> availability_start_time;
  penumbra::MediaTime window_start;
  penumbra::MediaTime window_end;
  std::optional<penumbra::TimeDelta> time_shift_depth;
  std::optional<penumbra::TimeDelta> suggested_presentation_delay;
  std::optional<penumbra::TimeDelta> hold_back;
  std::optional<penumbra::TimeDelta> part_hold_back;
  std::optional<penumbra::TimeDelta> minimum_update_period;
  std::optional<penumbra::TimeDelta> target_segment_duration;

  friend bool operator==(const LiveInfo&, const LiveInfo&) = default;
};

struct Presentation {
  Protocol protocol = Protocol::kHls;
  PresentationType type = PresentationType::kStatic;
  penumbra::TimeDelta duration = penumbra::TimeDelta::Unknown();
  std::optional<LiveInfo> live;
  std::vector<std::shared_ptr<const Period>> periods;
  std::vector<AdBreak> ad_breaks;
  std::uint64_t generation = 0;

  [[nodiscard]] const Period* FindPeriod(penumbra::MediaTime time) const;
  [[nodiscard]] const Period* FindPeriodById(const PeriodId& period_id) const;
};

[[nodiscard]] bool operator==(const SwitchingSet& a, const SwitchingSet& b);
[[nodiscard]] bool operator==(const Period& a, const Period& b);
[[nodiscard]] bool SameContent(const Presentation& a, const Presentation& b);

[[nodiscard]] std::shared_ptr<const Track> Freeze(Track track);
[[nodiscard]] std::shared_ptr<const SwitchingSet> Freeze(SwitchingSet set);
[[nodiscard]] std::shared_ptr<const Period> Freeze(Period period);

[[nodiscard]] bool Equivalent(const Presentation& a, const Presentation& b);

}

#endif
