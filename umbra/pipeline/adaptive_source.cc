#include "umbra/pipeline/adaptive_source.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/abr/bandwidth_estimator.h"
#include "umbra/abr/policy.h"
#include "umbra/abr/rendition_filter.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"
#include "umbra/base/url_redactor.h"
#include "umbra/codec/codec_string.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/demux/ts_demuxer.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/content_steering.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/fetch/recovery_ladder.h"
#include "umbra/fetch/segment_fetcher.h"
#include "umbra/manifest/dash_live.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/pipeline/buffer_manager.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::pipeline {
namespace {

constexpr size_t kMaxDemuxSteps = 100000;
constexpr uint32_t kVideoPipelineTrack = 1;
constexpr uint32_t kAudioPipelineTrack = 2;

base::ErrorDetail ErrorFor(const absl::Status& status) {
  const base::ErrorCode code =
      status.code() == absl::StatusCode::kInvalidArgument ||
              status.code() == absl::StatusCode::kFailedPrecondition
          ? base::ErrorCode::kManifestInvalid
          : base::ErrorCode::kMediaCorrupt;
  const base::ErrorCatalogueEntry* entry = base::FindError(code);
  assert(entry != nullptr);
  return {code,
          entry->category,
          entry->severity,
          entry->retryable,
          {{"status", std::string(absl::StatusCodeToString(status.code()))}},
          nullptr};
}

std::shared_ptr<const manifest::Track> PickTrack(
    const manifest::Presentation& presentation, penumbra::StreamType type,
    const std::optional<manifest::TrackId>& requested = std::nullopt) {
  if (requested.has_value()) {
    for (const auto& period : presentation.periods) {
      for (const auto& set : period->switching_sets) {
        if (set->type != type) {
          continue;
        }
        for (const auto& track : set->tracks) {
          if (track->id == *requested) {
            return track;
          }
        }
      }
    }
  }
  for (const auto& period : presentation.periods) {
    for (const auto& set : period->switching_sets) {
      if (set->type != type || set->tracks.empty()) {
        continue;
      }
      return set->tracks.front();
    }
  }
  return nullptr;
}

std::shared_ptr<const manifest::Track> FindManifestTrack(
    const manifest::Presentation& presentation,
    const manifest::TrackId& track_id) {
  for (const auto& period : presentation.periods) {
    if (period == nullptr) {
      continue;
    }
    for (const auto& set : period->switching_sets) {
      if (set == nullptr) {
        continue;
      }
      for (const auto& track : set->tracks) {
        if (track != nullptr && track->id == track_id) {
          return track;
        }
      }
    }
  }
  return nullptr;
}

class FixedLoadPolicy final : public fetch::LoadPolicy {
 public:
  explicit FixedLoadPolicy(BufferManager::Config config) : config_(config) {}

  [[nodiscard]] fetch::LoadWindow WindowFor(
      fetch::StreamKind kind) const override {
    return windows_.at(static_cast<size_t>(kind));
  }

  void Set(penumbra::MediaTime position) {
    const penumbra::TimeDelta goal =
        penumbra::TimeDelta::FromMilliseconds(config_.forward_goal_ms);
    windows_.at(static_cast<size_t>(fetch::StreamKind::kVideo)) = {
        .demand_until = position + goal,
        .prefetch_until = position + goal,
        .byte_budget = config_.video_max_bytes};
    windows_.at(static_cast<size_t>(fetch::StreamKind::kAudio)) = {
        .demand_until = position + goal,
        .prefetch_until = position + goal,
        .byte_budget = config_.audio_max_bytes};
  }

 private:
  BufferManager::Config config_;
  std::array<fetch::LoadWindow, fetch::kStreamKindCount> windows_;
};

std::string EncodeQueryValue(std::string_view value) {
  constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5',
                                         '6', '7', '8', '9', 'A', 'B',
                                         'C', 'D', 'E', 'F'};
  std::string encoded;
  for (const unsigned char character : value) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '_' || character == '.' || character == '~') {
      encoded.push_back(static_cast<char>(character));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex.at(character >> 4));
      encoded.push_back(kHex.at(character & 0x0f));
    }
  }
  return encoded;
}

std::string CloneUri(std::string uri, const fetch::PathwayClone& clone) {
  const size_t scheme = uri.find("://");
  if (!clone.host.empty() && scheme != std::string::npos) {
    const size_t authority = scheme + 3;
    const size_t end = uri.find_first_of("/?#", authority);
    uri.replace(authority,
                end == std::string::npos ? std::string::npos : end - authority,
                clone.host);
  }
  const size_t fragment_position = uri.find('#');
  const std::string fragment = fragment_position == std::string::npos
                                   ? std::string()
                                   : uri.substr(fragment_position);
  if (fragment_position != std::string::npos) {
    uri.resize(fragment_position);
  }
  for (const auto& [key, value] : clone.query_parameters) {
    uri.push_back(uri.find('?') == std::string::npos ? '?' : '&');
    uri.append(EncodeQueryValue(key));
    uri.push_back('=');
    uri.append(EncodeQueryValue(value));
  }
  uri.append(fragment);
  return uri;
}

std::shared_ptr<const manifest::Track> CloneTrack(
    const std::shared_ptr<const manifest::Track>& track,
    const fetch::PathwayClone& clone) {
  const manifest::SegmentIndex* index = track->segments.Index();
  if (index == nullptr ||
      index->IndexKind() != manifest::SegmentIndex::Kind::kExplicit) {
    return nullptr;
  }
  manifest::ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = index->FirstSequenceNumber();
  parameters.init_sections = index->InitSections();
  parameters.key_references = index->KeyReferences();
  for (manifest::InitSection& section : parameters.init_sections) {
    section.uri = CloneUri(std::move(section.uri), clone);
  }
  for (manifest::KeyReference& key : parameters.key_references) {
    key.uri = CloneUri(std::move(key.uri), clone);
  }
  parameters.segments.reserve(index->size());
  for (size_t position = 0; position < index->size(); ++position) {
    manifest::Segment segment = index->At(position);
    segment.uri = CloneUri(std::move(segment.uri), clone);
    for (manifest::HlsPart& part : segment.hls_parts) {
      part.uri = CloneUri(std::move(part.uri), clone);
    }
    parameters.segments.push_back(std::move(segment));
  }
  auto cloned_index =
      manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  if (!cloned_index.ok()) {
    return nullptr;
  }
  manifest::Track cloned = *track;
  cloned.segments = manifest::SegmentIndexProvider::Resolved(*cloned_index);
  auto* extension = std::get_if<manifest::HlsTrackExtension>(&cloned.extension);
  if (extension != nullptr) {
    extension->pathway_id = clone.id;
  }
  return manifest::Freeze(std::move(cloned));
}

