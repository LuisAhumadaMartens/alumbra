#include "umbra/manifest/validate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/manifest_error.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

using penumbra::StreamType;
using penumbra::TimeDelta;

constexpr std::int64_t kMaxBandwidthBps = 1'000'000'000'000;
constexpr std::int32_t kMaxDimension = 65'535;
constexpr std::int32_t kMaxSampleRate = 1'000'000;
constexpr std::int32_t kMaxChannels = 255;
constexpr std::int64_t kMaxFrameRateRatio = 1'000;

absl::Status CheckId(std::string_view kind, const std::string& id) {
  if (id.empty() || id.size() > kMaxIdBytes) {
    return InvalidManifest(absl::StrCat(kind, " id is empty or longer than ",
                                        kMaxIdBytes, " bytes"));
  }
  return absl::OkStatus();
}

absl::Status CheckText(std::string_view what, const std::string& text) {
  if (text.size() > kMaxStringBytes) {
    return InvalidManifest(
        absl::StrCat(what, " is longer than ", kMaxStringBytes, " bytes"));
  }
  return absl::OkStatus();
}

absl::Status CheckOptionalDelta(std::string_view what,
                                const std::optional<TimeDelta>& value) {
  if (value.has_value() &&
      (!value->is_finite() || value->InMicroseconds() < 0)) {
    return InvalidManifest(
        absl::StrCat(what, " must be a finite, non-negative duration"));
  }
  return absl::OkStatus();
}

absl::Status ValidateVideoFormat(const VideoTrackFormat& video) {
  const bool sized = video.width > 0 && video.height > 0;
  const bool unsized = video.width == 0 && video.height == 0;
  if ((!sized && !unsized) || video.width > kMaxDimension ||
      video.height > kMaxDimension) {
    return InvalidManifest("a video track has an invalid resolution");
  }
  if (video.frame_rate.has_value() &&
      (!video.frame_rate->is_positive() ||
       video.frame_rate->numerator() >
           kMaxFrameRateRatio * video.frame_rate->denominator())) {
    return InvalidManifest("a video track has an invalid frame rate");
  }
  if (video.bit_depth != 0 && (video.bit_depth < 8 || video.bit_depth > 16)) {
    return InvalidManifest("a video track has an invalid bit depth");
  }
  return absl::OkStatus();
}

absl::Status ValidateAudioFormat(const AudioTrackFormat& audio) {
  if (audio.sample_rate < 0 || audio.sample_rate > kMaxSampleRate ||
      audio.channels.channel_count < 0 ||
      audio.channels.channel_count > kMaxChannels) {
    return InvalidManifest("an audio track has an invalid format");
  }
  return absl::OkStatus();
}