class PresentationRecovery final : public fetch::RedundancyProvider,
                                   public fetch::RenditionControl {
 public:
  PresentationRecovery(std::shared_ptr<const manifest::Presentation> model,
                       std::string base_url)
      : model_(std::move(model)), base_url_(std::move(base_url)) {}

  std::vector<fetch::RedundantSource> SourcesFor(
      const manifest::TrackId& id) override {
    std::vector<std::shared_ptr<const manifest::Track>> tracks =
        TracksWithId(id);
    if (tracks.empty()) {
      return {};
    }
    const auto& primary = tracks.front();
    if (const auto* hls =
            std::get_if<manifest::HlsTrackExtension>(&primary->extension)) {
      return HlsSources(*hls, primary);
    }
    if (const auto* dash =
            std::get_if<manifest::DashTrackExtension>(&primary->extension);
        dash != nullptr && !dash->base_urls.empty()) {
      return DashSources(primary, *dash);
    }
    return {{.pathway_id = base_url_, .track = primary, .base_url = base_url_}};
  }

  void OnPathwayClones(
      const std::vector<fetch::PathwayClone>& clones) override {
    clones_ = clones;
  }

  void ExcludeRendition(fetch::StreamKind kind, const manifest::TrackId& track,
                        base::TimeTicks until) override {
    static_cast<void>(kind);
    excluded_[track] = until;
  }

  void RestoreRendition(fetch::StreamKind kind,
                        const manifest::TrackId& track) override {
    static_cast<void>(kind);
    excluded_.erase(track);
  }

  std::optional<fetch::StreamSelection> SelectAlternative(
      fetch::StreamKind kind, const manifest::TrackId& failed) override {
    const penumbra::StreamType type = kind == fetch::StreamKind::kVideo
                                          ? penumbra::StreamType::kVideo
                                          : penumbra::StreamType::kAudio;
    for (const auto& period : model_->periods) {
      for (const auto& set : period->switching_sets) {
        if (set->type != type) {
          continue;
        }
        for (const auto& track : set->tracks) {
          if (track->id != failed && !excluded_.contains(track->id)) {
            return fetch::StreamSelection{
                .track = track,
                .base_url = base_url_,
                .dynamic =
                    model_->type == manifest::PresentationType::kDynamic};
          }
        }
      }
    }
    return std::nullopt;
  }

 private:
  [[nodiscard]] std::vector<std::shared_ptr<const manifest::Track>>
  TracksWithId(const manifest::TrackId& id) const {
    std::vector<std::shared_ptr<const manifest::Track>> tracks;
    for (const auto& period : model_->periods) {
      for (const auto& set : period->switching_sets) {
        for (const auto& track : set->tracks) {
          if (track->id == id) {
            tracks.push_back(track);
          }
        }
      }
    }
    return tracks;
  }

  [[nodiscard]] static bool IsHlsRedundant(
      const manifest::HlsTrackExtension& primary,
      const manifest::Track& candidate,
      const manifest::HlsTrackExtension* extension,
      const manifest::Track& primary_track) {
    if (extension == nullptr) {
      return false;
    }
    if (primary.stable_variant_id.has_value()) {
      return extension->stable_variant_id == primary.stable_variant_id;
    }
    return candidate.bandwidth_bps == primary_track.bandwidth_bps &&
           candidate.codecs == primary_track.codecs &&
           candidate.type() == primary_track.type();
  }

  [[nodiscard]] std::vector<fetch::RedundantSource> HlsSources(
      const manifest::HlsTrackExtension& primary,
      const std::shared_ptr<const manifest::Track>& primary_track) const {
    std::vector<fetch::RedundantSource> sources;
    for (const auto& period : model_->periods) {
      for (const auto& set : period->switching_sets) {
        for (const auto& track : set->tracks) {
          const auto* extension =
              std::get_if<manifest::HlsTrackExtension>(&track->extension);
          if (!IsHlsRedundant(primary, *track, extension, *primary_track)) {
            continue;
          }
          sources.push_back({.pathway_id = extension->pathway_id.value_or(""),
                             .track = track,
                             .base_url = base_url_});
          AddClones(track, *extension, sources);
        }
      }
    }
    return sources;
  }

  void AddClones(const std::shared_ptr<const manifest::Track>& track,
                 const manifest::HlsTrackExtension& extension,
                 std::vector<fetch::RedundantSource>& sources) const {
    for (const fetch::PathwayClone& clone : clones_) {
      if (extension.pathway_id != clone.base_id) {
        continue;
      }
      std::shared_ptr<const manifest::Track> cloned = CloneTrack(track, clone);
      if (cloned != nullptr) {
        sources.push_back({.pathway_id = clone.id,
                           .track = std::move(cloned),
                           .base_url = base_url_});
      }
    }
  }

  [[nodiscard]] static std::vector<fetch::RedundantSource> DashSources(
      const std::shared_ptr<const manifest::Track>& track,
      const manifest::DashTrackExtension& dash) {
    std::vector<fetch::RedundantSource> sources;
    sources.reserve(dash.base_urls.size());
    for (const std::string& url : dash.base_urls) {
      sources.push_back({.pathway_id = url, .track = track, .base_url = url});
    }
    return sources;
  }

  std::shared_ptr<const manifest::Presentation> model_;
  std::string base_url_;
  std::vector<fetch::PathwayClone> clones_;
  std::map<manifest::TrackId, base::TimeTicks> excluded_;
};

class AdaptiveRecoveryTarget final : public fetch::RecoveryTarget {
 public:
  using SelectionCallback =
      std::function<void(fetch::StreamKind, const fetch::StreamSelection&)>;

  AdaptiveRecoveryTarget(fetch::SegmentFetcher* fetcher,
                         SelectionCallback selection_callback)
      : fetcher_(fetcher), selection_callback_(std::move(selection_callback)) {}

  absl::Status SelectTrack(fetch::StreamKind kind,
                           fetch::StreamSelection selection,
                           fetch::SwitchMode mode) override {
    const absl::Status status = fetcher_->SelectTrack(kind, selection, mode);
    if (status.ok()) {
      selection_callback_(kind, selection);
      fetcher_->Retry(kind);
    }
    return status;
  }

  void Retry(fetch::StreamKind kind) override { fetcher_->Retry(kind); }
  void Resync(fetch::StreamKind kind) override { fetcher_->Retry(kind); }
  void ClearTrack(fetch::StreamKind kind) override {
    fetcher_->ClearTrack(kind);
  }

 private:
  fetch::SegmentFetcher* fetcher_;
  SelectionCallback selection_callback_;
};

}