absl::Status ValidateCodecs(const Track& track) {
  if (track.codecs.size() > kMaxCodecsPerTrack) {
    return InvalidManifest("a track lists too many codecs");
  }
  const StreamType type = track.type();
  if (type == StreamType::kVideo) {
    return absl::OkStatus();
  }
  for (const codec::CodecDescription& description : track.codecs) {
    const penumbra::CodecId id = codec::CodecIdOf(description);
    const bool audio_codec =
        id == penumbra::CodecId::kAac || id == penumbra::CodecId::kOpus ||
        id == penumbra::CodecId::kAc3 || id == penumbra::CodecId::kEac3 ||
        id == penumbra::CodecId::kFlac || id == penumbra::CodecId::kMp3;
    if (type == StreamType::kText || !audio_codec) {
      return InvalidManifest(absl::StrCat("track ", track.id.Value(),
                                          " lists a codec of another type"));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateIndex(const Track& track) {
  const SegmentIndex* index = track.segments.Index();
  if (index == nullptr) {
    if (!track.segments.Location().has_value()) {
      return InvalidManifest(
          absl::StrCat("track ", track.id.Value(),
                       " has neither a segment index nor one to fetch"));
    }
    return absl::OkStatus();
  }
  if (index->size() > static_cast<std::size_t>(kMaxSegmentsPerIndex)) {
    return InvalidManifest("a segment index has too many segments");
  }
  return absl::OkStatus();
}

absl::Status ValidateProtection(const ProtectionInfo& protection) {
  if (protection.system_ids.size() > kMaxProtectionEntries ||
      protection.init_data.size() > kMaxProtectionEntries) {
    return InvalidManifest("a switching set lists too many protection entries");
  }
  const std::set<SystemId> systems(protection.system_ids.begin(),
                                   protection.system_ids.end());
  if (systems.size() != protection.system_ids.size()) {
    return InvalidManifest("a protection system id is listed twice");
  }
  for (const InitData& init : protection.init_data) {
    if (init.data.size() > kMaxInitDataBytes) {
      return InvalidManifest("protection init data is too large");
    }
  }
  if (protection.key_uri.has_value()) {
    if (protection.key_uri->empty()) {
      return InvalidManifest("a protection key URI is empty");
    }
    return CheckText("a protection key URI", *protection.key_uri);
  }
  return absl::OkStatus();
}

absl::Status ValidateTrackIn(const Track& track, Protocol protocol) {
  if (absl::Status status = ValidateTrack(track); !status.ok()) {
    return status;
  }
  const bool mismatched =
      (protocol == Protocol::kHls &&
       std::holds_alternative<DashTrackExtension>(track.extension)) ||
      (protocol == Protocol::kDash &&
       std::holds_alternative<HlsTrackExtension>(track.extension));
  if (mismatched) {
    return InvalidManifest(absl::StrCat(
        "track ", track.id.Value(), " carries the other protocol's extension"));
  }
  return absl::OkStatus();
}

struct PeriodScan {
  std::set<TrackId> track_ids;
  std::set<SwitchingSetId> set_ids;
  std::set<SwitchingSetId> audio_set_ids;
};

absl::Status ValidateSets(const Period& period, Protocol protocol,
                          PeriodScan& scan) {
  for (const std::shared_ptr<const SwitchingSet>& set : period.switching_sets) {
    if (set == nullptr) {
      return InvalidManifest("a period holds a null switching set");
    }
    if (absl::Status status = ValidateSwitchingSet(*set); !status.ok()) {
      return status;
    }
    if (!scan.set_ids.insert(set->id).second) {
      return InvalidManifest(absl::StrCat("switching set id ", set->id.Value(),
                                          " is repeated in period ",
                                          period.id.Value()));
    }
    if (set->type == StreamType::kAudio) {
      scan.audio_set_ids.insert(set->id);
    }
    for (const std::shared_ptr<const Track>& track : set->tracks) {
      if (absl::Status status = ValidateTrackIn(*track, protocol);
          !status.ok()) {
        return status;
      }
      if (!scan.track_ids.insert(track->id).second) {
        return InvalidManifest(absl::StrCat("track id ", track->id.Value(),
                                            " is repeated in period ",
                                            period.id.Value()));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateTrickPlay(const Period& period, Protocol protocol,
                               PeriodScan& scan) {
  for (const std::shared_ptr<const Track>& track : period.trick_play) {
    if (track == nullptr) {
      return InvalidManifest("a period holds a null trick-play track");
    }
    if (absl::Status status = ValidateTrackIn(*track, protocol); !status.ok()) {
      return status;
    }
    if (track->type() != StreamType::kVideo) {
      return InvalidManifest("a trick-play track must be video");
    }
    if (!scan.track_ids.insert(track->id).second) {
      return InvalidManifest(absl::StrCat("trick-play track id ",
                                          track->id.Value(), " is repeated"));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateCoupling(const Period& period, const PeriodScan& scan) {
  for (const std::shared_ptr<const SwitchingSet>& set : period.switching_sets) {
    for (const std::shared_ptr<const Track>& track : set->tracks) {
      for (const SwitchingSetId& audio : track->allowed_audio) {
        if (!scan.audio_set_ids.contains(audio)) {
          return InvalidManifest(
              absl::StrCat("track ", track->id.Value(),
                           " lists an audio set that is not in its period"));
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidatePeriodContents(const Period& period, Protocol protocol) {
  if (period.switching_sets.empty() ||
      period.switching_sets.size() > kMaxSwitchingSetsPerPeriod ||
      period.trick_play.size() > kMaxTrickPlayTracksPerPeriod) {
    return InvalidManifest(
        absl::StrCat("period ", period.id.Value(),
                     " has no switching sets or too many sets or tracks"));
  }
  PeriodScan scan;
  if (absl::Status status = ValidateSets(period, protocol, scan);
      !status.ok()) {
    return status;
  }
  if (absl::Status status = ValidateTrickPlay(period, protocol, scan);
      !status.ok()) {
    return status;
  }
  return ValidateCoupling(period, scan);
}

absl::Status ValidatePeriods(const Presentation& presentation) {
  if (presentation.periods.empty() ||
      presentation.periods.size() > kMaxPeriods) {
    return InvalidManifest(
        "a presentation needs between one and "
        "65536 periods");
  }
  std::set<PeriodId> ids;
  const Period* previous = nullptr;
  for (std::size_t i = 0; i < presentation.periods.size(); ++i) {
    const Period* period = presentation.periods[i].get();
    if (period == nullptr) {
      return InvalidManifest("a presentation holds a null period");
    }
    if (absl::Status status = CheckId("period", period->id.Value());
        !status.ok()) {
      return status;
    }
    if (!ids.insert(period->id).second) {
      return InvalidManifest(
          absl::StrCat("period id ", period->id.Value(), " is repeated"));
    }
    if (!period->start.is_finite() || period->start.InMicroseconds() < 0) {
      return InvalidManifest(absl::StrCat(
          "period ", period->id.Value(), " has no finite, non-negative start"));
    }
    const bool last = i + 1 == presentation.periods.size();
    const bool open_ended = last &&
                            presentation.type == PresentationType::kDynamic &&
                            period->duration.is_unknown();
    if (!open_ended && (!period->duration.is_finite() ||
                        period->duration.InMicroseconds() <= 0 ||
                        !period->End().is_finite())) {
      return InvalidManifest(absl::StrCat(
          "period ", period->id.Value(), " needs a positive, finite duration"));
    }
    if (previous != nullptr && period->start < previous->End()) {
      return InvalidManifest(absl::StrCat("period ", period->id.Value(),
                                          " overlaps the period before it"));
    }
    if (absl::Status status =
            ValidatePeriodContents(*period, presentation.protocol);
        !status.ok()) {
      return status;
    }
    previous = period;
  }
  return absl::OkStatus();
}

absl::Status ValidateLive(const LiveInfo& live) {
  if (!live.window_start.is_finite() || !live.window_end.is_finite() ||
      live.window_end < live.window_start) {
    return InvalidManifest("the live availability window is invalid");
  }
  const std::array<absl::Status, 6> statuses = {
      CheckOptionalDelta("time-shift depth", live.time_shift_depth),
      CheckOptionalDelta("suggested presentation delay",
                         live.suggested_presentation_delay),
      CheckOptionalDelta("hold-back", live.hold_back),
      CheckOptionalDelta("part hold-back", live.part_hold_back),
      CheckOptionalDelta("minimum update period", live.minimum_update_period),
      CheckOptionalDelta("target segment duration",
                         live.target_segment_duration)};
  for (const absl::Status& status : statuses) {
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateAdBreaks(const Presentation& presentation) {
  if (presentation.ad_breaks.size() > kMaxAdBreaks) {
    return InvalidManifest("a presentation lists too many ad breaks");
  }
  std::set<std::string> ids;
  for (const AdBreak& ad_break : presentation.ad_breaks) {
    if (absl::Status status = CheckId("ad break", ad_break.id); !status.ok()) {
      return status;
    }
    if (!ids.insert(ad_break.id).second) {
      return InvalidManifest(
          absl::StrCat("ad break id ", ad_break.id, " is repeated"));
    }
    if (!ad_break.start.is_finite() || ad_break.start.InMicroseconds() < 0) {
      return InvalidManifest("an ad break has no finite, non-negative start");
    }
    if (!ad_break.duration.is_unknown() &&
        (!ad_break.duration.is_finite() ||
         ad_break.duration.InMicroseconds() < 0)) {
      return InvalidManifest("an ad break has an invalid duration");
    }
    if (ad_break.resume_offset.has_value() &&
        !ad_break.resume_offset->is_finite()) {
      return InvalidManifest("an ad break has an invalid resume offset");
    }
    if (absl::Status status = CheckOptionalDelta("an ad break playout limit",
                                                 ad_break.playout_limit);
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateBandwidth(const Track& track, bool media) {
  const bool hls_bandwidth_may_be_unknown =
      std::holds_alternative<HlsTrackExtension>(track.extension);
  if (track.bandwidth_bps.has_value()) {
    const std::int64_t bandwidth = *track.bandwidth_bps;
    if (bandwidth < 0 || bandwidth > kMaxBandwidthBps ||
        (media && bandwidth == 0)) {
      return InvalidManifest(absl::StrCat("track ", track.id.Value(),
                                          " has an invalid bandwidth"));
    }
    return absl::OkStatus();
  }
  if (media && !hls_bandwidth_may_be_unknown) {
    return InvalidManifest(
        absl::StrCat("track ", track.id.Value(), " has an invalid bandwidth"));
  }
  return absl::OkStatus();
}

}

absl::Status ValidateTrack(const Track& track) {
  if (absl::Status status = CheckId("track", track.id.Value()); !status.ok()) {
    return status;
  }
  const StreamType type = track.type();
  const bool media = type != StreamType::kText;
  if (absl::Status status = ValidateBandwidth(track, media); !status.ok()) {
    return status;
  }
  if (track.average_bandwidth_bps.has_value() &&
      (*track.average_bandwidth_bps < 0 ||
       *track.average_bandwidth_bps > kMaxBandwidthBps)) {
    return InvalidManifest(absl::StrCat("track ", track.id.Value(),
                                        " has an invalid average bandwidth"));
  }
  if (const auto* video = std::get_if<VideoTrackFormat>(&track.format)) {
    if (absl::Status status = ValidateVideoFormat(*video); !status.ok()) {
      return status;
    }
  }
  if (const auto* audio = std::get_if<AudioTrackFormat>(&track.format)) {
    if (absl::Status status = ValidateAudioFormat(*audio); !status.ok()) {
      return status;
    }
  }
  if (absl::Status status = ValidateCodecs(track); !status.ok()) {
    return status;
  }
  if (!track.timeline.media_origin.is_finite() ||
      !track.timeline.presentation_origin.is_finite()) {
    return InvalidManifest(absl::StrCat("track ", track.id.Value(),
                                        " has an unbounded timeline mapping"));
  }
  if (!track.allowed_audio.empty() && type != StreamType::kVideo) {
    return InvalidManifest("only a video track can list allowed audio sets");
  }
  const std::set<SwitchingSetId> audio(track.allowed_audio.begin(),
                                       track.allowed_audio.end());
  if (audio.size() != track.allowed_audio.size() ||
      track.allowed_audio.size() > kMaxSwitchingSetsPerPeriod) {
    return InvalidManifest("a track lists an allowed audio set twice");
  }
  if (const auto* dash = std::get_if<DashTrackExtension>(&track.extension)) {
    if (dash->dependency_ids.size() > kMaxTracksPerSwitchingSet) {
      return InvalidManifest("a DASH track lists too many dependencies");
    }
  }
  return ValidateIndex(track);
}

absl::Status ValidateSwitchingSet(const SwitchingSet& set) {
  if (absl::Status status = CheckId("switching set", set.id.Value());
      !status.ok()) {
    return status;
  }
  if (absl::Status status = CheckText("a language", set.language);
      !status.ok()) {
    return status;
  }
  if (absl::Status status = CheckText("a label", set.label); !status.ok()) {
    return status;
  }
  if (set.tracks.empty() || set.tracks.size() > kMaxTracksPerSwitchingSet) {
    return InvalidManifest(absl::StrCat("switching set ", set.id.Value(),
                                        " has no tracks or too many"));
  }
  if (set.roles.size() > kMaxCodecsPerTrack) {
    return InvalidManifest("a switching set lists too many roles");
  }
  if (set.channels.channel_count < 0 ||
      set.channels.channel_count > kMaxChannels) {
    return InvalidManifest("a switching set has an invalid channel count");
  }
  if (set.protection.has_value()) {
    if (absl::Status status = ValidateProtection(*set.protection);
        !status.ok()) {
      return status;
    }
  }
  for (const std::shared_ptr<const Track>& track : set.tracks) {
    if (track == nullptr) {
      return InvalidManifest("a switching set holds a null track");
    }
    if (track->type() != set.type) {
      return InvalidManifest(absl::StrCat("track ", track->id.Value(),
                                          " is not of the type of set ",
                                          set.id.Value()));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidatePresentation(const Presentation& presentation) {
  const bool dynamic = presentation.type == PresentationType::kDynamic;
  if (dynamic != presentation.live.has_value()) {
    return InvalidManifest(
        "live information must be present exactly when the presentation is "
        "dynamic");
  }
  if (!dynamic && (!presentation.duration.is_finite() ||
                   presentation.duration.InMicroseconds() < 0)) {
    return InvalidManifest(
        "a static presentation needs a finite, non-negative duration");
  }
  if (dynamic && !presentation.duration.is_unknown() &&
      (!presentation.duration.is_finite() ||
       presentation.duration.InMicroseconds() < 0)) {
    return InvalidManifest("a dynamic presentation has an invalid duration");
  }
  if (presentation.live.has_value()) {
    if (absl::Status status = ValidateLive(*presentation.live); !status.ok()) {
      return status;
    }
  }
  if (absl::Status status = ValidatePeriods(presentation); !status.ok()) {
    return status;
  }
  return ValidateAdBreaks(presentation);
}

absl::StatusOr<std::shared_ptr<const Presentation>> BuildPresentation(
    Presentation draft) {
  if (absl::Status status = ValidatePresentation(draft); !status.ok()) {
    return status;
  }
  return std::make_shared<const Presentation>(std::move(draft));
}

}