class AdaptiveSource::Impl final : public fetch::SegmentSink,
                                   public fetch::TransferTimingSink,
                                   public std::enable_shared_from_this<Impl> {
 private:
  struct Queue;

 public:
  class OutputAdapter final : public demux::DemuxerOutput {
   public:
    OutputAdapter(Impl* owner, fetch::StreamKind kind)
        : owner_(owner), kind_(kind) {}

    void OnStreams(std::vector<penumbra::StreamInfo> streams) override {
      owner_->OnDemuxStreams(kind_, std::move(streams));
    }
    void OnMetadataStream(const demux::MetadataStreamInfo& stream) override {
      if (owner_->config_.text_observer != nullptr) {
        owner_->config_.text_observer->OnMetadataStream(stream);
      }
    }
    void OnSeekMap(std::shared_ptr<const demux::SeekMap> seek_map) override {
      static_cast<void>(seek_map);
    }
    void OnSample(penumbra::EncodedSample sample) override {
      owner_->OnDemuxSample(kind_, std::move(sample));
    }
    void OnTimedMetadata(demux::TimedMetadataItem item) override {
      owner_->OnDemuxTimedMetadata(kind_, std::move(item));
    }
    void OnProducerReferenceTime(
        const demux::ProducerReferenceTime& reference) override {
      static_cast<void>(reference);
    }

   private:
    Impl* owner_;
    fetch::StreamKind kind_;
  };

  explicit Impl(AdaptiveSourceConfig config)
      : config_(std::move(config)),
        fixed_policy_(std::make_shared<FixedLoadPolicy>(config_.buffer)),
        policy_(config_.load_policy == nullptr
                    ? std::static_pointer_cast<fetch::LoadPolicy>(fixed_policy_)
                    : config_.load_policy),
        video_track_(
            PickTrack(*config_.presentation, penumbra::StreamType::kVideo)),
        automatic_video_track_(video_track_),
        audio_track_(PickTrack(*config_.presentation,
                               penumbra::StreamType::kAudio,
                               config_.initial_audio_track)),
        bandwidth_estimator_(abr::CreateBandwidthEstimator()),
        abr_policy_(abr::CreateAbrPolicy()) {}

  absl::Status Start(std::shared_ptr<SampleSink> sink) {
    sink_ = std::move(sink);
    if (config_.presentation->protocol == manifest::Protocol::kDash &&
        config_.presentation->type == manifest::PresentationType::kDynamic) {
      const std::function<absl::Time()> wall_clock =
          config_.wall_clock == nullptr
              ? std::function<absl::Time()>([] { return absl::Now(); })
              : config_.wall_clock;
      const std::weak_ptr<Impl> weak = weak_from_this();
      manifest::ResolveDashUtcOffset(
          *config_.presentation, config_.transport, config_.runner, wall_clock,
          config_.dash_default_utc_timing_url,
          [weak, wall_clock, delay_ms = config_.dash_start_delay_ms](
              absl::StatusOr<absl::Duration> offset) {
            const std::shared_ptr<Impl> owner = weak.lock();
            if (owner == nullptr || owner->closed_) {
              return;
            }
            const absl::Duration utc_offset =
                offset.ok() ? *offset : absl::ZeroDuration();
            owner->utc_offset_ = utc_offset;
            const auto start = manifest::DashLiveStartPosition(
                *owner->config_.presentation, wall_clock(), utc_offset,
                delay_ms);
            if (!start.ok()) {
              owner->Fail(start.status());
              return;
            }
            owner->config_.initial_position = *start;
            const absl::Status status = owner->StartFetcher();
            if (!status.ok()) {
              owner->Fail(status);
            } else {
              owner->StartDashRefresher();
            }
          });
      return absl::OkStatus();
    }
    return StartFetcher();
  }

  absl::Status StartFetcher() {
    fixed_policy_->Set(config_.initial_position);
    recovery_provider_ = std::make_shared<PresentationRecovery>(
        config_.presentation, config_.manifest_url);
    fetch::RecoveryDependencies recovery_dependencies;
    recovery_dependencies.downstream = shared_from_this();
    recovery_dependencies.runner = config_.runner;
    recovery_dependencies.clock =
        config_.clock == nullptr ? &base::GetDefaultTickClock() : config_.clock;
    recovery_dependencies.redactor = std::make_shared<base::UrlRedactor>();
    recovery_dependencies.jitter =
        config_.jitter == nullptr
            ? std::static_pointer_cast<fetch::JitterSource>(
                  std::make_shared<fetch::SeededJitter>(std::random_device{}()))
            : config_.jitter;
    recovery_dependencies.redundancy = recovery_provider_;
    recovery_dependencies.renditions = recovery_provider_;
    auto recovery = fetch::RecoveryLadder::Create(
        std::move(recovery_dependencies), config_.recovery);
    if (!recovery.ok()) {
      return recovery.status();
    }
    recovery_ = std::move(*recovery);
    fetch::FetcherDependencies dependencies;
    dependencies.transport = config_.transport;
    dependencies.runner = config_.runner;
    dependencies.sink = recovery_;
    dependencies.policy = policy_;
    dependencies.timing = shared_from_this();
    dependencies.resolver = config_.index_resolver;
    auto created =
        fetch::SegmentFetcher::Create(std::move(dependencies), config_.fetch);
    if (!created.ok()) {
      return created.status();
    }
    fetcher_ = std::move(*created);
    fetch_generation_ = fetcher_->Seek(config_.initial_position);
    recovery_target_ = std::make_unique<AdaptiveRecoveryTarget>(
        fetcher_.get(), [this](fetch::StreamKind kind,
                               const fetch::StreamSelection& selection) {
          std::shared_ptr<const manifest::Track>& current =
              kind == fetch::StreamKind::kVideo ? video_track_ : audio_track_;
          current = selection.track;
        });
    recovery_->SetTarget(recovery_target_.get());
    if (config_.recovery.content_steering_enabled) {
      if (config_.presentation->hls.has_value() &&
          config_.presentation->hls->content_steering.has_value()) {
        const manifest::HlsContentSteering& steering =
            *config_.presentation->hls->content_steering;
        fetch::SteeringClientDependencies steering_dependencies;
        steering_dependencies.transport = config_.transport;
        steering_dependencies.runner = config_.runner;
        steering_dependencies.parser =
            std::make_shared<fetch::JsonSteeringParser>();
        steering_dependencies.listener = recovery_;
        fetch::SteeringClientConfig steering_config;
        steering_config.uri = steering.server_uri;
        steering_config.pathway_id = steering.pathway_id.value_or("");
        auto client = fetch::SteeringClient::Create(
            std::move(steering_dependencies), std::move(steering_config));
        if (!client.ok()) {
          return client.status();
        }
        steering_client_ = std::move(*client);
        steering_client_->Start();
      }
    }
    if (video_track_ != nullptr) {
      const absl::Status status =
          Select(fetch::StreamKind::kVideo, video_track_);
      if (!status.ok()) {
        Fail(status);
        return status;
      }
    }
    if (audio_track_ != nullptr) {
      const absl::Status status =
          Select(fetch::StreamKind::kAudio, audio_track_);
      if (!status.ok()) {
        Fail(status);
        return status;
      }
    }
    if (video_track_ == nullptr && audio_track_ == nullptr) {
      const absl::Status status =
          absl::InvalidArgumentError("presentation has no audio or video");
      Fail(status);
      return status;
    }
    return absl::OkStatus();
  }

  absl::Status UpdatePresentation(
      std::shared_ptr<const manifest::Presentation> presentation) {
    if (closed_ || fetcher_ == nullptr || presentation == nullptr) {
      return absl::FailedPreconditionError(
          "adaptive source is not open for presentation updates");
    }
    if (presentation->protocol != config_.presentation->protocol) {
      return absl::InvalidArgumentError(
          "presentation update changed the protocol");
    }
    const auto update_track =
        [this, &presentation](
            fetch::StreamKind kind,
            std::shared_ptr<const manifest::Track>& track) -> absl::Status {
      if (track == nullptr) {
        return absl::OkStatus();
      }
      std::shared_ptr<const manifest::Track> updated =
          FindManifestTrack(*presentation, track->id);
      if (updated == nullptr) {
        return absl::NotFoundError(
            "refreshed presentation no longer contains a selected track");
      }
      const absl::Status status = fetcher_->UpdateTrack(
          kind, updated,
          presentation->type == manifest::PresentationType::kDynamic,
          config_.manifest_url);
      if (status.ok()) {
        track = std::move(updated);
      }
      return status;
    };
    if (const absl::Status status =
            update_track(fetch::StreamKind::kVideo, video_track_);
        !status.ok()) {
      return status;
    }
    if (const absl::Status status =
            update_track(fetch::StreamKind::kAudio, audio_track_);
        !status.ok()) {
      return status;
    }
    const bool ended_live =
        config_.presentation->type == manifest::PresentationType::kDynamic &&
        presentation->type == manifest::PresentationType::kStatic;
    config_.presentation = std::move(presentation);
    if (ended_live && sink_ != nullptr &&
        config_.presentation->duration.is_finite()) {
      sink_->OnDurationChanged(penumbra::MediaTime::FromMicroseconds(
          config_.presentation->duration.InMicroseconds()));
    }
    return absl::OkStatus();
  }

  void StartDashRefresher() {
    if (closed_ ||
        config_.presentation->type != manifest::PresentationType::kDynamic ||
        config_.presentation->protocol != manifest::Protocol::kDash) {
      return;
    }
    const std::weak_ptr<Impl> weak = weak_from_this();
    auto refresher = manifest::DashMpdRefresher::Create(
        {.presentation = config_.presentation,
         .manifest_url = config_.manifest_url,
         .transport = config_.transport,
         .runner = config_.runner,
         .on_update =
             [weak](absl::StatusOr<manifest::DashMpdRefreshUpdate> update) {
               const std::shared_ptr<Impl> owner = weak.lock();
               if (owner == nullptr || owner->closed_ || !update.ok()) {
                 return;
               }
               owner->ApplyDashRefresh(std::move(*update));
             }});
    if (!refresher.ok()) {
      Fail(refresher.status());
      return;
    }
    dash_refresher_ = std::move(*refresher);
    dash_refresher_->Start();
  }

  void ApplyDashRefresh(manifest::DashMpdRefreshUpdate update) {
    const bool is_dynamic =
        update.presentation->type == manifest::PresentationType::kDynamic;
    const std::shared_ptr<const manifest::Presentation> presentation =
        update.presentation;
    auto apply = [weak = weak_from_this(), update = std::move(update),
                  is_dynamic](absl::StatusOr<absl::Duration> offset) mutable {
      const std::shared_ptr<Impl> owner = weak.lock();
      if (owner == nullptr || owner->closed_) {
        return;
      }
      if (is_dynamic) {
        owner->utc_offset_ = offset.ok() ? *offset : absl::ZeroDuration();
      }
      owner->config_.manifest_url = update.manifest_url;
      const absl::Status status =
          owner->UpdatePresentation(update.presentation);
      if (!status.ok()) {
        owner->Fail(status);
        return;
      }
    };
    if (!is_dynamic) {
      apply(absl::ZeroDuration());
      return;
    }
    const std::function<absl::Time()> wall_clock =
        config_.wall_clock == nullptr
            ? std::function<absl::Time()>([] { return absl::Now(); })
            : config_.wall_clock;
    manifest::ResolveDashUtcOffset(
        *presentation, config_.transport, config_.runner, wall_clock,
        config_.dash_default_utc_timing_url, std::move(apply));
  }

  void Request(penumbra::SeekTicket ticket, uint32_t track_id,
               size_t max_samples) {
    if (closed_ || sink_ == nullptr || ticket != ticket_) {
      return;
    }
    Queue& queue = QueueFor(track_id);
    queue.ticket = ticket;
    queue.grant = max_samples;
    Deliver(track_id, queue);
    fixed_policy_->Set(queue.last_end);
    fetcher_->OnBufferChanged();
  }

  void ApplyAutomaticRendition() {
    if (closed_ || fetcher_ == nullptr || video_track_ == nullptr) {
      return;
    }
    const manifest::SwitchingSet* active_set = nullptr;
    for (const auto& period : config_.presentation->periods) {
      active_set = period->FindSwitchingSetOf(video_track_->id);
      if (active_set != nullptr) {
        break;
      }
    }
    if (active_set == nullptr || active_set->tracks.size() < 2) {
      return;
    }
    abr::AbrSnapshot snapshot;
    snapshot.bandwidth_estimate_bps =
        bandwidth_estimator_->GetEstimate().bandwidth_bps;
    const penumbra::MediaTime buffered_end = video_queue_.last_end;
    if (buffered_end.is_finite() && config_.initial_position.is_finite() &&
        buffered_end > config_.initial_position) {
      snapshot.buffer_ms =
          (buffered_end - config_.initial_position).InMicroseconds() / 1000;
    }
    snapshot.now_ms = absl::ToInt64Milliseconds(
        (config_.clock == nullptr ? base::GetDefaultTickClock()
                                  : *config_.clock)
            .NowTicks()
            .SinceOrigin());
    snapshot.last_switch_ms = last_video_switch_ms_;
    snapshot.segment_duration_ms = 4000;
    snapshot.force_frame_rate_change = true;
    std::uint64_t current_id = 0;
    std::vector<std::shared_ptr<const manifest::Track>> candidates;
    for (const auto& candidate : active_set->tracks) {
      if (candidate == nullptr ||
          candidate->type() != penumbra::StreamType::kVideo) {
        continue;
      }
      const std::uint64_t id =
          static_cast<std::uint64_t>(candidates.size() + 1);
      const auto* format =
          std::get_if<manifest::VideoTrackFormat>(&candidate->format);
      const std::int32_t height = format == nullptr ? 0 : format->height;
      snapshot.candidates.push_back(
          {.id = id,
           .bitrate_bps = candidate->bandwidth_bps.value_or(0),
           .height = height,
           .frame_rate_numerator = 30,
           .frame_rate_denominator = 1,
           .score = 0.0,
           .filter_id = candidate->id.Value()});
      candidates.push_back(candidate);
      if (candidate->id == video_track_->id) {
        current_id = id;
      }
    }
    if (snapshot.candidates.size() < 2 || current_id == 0) {
      return;
    }
    snapshot.current_rendition_id = current_id;
    if (config_.settings.has_value()) {
      snapshot.filter_policy =
          abr::ReadFilterPolicy(*config_.settings, true, 0);
    }
    const abr::AbrDecision decision = abr_policy_->Decide(snapshot);
    const std::size_t selected_index =
        decision.rendition_id == 0
            ? candidates.size()
            : static_cast<std::size_t>(decision.rendition_id - 1);
    if (selected_index >= candidates.size() ||
        candidates[selected_index]->id == video_track_->id) {
      return;
    }
    const absl::Status status =
        Select(fetch::StreamKind::kVideo, candidates[selected_index]);
    if (status.ok()) {
      video_track_ = candidates[selected_index];
      automatic_video_track_ = video_track_;
      last_video_switch_ms_ = snapshot.now_ms;
    }
  }

  absl::Status SelectRendition(fetch::StreamKind kind,
                               const manifest::TrackId& id) {
    if (closed_ || fetcher_ == nullptr) {
      return absl::FailedPreconditionError("adaptive source is closed");
    }
    std::shared_ptr<const manifest::Track>& current =
        kind == fetch::StreamKind::kVideo ? video_track_ : audio_track_;
    if (current == nullptr) {
      return absl::InvalidArgumentError("rendition type is not selected");
    }
    std::shared_ptr<const manifest::Track> selected;
    if (kind == fetch::StreamKind::kAudio) {
      for (const auto& period : config_.presentation->periods) {
        for (const auto& set : period->switching_sets) {
          if (set->type != penumbra::StreamType::kAudio) {
            continue;
          }
          for (const auto& candidate : set->tracks) {
            if (candidate->id == id) {
              selected = candidate;
              break;
            }
          }
          if (selected != nullptr) {
            break;
          }
        }
        if (selected != nullptr) {
          break;
        }
      }
    } else {
      for (const auto& period : config_.presentation->periods) {
        const manifest::SwitchingSet* set =
            period->FindSwitchingSetOf(current->id);
        if (set == nullptr || set->type != current->type()) {
          continue;
        }
        for (const auto& candidate : set->tracks) {
          if (candidate->id == id) {
            selected = candidate;
            break;
          }
        }
        if (selected != nullptr) {
          break;
        }
      }
    }
    if (selected == nullptr) {
      return absl::InvalidArgumentError(
          "rendition is not in the active switching set");
    }
    if (kind == fetch::StreamKind::kVideo && !manual_video_selection_) {
      automatic_video_track_ = current;
    }
    const absl::Status status = Select(kind, selected);
    if (status.ok()) {
      current = std::move(selected);
      if (kind == fetch::StreamKind::kVideo) {
        manual_video_selection_ = true;
      }
    }
    return status;
  }

  absl::Status ClearVideoTrackSelection() {
    if (!manual_video_selection_ || automatic_video_track_ == nullptr) {
      return absl::FailedPreconditionError(
          "video rendition is not manually selected");
    }
    const absl::Status status =
        Select(fetch::StreamKind::kVideo, automatic_video_track_);
    if (!status.ok()) {
      return status;
    }
    video_track_ = automatic_video_track_;
    manual_video_selection_ = false;
    last_video_switch_ms_ = -1;
    return absl::OkStatus();
  }

  std::string SelectedAudioTrackId() const {
    return audio_track_ == nullptr ? std::string{} : audio_track_->id.Value();
  }

  std::string SelectedVideoTrackId() const {
    return video_track_ == nullptr ? std::string{} : video_track_->id.Value();
  }

  bool OnPlaylistFailover(fetch::StreamKind kind) {
    return recovery_ != nullptr && recovery_->OnPlaylistFailover(kind);
  }

  SeekPoint Resolve(penumbra::MediaTime position, SeekMode mode,
                    FastSeekSnap snap) const {
    if (config_.presentation->protocol == manifest::Protocol::kDash &&
        config_.presentation->type == manifest::PresentationType::kDynamic) {
      const std::function<absl::Time()> wall_clock =
          config_.wall_clock == nullptr
              ? std::function<absl::Time()>([] { return absl::Now(); })
              : config_.wall_clock;
      const auto window = manifest::DashAvailableWindow(
          *config_.presentation, wall_clock(), utc_offset_);
      if (window.ok()) {
        position = std::clamp(position, window->start, window->available_end);
      }
    }
    penumbra::MediaTime decode = position;
    if (mode == SeekMode::kFast) {
      decode =
          SegmentStart(video_track_, position, snap,
                       SegmentStart(audio_track_, position, snap, position));
      position = decode;
    }
    return {.presentation_time = position, .decode_time = decode};
  }

  void DoSeek(penumbra::SeekTicket ticket,
              penumbra::MediaTime decode_position) {
    if (closed_ || fetcher_ == nullptr) {
      return;
    }
    ticket_ = ticket;
    ResetQueue(video_queue_);
    ResetQueue(audio_queue_);
    fixed_policy_->Set(decode_position);
    if (recovery_ != nullptr) {
      recovery_->OnSeek();
    }
    fetch_generation_ = fetcher_->Seek(decode_position);
    for (auto& demuxer : demuxers_) {
      if (demuxer != nullptr) {
        demuxer->Reset(0);
      }
    }
    opened_ = false;
    stream_info_.fill(std::nullopt);
  }

  void Close() {
    closed_ = true;
    if (dash_refresher_ != nullptr) {
      dash_refresher_->Stop();
      dash_refresher_.reset();
    }
    sink_.reset();
    if (steering_client_ != nullptr) {
      steering_client_->Stop();
      steering_client_.reset();
    }
    recovery_.reset();
    recovery_target_.reset();
    recovery_provider_.reset();
    fetcher_.reset();
    for (auto& demuxer : demuxers_) {
      demuxer.reset();
    }
    for (auto& output : outputs_) {
      output.reset();
    }
    ResetQueue(video_queue_);
    ResetQueue(audio_queue_);
  }

  void OnTransferSample(const fetch::TransferSample& sample) override {
    if (config_.transfer_timing != nullptr) {
      config_.transfer_timing->OnTransferSample(sample);
    }
    if (sample.kind != fetch::StreamKind::kVideo ||
        sample.request_class != penumbra::RequestClass::kMedia) {
      return;
    }
    bandwidth_estimator_->AddSample(sample.bytes, sample.time_to_first_byte,
                                    sample.transfer_time);
    if (!manual_video_selection_) {
      const std::weak_ptr<Impl> weak = weak_from_this();
      static_cast<void>(config_.runner->PostTask([weak] {
        if (const std::shared_ptr<Impl> self = weak.lock()) {
          if (!self->manual_video_selection_) {
            self->ApplyAutomaticRendition();
          }
        }
      }));
    }
  }

  void OnInitSegment(fetch::InitSegmentData init) override {
    if (closed_) {
      return;
    }
    fetch_generation_ = init.generation;
    demux::Demuxer* demuxer = GetDemuxer(init.kind, init.bytes, "mp4");
    if (demuxer == nullptr) {
      Fail(absl::UnimplementedError("adaptive init format is unsupported"));
      return;
    }
    demuxer->Reset(0);
    const absl::Status status = demuxer->Append(0, init.bytes);
    if (!status.ok()) {
      Fail(status);
      return;
    }
    Pump(*demuxer, false);
  }

  void OnMediaSegment(fetch::MediaSegmentData media) override {
    if (closed_) {
      return;
    }
    fetch_generation_ = media.generation;
    Queue& queue = QueueFor(media.kind);
    queue.overlap_cutoff = penumbra::MediaTime::Unknown();
    if (queue.has_segment &&
        queue.discontinuity_sequence == media.segment.discontinuity_sequence &&
        media.segment.start < queue.segment_end) {
      queue.overlap_cutoff = queue.segment_end;
    }
    queue.segment_end = media.segment.End();
    queue.discontinuity_sequence = media.segment.discontinuity_sequence;
    queue.has_segment = true;
    const std::shared_ptr<const manifest::Track> media_track =
        FindTrack(media.track);
    if (media_track != nullptr) {
      timelines_.at(static_cast<size_t>(media.kind)) = media_track->timeline;
    }
    const std::string_view extension = Extension(media.segment.uri);
    demux::Demuxer* demuxer = GetDemuxer(media.kind, media.bytes, extension);
    if (demuxer == nullptr) {
      Fail(absl::UnimplementedError("adaptive media format is unsupported"));
      return;
    }
    demuxer->Reset(0);
    demuxer->SetSourceInfo(media.bytes.size(), false);
    const absl::Status status = demuxer->Append(0, media.bytes);
    if (!status.ok()) {
      Fail(status);
      return;
    }
    demuxer->EndOfInput();
    Pump(*demuxer, true);
  }

  void OnEndOfStream(fetch::StreamKind kind, uint64_t generation) override {
    if (closed_ || generation != fetch_generation_ || sink_ == nullptr) {
      return;
    }
    Queue& queue = QueueFor(kind);
    queue.ended = true;
    Deliver(PipelineTrack(kind), queue);
  }

  void OnStreamError(fetch::StreamKind kind, uint64_t generation,
                     absl::Status status) override {
    static_cast<void>(kind);
    static_cast<void>(generation);
    Fail(status);
  }

  void OnIndexResolved(
      fetch::StreamKind kind, const manifest::TrackId& track,
      const manifest::SegmentIndexProvider& provider) override {
    static_cast<void>(kind);
    static_cast<void>(track);
    static_cast<void>(provider);
  }

  void OnDemuxStreams(fetch::StreamKind kind,
                      std::vector<penumbra::StreamInfo> streams) {
    if (config_.text_observer != nullptr) {
      config_.text_observer->OnTextStreams(streams);
    }
    for (penumbra::StreamInfo& stream : streams) {
      const uint32_t demux_track_id = stream.track_id;
      demux_stream_info_.at(static_cast<size_t>(kind))[demux_track_id] = stream;
      const penumbra::StreamType expected = kind == fetch::StreamKind::kVideo
                                                ? penumbra::StreamType::kVideo
                                                : penumbra::StreamType::kAudio;
      if (stream.type() != expected) {
        continue;
      }
      const uint32_t id = PipelineTrack(expected);
      demux_tracks_.at(static_cast<size_t>(kind))[demux_track_id] = expected;
      stream.track_id = id;
      stream_info_.at(static_cast<size_t>(stream.type())) = std::move(stream);
    }
    MaybeOpen();
  }

  void OnDemuxSample(fetch::StreamKind kind, penumbra::EncodedSample sample) {
    const uint32_t demux_track_id = sample.track_id;
    const auto& track_map = demux_tracks_.at(static_cast<size_t>(kind));
    const auto found = track_map.find(sample.track_id);
    penumbra::StreamType type = penumbra::StreamType::kText;
    if (sample.config_change != nullptr) {
      type = sample.config_change->type();
    } else if (found != track_map.end()) {
      type = found->second;
    }
    if (type == penumbra::StreamType::kText) {
      const std::optional<manifest::TimelineMapping>& timeline =
          timelines_.at(static_cast<size_t>(kind));
      if (timeline.has_value()) {
        MapTimestamp(sample.pts, *timeline);
        MapTimestamp(sample.dts, *timeline);
      }
      const auto stream =
          demux_stream_info_.at(static_cast<size_t>(kind)).find(demux_track_id);
      if (stream != demux_stream_info_.at(static_cast<size_t>(kind)).end() &&
          config_.text_observer != nullptr) {
        config_.text_observer->OnTextSample(stream->second, std::move(sample));
      }
      return;
    }
    const uint32_t id = PipelineTrack(type);
    if (id == 0) {
      return;
    }
    sample.track_id = id;
    if (sample.config_change != nullptr) {
      auto updated =
          std::make_shared<penumbra::StreamInfo>(*sample.config_change);
      demux_tracks_.at(static_cast<size_t>(kind))[demux_track_id] = type;
      updated->track_id = id;
      sample.config_change = std::move(updated);
    }
    Queue& queue = QueueFor(id);
    const std::optional<manifest::TimelineMapping>& timeline =
        timelines_.at(static_cast<size_t>(kind));
    if (timeline.has_value()) {
      MapTimestamp(sample.pts, *timeline);
      MapTimestamp(sample.dts, *timeline);
      if (!TrimOverlap(type, queue, sample)) {
        return;
      }
    }
    UpdateLastEnd(queue, sample);
    queue.samples.push_back(std::move(sample));
    Deliver(id, queue);
  }

  void OnDemuxTimedMetadata(fetch::StreamKind kind,
                            demux::TimedMetadataItem item) {
    if (config_.text_observer == nullptr) {
      return;
    }
    const std::optional<manifest::TimelineMapping>& timeline =
        timelines_.at(static_cast<size_t>(kind));
    if (timeline.has_value()) {
      MapTimestamp(item.time, *timeline);
    }
    config_.text_observer->OnTimedMetadata(std::move(item));
  }

  static bool TrimOverlap(penumbra::StreamType type, const Queue& queue,
                          penumbra::EncodedSample& sample) {
    if (!sample.pts.is_known() || !queue.overlap_cutoff.is_finite()) {
      return true;
    }
    const std::optional<penumbra::MediaTime> sample_time =
        penumbra::ToMediaTime(sample.pts, penumbra::Rounding::kNearest);
    if (!sample_time.has_value() || *sample_time >= queue.overlap_cutoff) {
      return true;
    }
    if (type != penumbra::StreamType::kAudio || !sample.duration.is_known()) {
      return false;
    }
    const std::optional<penumbra::TimeDelta> duration =
        penumbra::ToTimeDelta(sample.duration, penumbra::Rounding::kNearest);
    if (!duration.has_value() ||
        *sample_time + *duration <= queue.overlap_cutoff) {
      return false;
    }
    const penumbra::TimeDelta trim = queue.overlap_cutoff - *sample_time;
    const std::optional<penumbra::Timestamp> trim_timestamp =
        penumbra::ToTimestamp(trim, sample.pts.time_base,
                              penumbra::Rounding::kUp);
    if (trim_timestamp.has_value()) {
      sample.trim_start = *trim_timestamp;
    }
    return true;
  }

  static void UpdateLastEnd(Queue& queue,
                            const penumbra::EncodedSample& sample) {
    if (!sample.pts.is_known()) {
      return;
    }
    const std::optional<penumbra::MediaTime> start =
        penumbra::ToMediaTime(sample.pts, penumbra::Rounding::kNearest);
    const std::optional<penumbra::TimeDelta> duration =
        penumbra::ToTimeDelta(sample.duration, penumbra::Rounding::kNearest);
    if (start.has_value() && duration.has_value()) {
      const penumbra::MediaTime end = *start + *duration;
      queue.last_end = std::max(queue.last_end, end);
    }
  }

 private:
  struct Queue {
    std::deque<penumbra::EncodedSample> samples;
    penumbra::SeekTicket ticket = 0;
    size_t grant = 0;
    penumbra::MediaTime last_end = penumbra::MediaTime::Zero();
    penumbra::MediaTime segment_end = penumbra::MediaTime::Unknown();
    penumbra::MediaTime overlap_cutoff = penumbra::MediaTime::Unknown();
    std::int64_t discontinuity_sequence = 0;
    bool has_segment = false;
    bool ended = false;
    bool eos_sent = false;
  };

  absl::Status Select(fetch::StreamKind kind,
                      const std::shared_ptr<const manifest::Track>& track) {
    fetch::StreamSelection selection;
    selection.track = track;
    selection.base_url = config_.manifest_url;
    selection.dynamic =
        config_.presentation->type == manifest::PresentationType::kDynamic;
    if (const auto* dash =
            std::get_if<manifest::DashTrackExtension>(&track->extension);
        dash != nullptr && !dash->base_urls.empty()) {
      selection.base_url = dash->base_urls.front();
    }
    return recovery_ == nullptr
               ? fetcher_->SelectTrack(kind, std::move(selection),
                                       fetch::SwitchMode::kAfterInFlight)
               : recovery_->SelectTrack(kind, selection,
                                        fetch::SwitchMode::kAfterInFlight);
  }

  std::shared_ptr<const manifest::Track> FindTrack(
      const manifest::TrackId& id) const {
    for (const auto& period : config_.presentation->periods) {
      for (const auto& set : period->switching_sets) {
        for (const auto& track : set->tracks) {
          if (track->id == id) {
            return track;
          }
        }
      }
    }
    return nullptr;
  }

  demux::Demuxer* GetDemuxer(fetch::StreamKind kind,
                             const penumbra::SharedBytes& bytes,
                             std::string_view extension) {
    const auto index = static_cast<size_t>(kind);
    if (demuxers_.at(index) != nullptr) {
      return demuxers_.at(index).get();
    }
    if (extension == "ts" || IsTransportStream(bytes)) {
      outputs_.at(index) = std::make_unique<OutputAdapter>(this, kind);
      demuxers_.at(index) = std::make_unique<demux::TsDemuxer>(
          *outputs_.at(index),
          demux::TsDemuxerOptions{.program_number = 0,
                                  .extract_captions = true,
                                  .probe_duration = false});
      return demuxers_.at(index).get();
    }
    if (extension == "mp4" || extension == "m4s" || extension == "m4v" ||
        extension == "m4a" || extension == "cmfv" || extension == "cmfa" ||
        extension.empty()) {
      outputs_.at(index) = std::make_unique<OutputAdapter>(this, kind);
      demuxers_.at(index) =
          std::make_unique<demux::Mp4Demuxer>(*outputs_.at(index));
      return demuxers_.at(index).get();
    }
    return nullptr;
  }

  static std::string_view Extension(const std::string& uri) {
    const size_t query = uri.find_first_of("?#");
    const size_t end = query == std::string::npos ? uri.size() : query;
    const size_t dot = uri.rfind('.', end);
    return dot == std::string::npos
               ? std::string_view{}
               : std::string_view(uri).substr(dot + 1, end - dot - 1);
  }

  static bool IsTransportStream(const penumbra::SharedBytes& bytes) {
    const auto data = bytes.span();
    return data.size() >= size_t{188} * 3 && data[0] == 0x47 &&
           data[188] == 0x47 && data[376] == 0x47;
  }

  static uint32_t PipelineTrack(fetch::StreamKind kind) {
    return kind == fetch::StreamKind::kVideo ? kVideoPipelineTrack
                                             : kAudioPipelineTrack;
  }

  static uint32_t PipelineTrack(penumbra::StreamType type) {
    if (type == penumbra::StreamType::kVideo) {
      return kVideoPipelineTrack;
    }
    if (type == penumbra::StreamType::kAudio) {
      return kAudioPipelineTrack;
    }
    return 0;
  }

  static void MapTimestamp(penumbra::Timestamp& timestamp,
                           const manifest::TimelineMapping& mapping) {
    if (!timestamp.is_known()) {
      return;
    }
    const std::optional<penumbra::MediaTime> media =
        penumbra::ToMediaTime(timestamp, penumbra::Rounding::kNearest);
    if (!media.has_value()) {
      return;
    }
    const std::optional<penumbra::Timestamp> mapped = penumbra::ToTimestamp(
        mapping.ToPresentation(*media), timestamp.time_base,
        penumbra::Rounding::kNearest);
    if (mapped.has_value()) {
      timestamp = *mapped;
    }
  }

  static penumbra::MediaTime SegmentStart(
      const std::shared_ptr<const manifest::Track>& track,
      penumbra::MediaTime target, FastSeekSnap snap,
      penumbra::MediaTime fallback) {
    if (track == nullptr || track->segments.Index() == nullptr) {
      return fallback;
    }
    const manifest::SegmentIndex& index = *track->segments.Index();
    if (index.empty()) {
      return fallback;
    }
    size_t first_after_target = 0;
    size_t last = index.size();
    while (first_after_target < last) {
      const size_t middle =
          first_after_target + ((last - first_after_target) / 2);
      if (index.StartOf(middle) <= target) {
        first_after_target = middle + 1;
      } else {
        last = middle;
      }
    }
    if (first_after_target == 0) {
      return index.StartOf(0);
    }
    const size_t previous = first_after_target - 1;
    if (snap == FastSeekSnap::kNearest && first_after_target < index.size()) {
      const penumbra::MediaTime previous_start = index.StartOf(previous);
      const penumbra::MediaTime next_start = index.StartOf(first_after_target);
      if (next_start - target < target - previous_start) {
        return next_start;
      }
    }
    return index.StartOf(previous);
  }

  void Pump(demux::Demuxer& demuxer, bool ending) {
    for (size_t step = 0; step < kMaxDemuxSteps; ++step) {
      auto result = demuxer.Step();
      if (!result.ok()) {
        Fail(result.status());
        return;
      }
      if (result->kind == demux::StepKind::kEndOfStream ||
          result->kind == demux::StepKind::kNeedMoreData) {
        return;
      }
      if (result->kind == demux::StepKind::kSeek) {
        Fail(absl::UnimplementedError("segment demuxer requested a byte seek"));
        return;
      }
    }
    Fail(absl::ResourceExhaustedError("segment demux step budget exceeded"));
    static_cast<void>(ending);
  }

  void MaybeOpen() {
    if (opened_ || sink_ == nullptr) {
      return;
    }
    const auto& video_info =
        stream_info_.at(static_cast<size_t>(penumbra::StreamType::kVideo));
    const auto& audio_info =
        stream_info_.at(static_cast<size_t>(penumbra::StreamType::kAudio));
    if ((video_track_ != nullptr && !video_info.has_value()) ||
        (audio_track_ != nullptr && !audio_info.has_value())) {
      return;
    }
    SourceDescription description;
    for (const auto& period : config_.presentation->periods) {
      for (const auto& set : period->switching_sets) {
        if (set->type == penumbra::StreamType::kAudio) {
          for (const auto& track : set->tracks) {
            if (track == nullptr) {
              continue;
            }
            const auto has_role = [&set](manifest::Role role) {
              return std::ranges::find(set->roles, role) != set->roles.end();
            };
            const umbra::player::AudioTrackKind kind =
                has_role(manifest::Role::kDescription)
                    ? umbra::player::AudioTrackKind::kDescriptions
                : has_role(manifest::Role::kCommentary)
                    ? umbra::player::AudioTrackKind::kCommentary
                : has_role(manifest::Role::kDub)
                    ? umbra::player::AudioTrackKind::kDub
                : has_role(manifest::Role::kSign)
                    ? umbra::player::AudioTrackKind::kSign
                : has_role(manifest::Role::kEmergency)
                    ? umbra::player::AudioTrackKind::kEmergency
                : has_role(manifest::Role::kMain)
                    ? umbra::player::AudioTrackKind::kMain
                    : umbra::player::AudioTrackKind::kAlternative;
            description.audio_tracks.push_back(
                {.id = track->id.Value(),
                 .kind = kind,
                 .label = set->label,
                 .language = set->language,
                 .enabled =
                     audio_track_ != nullptr && audio_track_->id == track->id});
          }
        } else if (set->type == penumbra::StreamType::kVideo) {
          for (const auto& track : set->tracks) {
            if (track == nullptr) {
              continue;
            }
            const auto has_role = [&set](manifest::Role role) {
              return std::ranges::find(set->roles, role) != set->roles.end();
            };
            const umbra::player::VideoTrackKind kind =
                has_role(manifest::Role::kDescription)
                    ? umbra::player::VideoTrackKind::kDescriptions
                : has_role(manifest::Role::kCommentary)
                    ? umbra::player::VideoTrackKind::kCommentary
                : has_role(manifest::Role::kEmergency)
                    ? umbra::player::VideoTrackKind::kEmergency
                : has_role(manifest::Role::kCaption)
                    ? umbra::player::VideoTrackKind::kCaptions
                : has_role(manifest::Role::kSubtitle)
                    ? umbra::player::VideoTrackKind::kSubtitles
                : has_role(manifest::Role::kSign)
                    ? umbra::player::VideoTrackKind::kSign
                : has_role(manifest::Role::kMain)
                    ? umbra::player::VideoTrackKind::kMain
                    : umbra::player::VideoTrackKind::kAlternative;
            const auto* format =
                std::get_if<manifest::VideoTrackFormat>(&track->format);
            description.video_tracks.push_back(
                {.id = track->id.Value(),
                 .kind = kind,
                 .label = set->label,
                 .language = set->language,
                 .selected =
                     video_track_ != nullptr && video_track_->id == track->id,
                 .height = format == nullptr ? 0 : format->height,
                 .bitrate_bps = track->bandwidth_bps.value_or(0),
                 .codec = track->codecs.empty() ? std::string{}
                                                : codec::FormatCodecString(
                                                      track->codecs.front())});
          }
        }
      }
      if (!period->switching_sets.empty()) {
        break;
      }
    }
    if (video_info.has_value() && video_track_ != nullptr) {
      description.streams.push_back(
          {.info = video_info.value(),
           .encryption_scheme = std::nullopt,
           .output_mode = penumbra::OutputMode::kTexture});
    }
    if (audio_info.has_value() && audio_track_ != nullptr) {
      description.streams.push_back(
          {.info = audio_info.value(),
           .encryption_scheme = std::nullopt,
           .output_mode = penumbra::OutputMode::kTexture});
    }
    if (config_.presentation->type == manifest::PresentationType::kDynamic) {
      description.duration = penumbra::MediaTime::Infinite();
    } else if (config_.presentation->duration.is_finite()) {
      description.duration = penumbra::MediaTime::FromMicroseconds(
          config_.presentation->duration.InMicroseconds());
    } else {
      description.duration = penumbra::MediaTime::Unknown();
    }
    if (config_.presentation->type == manifest::PresentationType::kDynamic &&
        config_.presentation->live.has_value()) {
      if (config_.presentation->protocol == manifest::Protocol::kDash) {
        const std::function<absl::Time()> wall_clock =
            config_.wall_clock == nullptr
                ? std::function<absl::Time()>([] { return absl::Now(); })
                : config_.wall_clock;
        const auto window = manifest::DashAvailableWindow(
            *config_.presentation, wall_clock(), utc_offset_);
        if (window.ok()) {
          description.seekable_range = SeekableRange{
              .start = window->start, .end = window->available_end};
        }
      } else {
        description.seekable_range =
            SeekableRange{.start = config_.presentation->live->window_start,
                          .end = config_.presentation->live->window_end};
      }
    }
    opened_ = true;
    sink_->OnOpened(std::move(description));
  }

  Queue& QueueFor(uint32_t track_id) {
    return track_id == kAudioPipelineTrack ? audio_queue_ : video_queue_;
  }

  Queue& QueueFor(fetch::StreamKind kind) {
    return QueueFor(PipelineTrack(kind));
  }

  void Deliver(uint32_t track_id, Queue& queue) {
    if (sink_ == nullptr || !opened_ || queue.ticket == 0) {
      return;
    }
    const size_t count = std::min(queue.grant, queue.samples.size());
    if (count > 0) {
      std::vector<penumbra::EncodedSample> samples;
      samples.reserve(count);
      for (size_t index = 0; index < count; ++index) {
        samples.push_back(std::move(queue.samples.front()));
        queue.samples.pop_front();
      }
      queue.grant -= count;
      sink_->OnSamples(queue.ticket, track_id, std::move(samples));
    }
    if (queue.ended && queue.samples.empty() && !queue.eos_sent) {
      queue.eos_sent = true;
      sink_->OnEndOfStream(queue.ticket, track_id);
    }
  }

  static void ResetQueue(Queue& queue) {
    queue.samples.clear();
    queue.grant = 0;
    queue.ended = false;
    queue.eos_sent = false;
    queue.last_end = penumbra::MediaTime::Zero();
    queue.segment_end = penumbra::MediaTime::Unknown();
    queue.overlap_cutoff = penumbra::MediaTime::Unknown();
    queue.has_segment = false;
  }

  void Fail(const absl::Status& status) {
    if (closed_ || sink_ == nullptr) {
      return;
    }
    if (!opened_) {
      sink_->OnOpenFailed(ErrorFor(status));
    } else {
      sink_->OnSourceError(ErrorFor(status));
    }
  }

  AdaptiveSourceConfig config_;
  std::shared_ptr<FixedLoadPolicy> fixed_policy_;
  std::shared_ptr<fetch::LoadPolicy> policy_;
  std::shared_ptr<SampleSink> sink_;
  std::unique_ptr<fetch::SegmentFetcher> fetcher_;
  std::shared_ptr<PresentationRecovery> recovery_provider_;
  std::shared_ptr<fetch::RecoveryLadder> recovery_;
  std::unique_ptr<fetch::RecoveryTarget> recovery_target_;
  std::unique_ptr<fetch::SteeringClient> steering_client_;
  std::unique_ptr<manifest::DashMpdRefresher> dash_refresher_;
  absl::Duration utc_offset_ = absl::ZeroDuration();
  std::array<std::unique_ptr<demux::Demuxer>, fetch::kStreamKindCount>
      demuxers_;
  std::array<std::unique_ptr<OutputAdapter>, fetch::kStreamKindCount> outputs_;
  std::array<std::optional<penumbra::StreamInfo>, 3> stream_info_;
  std::array<std::map<uint32_t, penumbra::StreamType>, fetch::kStreamKindCount>
      demux_tracks_;
  std::array<std::map<uint32_t, penumbra::StreamInfo>, fetch::kStreamKindCount>
      demux_stream_info_;
  std::array<std::optional<manifest::TimelineMapping>, fetch::kStreamKindCount>
      timelines_;
  std::shared_ptr<const manifest::Track> video_track_;
  std::shared_ptr<const manifest::Track> automatic_video_track_;
  std::shared_ptr<const manifest::Track> audio_track_;
  std::unique_ptr<abr::BandwidthEstimator> bandwidth_estimator_;
  std::unique_ptr<abr::AbrPolicy> abr_policy_;
  bool manual_video_selection_ = false;
  std::int64_t last_video_switch_ms_ = -1;
  Queue video_queue_;
  Queue audio_queue_;
  penumbra::SeekTicket ticket_ = 1;
  uint64_t fetch_generation_ = 0;
  bool opened_ = false;
  bool closed_ = false;
};

absl::StatusOr<std::unique_ptr<AdaptiveSource>> AdaptiveSource::Create(
    AdaptiveSourceConfig config) {
  if (config.presentation == nullptr || config.transport == nullptr ||
      config.runner == nullptr || config.manifest_url.empty() ||
      !BufferManager::ValidateConfig(config.buffer)) {
    return absl::InvalidArgumentError(
        "adaptive source dependencies are missing");
  }
  if (config.settings.has_value()) {
    const settings::Snapshot& snapshot = *config.settings;
    config.recovery.failover_enabled =
        snapshot.Get(settings::kStreamingFailoverEnabled);
    config.recovery.content_steering_enabled =
        snapshot.Get(settings::kStreamingContentSteeringEnabled);
    config.recovery.backoff.max_attempts =
        static_cast<int>(snapshot.Get(settings::kNetworkRetryMaxAttempts));
    config.recovery.backoff.initial_backoff =
        snapshot.Get(settings::kNetworkRetryInitialBackoffMs);
    config.recovery.backoff.max_backoff =
        snapshot.Get(settings::kNetworkRetryMaxBackoffMs);
    config.recovery.rendition_exclusion =
        snapshot.Get(settings::kAbrRenditionExclusionMs);
    config.recovery.pathway_exclusion =
        snapshot.Get(settings::kNetworkPathwayExclusionMs);
  }
  auto impl = std::make_shared<Impl>(std::move(config));
  return std::unique_ptr<AdaptiveSource>(new AdaptiveSource(std::move(impl)));
}

AdaptiveSource::AdaptiveSource(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

AdaptiveSource::~AdaptiveSource() { Close(); }

void AdaptiveSource::Open(std::shared_ptr<SampleSink> sink) {
  if (impl_ == nullptr || sink == nullptr) {
    return;
  }
  const absl::Status status = impl_->Start(std::move(sink));
  if (!status.ok()) {
    impl_->Close();
  }
}

void AdaptiveSource::RequestSamples(penumbra::SeekTicket ticket,
                                    uint32_t track_id, size_t max_samples) {
  if (impl_ != nullptr) {
    impl_->Request(ticket, track_id, max_samples);
  }
}

bool AdaptiveSource::SelectAudioTrack(std::string_view id) {
  return impl_ != nullptr &&
         impl_
             ->SelectRendition(fetch::StreamKind::kAudio,
                               manifest::TrackId(std::string(id)))
             .ok();
}

bool AdaptiveSource::SelectVideoTrack(std::string_view id) {
  return impl_ != nullptr &&
         impl_
             ->SelectRendition(fetch::StreamKind::kVideo,
                               manifest::TrackId(std::string(id)))
             .ok();
}

bool AdaptiveSource::ClearVideoTrackSelection() {
  return impl_ != nullptr && impl_->ClearVideoTrackSelection().ok();
}

std::string AdaptiveSource::SelectedAudioTrackId() const {
  return impl_ == nullptr ? std::string{} : impl_->SelectedAudioTrackId();
}

std::string AdaptiveSource::SelectedVideoTrackId() const {
  return impl_ == nullptr ? std::string{} : impl_->SelectedVideoTrackId();
}

absl::Status AdaptiveSource::SelectRendition(fetch::StreamKind kind,
                                             const manifest::TrackId& track) {
  return impl_ == nullptr
             ? absl::FailedPreconditionError("adaptive source is closed")
             : impl_->SelectRendition(kind, track);
}

bool AdaptiveSource::OnPlaylistFailover(fetch::StreamKind kind) {
  return impl_ != nullptr && impl_->OnPlaylistFailover(kind);
}

absl::Status AdaptiveSource::UpdatePresentation(
    std::shared_ptr<const manifest::Presentation> presentation) {
  return impl_ == nullptr
             ? absl::FailedPreconditionError("adaptive source is closed")
             : impl_->UpdatePresentation(std::move(presentation));
}

SeekPoint AdaptiveSource::ResolveSeek(penumbra::MediaTime position,
                                      SeekMode mode, FastSeekSnap snap) const {
  return impl_ == nullptr
             ? SeekPoint{.presentation_time = position, .decode_time = position}
             : impl_->Resolve(position, mode, snap);
}

void AdaptiveSource::Seek(penumbra::SeekTicket ticket,
                          penumbra::MediaTime decode_position) {
  if (impl_ != nullptr) {
    impl_->DoSeek(ticket, decode_position);
  }
}

void AdaptiveSource::Close() {
  if (impl_ != nullptr) {
    impl_->Close();
    impl_.reset();
  }
}

}
