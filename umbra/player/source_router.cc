#include "umbra/player/source_router.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/progressive_demuxer_registry.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/hls_parser.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/pipeline/adaptive_source.h"
#include "umbra/pipeline/progressive_source.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/text_track.h"
#include "umbra/player/text_track_routing.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/byte_stream_data_source.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_resolver.h"
#include "umbra/source/source_type.h"
#include "umbra/source/source_type_detector.h"
#include "umbra/text/cea_decoder.h"

namespace umbra::player::internal {
namespace {

constexpr size_t kMaxManifestBytes = size_t{16} * 1024 * 1024;

class TextSegmentOutput final : public demux::DemuxerOutput {
 public:
  void OnStreams(std::vector<penumbra::StreamInfo> value) override {
    streams = std::move(value);
  }
  void OnMetadataStream(const demux::MetadataStreamInfo& stream) override {
    static_cast<void>(stream);
  }
  void OnSeekMap(std::shared_ptr<const demux::SeekMap> seek_map) override {
    static_cast<void>(seek_map);
  }
  void OnSample(penumbra::EncodedSample sample) override {
    samples.push_back(std::move(sample));
  }
  void OnTimedMetadata(demux::TimedMetadataItem item) override {
    static_cast<void>(item);
  }
  void OnProducerReferenceTime(
      const demux::ProducerReferenceTime& reference) override {
    static_cast<void>(reference);
  }

  std::vector<penumbra::StreamInfo> streams;
  std::vector<penumbra::EncodedSample> samples;
};

base::ErrorDetail ErrorFor(const absl::Status& status) {
  base::ErrorCode code = base::ErrorCode::kSourceReadFailed;
  switch (status.code()) {
    case absl::StatusCode::kNotFound:
      code = base::ErrorCode::kSourceNotFound;
      break;
    case absl::StatusCode::kPermissionDenied:
      code = base::ErrorCode::kSourceAccessDenied;
      break;
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kFailedPrecondition:
      code = base::ErrorCode::kManifestInvalid;
      break;
    default:
      break;
  }
  const base::ErrorCatalogueEntry* entry = base::FindError(code);
  assert(entry != nullptr);
  return {code,
          entry->category,
          entry->severity,
          entry->retryable,
          {{"status", std::string(absl::StatusCodeToString(status.code()))}},
          nullptr};
}

class RoutedSource final : public pipeline::SampleSource {
 public:
  RoutedSource(source::ResolvedSource resolved, TextTrackList* text_tracks,
               TextTrackRoutingOptions text_options,
               TextTrackUrlLoader text_track_loader,
               std::shared_ptr<base::SequencedTaskRunner> runner,
               std::shared_ptr<penumbra::HttpTransport> transport,
               std::shared_ptr<const base::UrlRedactor> redactor)
      : impl_(std::make_shared<Impl>(
            std::move(resolved), text_tracks, std::move(text_options),
            std::move(text_track_loader), std::move(runner),
            std::move(transport), std::move(redactor))) {}

  ~RoutedSource() override { Close(); }
  RoutedSource(const RoutedSource&) = delete;
  RoutedSource& operator=(const RoutedSource&) = delete;
  void Open(std::shared_ptr<pipeline::SampleSink> sink) override {
    impl_->Open(std::move(sink));
  }
  void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                      size_t max_samples) override {
    impl_->RequestSamples(ticket, track_id, max_samples);
  }
  [[nodiscard]] pipeline::SeekPoint ResolveSeek(
      penumbra::MediaTime position, pipeline::SeekMode mode,
      pipeline::FastSeekSnap snap) const override {
    return impl_->ResolveSeek(position, mode, snap);
  }
  void Seek(penumbra::SeekTicket ticket,
            penumbra::MediaTime decode_position) override {
    impl_->Seek(ticket, decode_position);
  }
  void Close() override { impl_->Close(); }

 private:
  class Impl final : public pipeline::ProgressiveTextObserver,
                     public std::enable_shared_from_this<Impl> {
   public:
    Impl(source::ResolvedSource resolved, TextTrackList* text_tracks,
         TextTrackRoutingOptions text_options,
         TextTrackUrlLoader text_track_loader,
         std::shared_ptr<base::SequencedTaskRunner> runner,
         std::shared_ptr<penumbra::HttpTransport> transport,
         std::shared_ptr<const base::UrlRedactor> redactor)
        : resolved_(std::move(resolved)),
          text_tracks_(text_tracks),
          text_options_(std::move(text_options)),
          text_track_loader_(std::move(text_track_loader)),
          runner_(std::move(runner)),
          transport_(std::move(transport)),
          redactor_(std::move(redactor)) {}

    void Open(std::shared_ptr<pipeline::SampleSink> sink) {
      sink_ = std::move(sink);
      if (closed_ || resolved_.kind != source::ProviderKind::kByteStream ||
          resolved_.byte_stream == nullptr) {
        Fail(absl::UnimplementedError("source provider is unsupported"));
        return;
      }
      penumbra::ByteStreamRequest request;
      request.url = resolved_.url.ToString();
      if (resolved_.credential.has_value()) {
        request.credential = resolved_.credential->credential;
      }
      const std::weak_ptr<Impl> weak = shared_from_this();
      open_request_ = source::ByteStreamDataSource::Open(
          resolved_.byte_stream, std::move(request), runner_, redactor_, {},
          [weak](absl::StatusOr<std::shared_ptr<source::DataSource>> result) {
            if (const auto self = weak.lock()) {
              self->OnDataSource(std::move(result));
            }
          });
    }

    void OnDataSource(
        absl::StatusOr<std::shared_ptr<source::DataSource>> result) {
      open_request_.reset();
      if (closed_) {
        if (result.ok()) {
          (*result)->Close();
        }
        return;
      }
      if (!result.ok()) {
        Fail(result.status());
        return;
      }
      data_source_ = std::move(*result);
      source::SniffHints hints;
      hints.extension = resolved_.url.PathExtension();
      hints.content_type = data_source_->Info().content_type;
      auto created = source::BufferedReader::Create(
          data_source_, runner_,
          {.read_ahead_bytes = kMaxManifestBytes,
           .chunk_bytes = source::kDefaultChunkBytes});
      if (!created.ok()) {
        Fail(created.status());
        return;
      }
      reader_ = std::shared_ptr<source::BufferedReader>(std::move(*created));
      detector_ = std::make_unique<source::SourceTypeDetector>(reader_.get(),
                                                               redactor_);
      const std::weak_ptr<Impl> weak = shared_from_this();
      detector_->Start(std::move(hints),
                       [weak](absl::StatusOr<source::DetectedType> type) {
                         if (const auto self = weak.lock()) {
                           self->OnDetected(std::move(type));
                         }
                       });
    }

    void OnDetected(absl::StatusOr<source::DetectedType> detected) {
      if (closed_) {
        return;
      }
      if (!detected.ok()) {
        Fail(detected.status());
        return;
      }
      if (detected->type == source::SourceType::kHls ||
          detected->type == source::SourceType::kDash) {
        ReadManifest(detected->type);
        return;
      }
      DiscoverSiblings();
      auto created = pipeline::ProgressiveSource::Create(
          data_source_, runner_, redactor_,
          {.extension = resolved_.url.PathExtension(),
           .content_type = data_source_->Info().content_type},
          {}, shared_from_this());
      if (!created.ok()) {
        Fail(created.status());
        return;
      }
      active_ = std::move(*created);
      active_->Open(sink_);
    }

    void OnTextStreams(std::vector<penumbra::StreamInfo> streams) override {
      if (text_tracks_ == nullptr) {
        for (const penumbra::StreamInfo& stream : streams) {
          if (stream.type() != penumbra::StreamType::kText) {
            source_languages_[stream.track_id] = stream.language;
          }
        }
        return;
      }
      for (const penumbra::StreamInfo& stream : streams) {
        if (stream.type() != penumbra::StreamType::kText) {
          source_languages_[stream.track_id] = stream.language;
          continue;
        }
        std::string label =
            stream.codec_string.empty() ? "Subtitles" : stream.codec_string;
        auto track = std::make_shared<TextTrack>(
            TextTrackKind::kSubtitles, std::move(label), stream.language,
            text_options_.cue_memory_budget_bytes);
        inband_tracks_[stream.track_id] = std::move(track);
        text_tracks_->Add(inband_tracks_.at(stream.track_id));
      }
    }

    void OnTextSample(penumbra::StreamInfo stream,
                      penumbra::EncodedSample sample) override {
      const auto found = inband_tracks_.find(stream.track_id);
      if (found == inband_tracks_.end()) {
        return;
      }
      static_cast<void>(LoadTextTrackSample(*found->second, stream, sample));
    }

    void OnMetadataStream(const demux::MetadataStreamInfo& stream) override {
      if (stream.kind != demux::MetadataKind::kCaptions ||
          text_tracks_ == nullptr) {
        return;
      }
      caption_streams_[stream.stream_id] = stream.source_track_id;
      if (!caption_decoders_.contains(stream.source_track_id)) {
        caption_decoders_.emplace(stream.source_track_id,
                                  std::make_unique<text::CeaDecoder>());
      }
    }

    void OnTimedMetadata(demux::TimedMetadataItem item) override {
      const auto source_track = caption_streams_.find(item.stream_id);
      if (source_track == caption_streams_.end() || text_tracks_ == nullptr) {
        return;
      }
      const auto decoder = caption_decoders_.find(source_track->second);
      if (decoder == caption_decoders_.end()) {
        return;
      }
      const std::vector<text::CaptionSnapshot> snapshots =
          decoder->second->Push(item.payload.span(),
                                TimestampSeconds(item.time));
      for (const text::CaptionSnapshot& snapshot : snapshots) {
        auto track = caption_tracks_.find(std::make_tuple(
            source_track->second, snapshot.digital, snapshot.service));
        if (track == caption_tracks_.end()) {
          const auto language = source_languages_.find(source_track->second);
          const std::string language_value =
              language == source_languages_.end() ? "" : language->second;
          const std::string label =
              snapshot.digital ? "SERVICE" + std::to_string(snapshot.service)
                               : "CC" + std::to_string(snapshot.service);
          std::shared_ptr<TextTrack> created;
          const auto service_track = caption_service_tracks_.find(
              std::make_pair(snapshot.digital, snapshot.service));
          if (service_track != caption_service_tracks_.end()) {
            created = service_track->second.lock();
          }
          if (created == nullptr) {
            created = std::make_shared<TextTrack>(
                TextTrackKind::kCaptions, label, language_value,
                text_options_.cue_memory_budget_bytes);
            text_tracks_->Add(created);
          }
          track =
              caption_tracks_
                  .emplace(std::make_tuple(source_track->second,
                                           snapshot.digital, snapshot.service),
                           std::move(created))
                  .first;
        }
        TextCue cue;
        cue.start_time = snapshot.time;
        cue.end_time = item.duration.value == 0
                           ? snapshot.time + 2.0
                           : snapshot.time + TimestampSeconds(item.duration);
        cue.line = snapshot.row;
        cue.position = snapshot.column;
        StyledTextRun run;
        run.text = snapshot.text;
        cue.runs.push_back(std::move(run));
        track->second->AddCue(std::move(cue));
      }
    }

    static double TimestampSeconds(const penumbra::Timestamp& timestamp) {
      const auto micros = penumbra::Rescale(
          timestamp.value, timestamp.time_base, penumbra::kMicrosecondTimeBase,
          penumbra::Rounding::kNearest);
      return micros.has_value() ? static_cast<double>(*micros) / 1000000.0
                                : 0.0;
    }

    static std::optional<uint8_t> CaptionService(std::string_view identifier,
                                                 std::string_view prefix) {
      if (!identifier.starts_with(prefix)) {
        return std::nullopt;
      }
      unsigned int service = 0;
      const std::string_view suffix = identifier.substr(prefix.size());
      if (suffix.empty()) {
        return std::nullopt;
      }
      for (const char digit : suffix) {
        if (digit < '0' || digit > '9') {
          return std::nullopt;
        }
        service = (service * 10) + static_cast<unsigned int>(digit - '0');
        if (service > 63) {
          return std::nullopt;
        }
      }
      if (service == 0) {
        return std::nullopt;
      }
      return static_cast<uint8_t>(service);
    }

    void DiscoverSiblings() {
      if (!text_options_.auto_discover_siblings || text_tracks_ == nullptr ||
          text_track_loader_ == nullptr || resolved_.byte_stream == nullptr ||
          !resolved_.url.scheme.has_value() ||
          *resolved_.url.scheme != "file") {
        return;
      }
      const std::string& path = resolved_.url.path;
      const size_t slash = path.find_last_of('/');
      const std::string filename =
          path.substr(slash == std::string::npos ? 0 : slash + 1);
      const size_t dot = filename.find_last_of('.');
      auto stem = std::make_shared<std::string>(filename.substr(0, dot));
      const std::string source_url = resolved_.url.ToString();
      const std::weak_ptr<Impl> weak = shared_from_this();
      resolved_.byte_stream->ListSiblingFileUrls(
          source_url, [weak, source_url, stem](auto siblings) {
            const auto self = weak.lock();
            if (self == nullptr || self->closed_ || !siblings.ok()) {
              return;
            }
            auto urls = std::make_shared<std::vector<std::string>>(
                std::move(*siblings));
            static_cast<void>(
                self->runner_->PostTask([weak, source_url, stem, urls] {
                  if (const auto owner = weak.lock();
                      owner != nullptr && !owner->closed_) {
                    owner->AddSiblingTracks(source_url, *stem, *urls);
                  }
                }));
          });
    }

    void AddSiblingTracks(const std::string& source_url,
                          const std::string& stem,
                          const std::vector<std::string>& siblings) {
      for (const std::string& sibling : siblings) {
        if (sibling == source_url) {
          continue;
        }
        const std::string filename = FilenameFromUrl(sibling);
        const std::optional<std::string> language =
            LanguageForSibling(filename, stem);
        if (!language.has_value()) {
          continue;
        }
        AddSiblingTrack(sibling, *language);
      }
    }

    static std::string FilenameFromUrl(std::string_view url) {
      const size_t slash = url.find_last_of('/');
      return std::string(
          url.substr(slash == std::string_view::npos ? 0 : slash + 1));
    }

    static std::optional<std::string> LanguageForSibling(
        const std::string& filename, const std::string& stem) {
      const size_t extension_dot = filename.find_last_of('.');
      if (extension_dot == std::string::npos) {
        return std::nullopt;
      }
      const std::string extension = filename.substr(extension_dot + 1);
      const std::string name = filename.substr(0, extension_dot);
      if (extension == "srt" && name == stem) {
        return std::string();
      }
      if (extension == "vtt" && name.starts_with(stem + ".")) {
        std::string language = name.substr(stem.size() + 1);
        if (!language.empty()) {
          return language;
        }
      }
      return std::nullopt;
    }

    void AddSiblingTrack(const std::string& sibling,
                         const std::string& language) {
      auto track = std::make_shared<TextTrack>(
          TextTrackKind::kSubtitles, language.empty() ? "Subtitles" : language,
          language, text_options_.cue_memory_budget_bytes);
      text_tracks_->Add(track);
      const std::weak_ptr<Impl> weak = shared_from_this();
      text_track_loader_(
          sibling, text_options_.max_payload_bytes,
          [weak, track, fallback = text_options_.fallback_encoding,
           auto_detect = text_options_.auto_detect_encoding](auto payload) {
            if (const auto self = weak.lock();
                self != nullptr && !self->closed_ && payload.ok()) {
              static_cast<void>(LoadTextTrackPayload(*track, *payload, fallback,
                                                     auto_detect));
            }
          });
    }

    void ReadManifest(source::SourceType type) {
      manifest_type_ = type;
      const size_t size =
          data_source_->Info().size.has_value()
              ? static_cast<size_t>(std::min<uint64_t>(
                    *data_source_->Info().size, kMaxManifestBytes))
              : kMaxManifestBytes;
      const std::weak_ptr<Impl> weak = shared_from_this();
      read_request_ = reader_->Read(
          0, size, [weak](absl::StatusOr<penumbra::SharedBytes> result) {
            if (const auto self = weak.lock()) {
              self->read_request_.reset();
              self->OnManifest(std::move(result));
            }
          });
    }

    void OnManifest(absl::StatusOr<penumbra::SharedBytes> result) {
      if (closed_) {
        return;
      }
      if (!result.ok()) {
        Fail(result.status());
        return;
      }
      const std::string_view text(reinterpret_cast<const char*>(result->data()),
                                  result->size());
      absl::StatusOr<manifest::Presentation> presentation =
          manifest_type_ == source::SourceType::kHls
              ? manifest::ParseHlsPlaylist(
                    text, {.base_uri = resolved_.url.ToString(),
                           .query_parameters = {},
                           .imported_variables = {},
                           .strict = false})
              : manifest::ParseDashMpd(text);
      if (!presentation.ok()) {
        Fail(presentation.status());
        return;
      }
      pipeline::AdaptiveSourceConfig config;
      AddManifestTextTracks(*presentation);
      config.presentation =
          std::make_shared<manifest::Presentation>(std::move(*presentation));
      config.manifest_url = resolved_.url.ToString();
      config.transport = transport_;
      config.runner = runner_;
      config.text_observer = shared_from_this();
      auto created = pipeline::AdaptiveSource::Create(std::move(config));
      if (!created.ok()) {
        Fail(created.status());
        return;
      }
      active_ = std::move(*created);
      active_->Open(sink_);
    }

    void AddManifestTextTracks(const manifest::Presentation& presentation) {
      if (text_tracks_ == nullptr) {
        return;
      }
      if (presentation.hls.has_value()) {
        AddHlsTextTracks(*presentation.hls);
        return;
      }
      for (const auto& period : presentation.periods) {
        for (const auto& switching_set : period->switching_sets) {
          AddDashTextTracks(*switching_set);
        }
      }
    }

    void AddHlsTextTracks(
        const manifest::HlsPresentationExtension& hls_presentation) {
      for (const manifest::HlsRendition& rendition :
           hls_presentation.renditions) {
        if (rendition.type == manifest::RenditionType::kSubtitles ||
            rendition.type == manifest::RenditionType::kClosedCaptions) {
          AddHlsTextTrack(rendition);
        }
      }
    }

    void AddHlsTextTrack(const manifest::HlsRendition& rendition) {
      const TextTrackMetadata metadata = MetadataForHlsTextRendition(rendition);
      auto track = std::make_shared<TextTrack>(
          metadata.kind, metadata.label, metadata.language,
          text_options_.cue_memory_budget_bytes);
      text_tracks_->Add(track);
      RegisterHlsCaptionService(rendition, track);
      if (text_track_loader_ != nullptr &&
          rendition.type == manifest::RenditionType::kSubtitles &&
          rendition.uri.has_value()) {
        LoadHlsTextRendition(*rendition.uri, track);
      }
    }

    void RegisterHlsCaptionService(const manifest::HlsRendition& rendition,
                                   const std::shared_ptr<TextTrack>& track) {
      if (rendition.type != manifest::RenditionType::kClosedCaptions ||
          !rendition.in_stream_id.has_value()) {
        return;
      }
      if (const auto service = CaptionService(*rendition.in_stream_id, "CC")) {
        caption_service_tracks_[{false, *service}] = track;
      } else if (const auto service =
                     CaptionService(*rendition.in_stream_id, "SERVICE")) {
        caption_service_tracks_[{true, *service}] = track;
      }
    }

    void AddDashTextTracks(const manifest::SwitchingSet& switching_set) {
      if (switching_set.type == penumbra::StreamType::kText) {
        AddDashTextRendition(switching_set);
        return;
      }
      for (const manifest::DashDescriptor& descriptor :
           switching_set.accessibility) {
        AddDashCaptionTrack(descriptor, switching_set);
      }
    }

    void AddDashCaptionTrack(const manifest::DashDescriptor& descriptor,
                             const manifest::SwitchingSet& switching_set) {
      const bool cea608 =
          descriptor.scheme_id_uri == "urn:scte:dash:cc:cea-608:2015";
      const bool cea708 =
          descriptor.scheme_id_uri == "urn:scte:dash:cc:cea-708:2015";
      if (!cea608 && !cea708) {
        return;
      }
      const std::string_view prefix = cea608 ? "CC" : "SERVICE";
      const auto service = CaptionService(descriptor.value, prefix);
      if (!service.has_value()) {
        return;
      }
      auto track = std::make_shared<TextTrack>(
          TextTrackKind::kCaptions, descriptor.value, switching_set.language,
          text_options_.cue_memory_budget_bytes);
      text_tracks_->Add(track);
      caption_service_tracks_[{cea708, *service}] = track;
    }

    void AddDashTextRendition(const manifest::SwitchingSet& switching_set) {
      const TextTrackMetadata metadata =
          MetadataForManifestTextTrack(switching_set);
      auto track = std::make_shared<TextTrack>(
          metadata.kind, metadata.label, metadata.language,
          text_options_.cue_memory_budget_bytes);
      text_tracks_->Add(track);
      RegisterDashCaptionServices(switching_set, track);
      LoadDashTextSegments(switching_set, track);
    }

    void RegisterDashCaptionServices(
        const manifest::SwitchingSet& switching_set,
        const std::shared_ptr<TextTrack>& track) {
      for (const manifest::DashDescriptor& descriptor :
           switching_set.accessibility) {
        RegisterDashCaptionService(descriptor, track);
      }
    }

    void RegisterDashCaptionService(const manifest::DashDescriptor& descriptor,
                                    const std::shared_ptr<TextTrack>& track) {
      if (descriptor.scheme_id_uri == "urn:scte:dash:cc:cea-608:2015") {
        if (const auto service = CaptionService(descriptor.value, "CC")) {
          caption_service_tracks_[{false, *service}] = track;
        }
      } else if (descriptor.scheme_id_uri == "urn:scte:dash:cc:cea-708:2015") {
        if (const auto service = CaptionService(descriptor.value, "SERVICE")) {
          caption_service_tracks_[{true, *service}] = track;
        }
      }
    }

    void LoadDashTextSegments(const manifest::SwitchingSet& switching_set,
                              const std::shared_ptr<TextTrack>& track) {
      if (switching_set.tracks.empty() || text_track_loader_ == nullptr) {
        return;
      }
      const std::shared_ptr<const manifest::Track>& text_track =
          switching_set.tracks.front();
      const manifest::SegmentIndex* index = text_track->segments.Index();
      if (index == nullptr) {
        return;
      }
      std::string codec;
      if (const auto* extension =
              std::get_if<manifest::DashTrackExtension>(&text_track->extension);
          extension != nullptr && extension->text_codec.has_value()) {
        codec = *extension->text_codec;
      }
      LoadTextSegments(*index, track, codec);
    }

    void LoadHlsTextRendition(std::string url,
                              const std::shared_ptr<TextTrack>& track) {
      const std::weak_ptr<Impl> weak = shared_from_this();
      const std::string playlist_url = url;
      text_track_loader_(
          std::move(url), text_options_.max_payload_bytes,
          [weak, track, playlist_url](absl::StatusOr<std::string> payload) {
            const auto self = weak.lock();
            if (self == nullptr || self->closed_ || !payload.ok()) {
              return;
            }
            auto playlist =
                manifest::ParseHlsPlaylist(*payload, {.base_uri = playlist_url,
                                                      .query_parameters = {},
                                                      .imported_variables = {},
                                                      .strict = false});
            if (!playlist.ok()) {
              return;
            }
            for (const auto& period : playlist->periods) {
              for (const auto& switching_set : period->switching_sets) {
                for (const auto& rendition_track : switching_set->tracks) {
                  const manifest::SegmentIndex* index =
                      rendition_track->segments.Index();
                  if (index != nullptr) {
                    self->LoadTextSegments(*index, track);
                  }
                }
              }
            }
          });
    }

    void LoadTextSegments(const manifest::SegmentIndex& index,
                          const std::shared_ptr<TextTrack>& track,
                          const std::string& codec = {}) {
      const size_t count = std::min(index.size(), size_t{4096});
      std::vector<manifest::Segment> segments;
      segments.reserve(count);
      for (size_t position = 0; position < count; ++position) {
        const manifest::Segment segment = index.At(position);
        if (!segment.uri.empty() && !segment.gap) {
          segments.push_back(segment);
        }
      }
      const bool mp4_text = codec == "wvtt" || codec == "stpp";
      const std::vector<manifest::InitSection>& init_sections =
          index.InitSections();
      if (mp4_text && !init_sections.empty()) {
        const std::weak_ptr<Impl> weak = shared_from_this();
        text_track_loader_(
            init_sections.front().uri, text_options_.max_payload_bytes,
            [weak, track, codec, segments = std::move(segments)](
                absl::StatusOr<std::string> init_data) {
              const auto self = weak.lock();
              if (self == nullptr || self->closed_ || !init_data.ok()) {
                return;
              }
              for (const manifest::Segment& segment : segments) {
                self->LoadTextSegment(segment, track, codec, *init_data);
              }
            });
        return;
      }
      for (const manifest::Segment& segment : segments) {
        LoadTextSegment(segment, track, codec, {});
      }
    }

    void LoadTextSegment(const manifest::Segment& segment,
                         const std::shared_ptr<TextTrack>& track,
                         const std::string& codec,
                         const std::string& init_data) {
      const double start =
          static_cast<double>(segment.start.InMicroseconds()) / 1000000.0;
      const std::weak_ptr<Impl> weak = shared_from_this();
      text_track_loader_(
          segment.uri, text_options_.max_payload_bytes,
          [weak, track, codec, init_data,
           start](absl::StatusOr<std::string> payload) {
            const auto self = weak.lock();
            if (self == nullptr || self->closed_ || !payload.ok()) {
              return;
            }
            if ((codec == "wvtt" || codec == "stpp") && !init_data.empty()) {
              Impl::LoadMp4TextSegment(*payload, init_data, track, start);
              return;
            }
            static_cast<void>(LoadTextTrackPayload(
                *track, *payload, self->text_options_.fallback_encoding,
                self->text_options_.auto_detect_encoding, start));
          });
    }

    static void LoadMp4TextSegment(std::string_view media_data,
                                   std::string_view init_data,
                                   const std::shared_ptr<TextTrack>& track,
                                   double segment_start) {
      std::vector<uint8_t> bytes;
      bytes.reserve(init_data.size() + media_data.size());
      bytes.insert(bytes.end(), init_data.begin(), init_data.end());
      bytes.insert(bytes.end(), media_data.begin(), media_data.end());
      TextSegmentOutput output;
      auto created =
          demux::CreateProgressiveDemuxer(source::SourceType::kMp4, output);
      if (!created.ok()) {
        return;
      }
      std::unique_ptr<demux::Demuxer> demuxer = std::move(*created);
      demuxer->SetSourceInfo(bytes.size(), true);
      if (!demuxer->Append(0, penumbra::SharedBytes::Copy(bytes)).ok()) {
        return;
      }
      demuxer->EndOfInput();
      for (size_t step_count = 0; step_count < 10000; ++step_count) {
        auto step = demuxer->Step();
        if (!step.ok()) {
          return;
        }
        if (step->kind == demux::StepKind::kEndOfStream) {
          break;
        }
        if (step->kind == demux::StepKind::kNeedMoreData) {
          return;
        }
      }
      for (const penumbra::EncodedSample& sample : output.samples) {
        const auto stream = std::ranges::find_if(
            output.streams, [&sample](const penumbra::StreamInfo& candidate) {
              return candidate.track_id == sample.track_id &&
                     candidate.type() == penumbra::StreamType::kText;
            });
        if (stream != output.streams.end()) {
          static_cast<void>(
              LoadTextTrackSample(*track, *stream, sample, segment_start));
        }
      }
    }

    void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                        size_t max_samples) const {
      if (active_ != nullptr) {
        active_->RequestSamples(ticket, track_id, max_samples);
      }
    }

    pipeline::SeekPoint ResolveSeek(penumbra::MediaTime position,
                                    pipeline::SeekMode mode,
                                    pipeline::FastSeekSnap snap) const {
      if (active_ != nullptr) {
        return active_->ResolveSeek(position, mode, snap);
      }
      return {.presentation_time = position, .decode_time = position};
    }

    void Seek(penumbra::SeekTicket ticket,
              penumbra::MediaTime decode_position) const {
      if (active_ != nullptr) {
        active_->Seek(ticket, decode_position);
      }
    }

    void Fail(const absl::Status& status) {
      if (!closed_ && sink_ != nullptr) {
        sink_->OnOpenFailed(ErrorFor(status));
      }
      Close();
    }

    void Close() {
      if (closed_) {
        return;
      }
      closed_ = true;
      if (open_request_ != nullptr) {
        open_request_->Cancel();
        open_request_.reset();
      }
      if (detector_ != nullptr) {
        detector_->Cancel();
        detector_.reset();
      }
      if (read_request_ != nullptr) {
        read_request_->Cancel();
        read_request_.reset();
      }
      if (active_ != nullptr) {
        active_->Close();
        active_.reset();
      }
      if (reader_ != nullptr) {
        reader_->Close();
        reader_.reset();
      }
      if (data_source_ != nullptr) {
        data_source_->Close();
        data_source_.reset();
      }
      sink_.reset();
    }

    source::ResolvedSource resolved_;
    TextTrackList* text_tracks_;
    TextTrackRoutingOptions text_options_;
    TextTrackUrlLoader text_track_loader_;
    std::shared_ptr<base::SequencedTaskRunner> runner_;
    std::shared_ptr<penumbra::HttpTransport> transport_;
    std::shared_ptr<const base::UrlRedactor> redactor_;
    std::shared_ptr<pipeline::SampleSink> sink_;
    std::shared_ptr<source::OpenRequest> open_request_;
    std::shared_ptr<source::ReadRequest> read_request_;
    std::shared_ptr<source::DataSource> data_source_;
    std::shared_ptr<source::BufferedReader> reader_;
    std::unique_ptr<source::SourceTypeDetector> detector_;
    std::unique_ptr<pipeline::SampleSource> active_;
    std::map<uint32_t, std::shared_ptr<TextTrack>> inband_tracks_;
    std::map<uint32_t, std::string> source_languages_;
    std::map<uint32_t, uint32_t> caption_streams_;
    std::map<uint32_t, std::unique_ptr<text::CeaDecoder>> caption_decoders_;
    std::map<std::tuple<uint32_t, bool, uint8_t>, std::shared_ptr<TextTrack>>
        caption_tracks_;
    std::map<std::pair<bool, uint8_t>, std::weak_ptr<TextTrack>>
        caption_service_tracks_;
    source::SourceType manifest_type_ = source::SourceType::kHls;
    bool closed_ = false;
  };

  std::shared_ptr<Impl> impl_;
};

class TextPayloadRequest final
    : public std::enable_shared_from_this<TextPayloadRequest> {
 public:
  TextPayloadRequest(source::ResolvedSource resolved,
                     std::shared_ptr<base::SequencedTaskRunner> runner,
                     std::shared_ptr<const base::UrlRedactor> redactor,
                     size_t max_bytes, TextPayloadCallback callback)
      : resolved_(std::move(resolved)),
        runner_(std::move(runner)),
        redactor_(std::move(redactor)),
        max_bytes_(max_bytes),
        callback_(std::move(callback)) {}

  void Start() {
    if (resolved_.kind != source::ProviderKind::kByteStream ||
        resolved_.byte_stream == nullptr) {
      Finish(absl::UnimplementedError("text source is not a byte stream"));
      return;
    }
    penumbra::ByteStreamRequest request;
    request.url = resolved_.url.ToString();
    if (resolved_.credential.has_value()) {
      request.credential = resolved_.credential->credential;
    }
    const std::shared_ptr<TextPayloadRequest> self = shared_from_this();
    open_request_ = source::ByteStreamDataSource::Open(
        resolved_.byte_stream, std::move(request), runner_, redactor_, {},
        [self](absl::StatusOr<std::shared_ptr<source::DataSource>> result) {
          self->OnOpened(std::move(result));
        });
  }

 private:
  void OnOpened(absl::StatusOr<std::shared_ptr<source::DataSource>> result) {
    open_request_.reset();
    if (!result.ok()) {
      Finish(result.status());
      return;
    }
    data_source_ = std::move(*result);
    if (data_source_->Info().size.has_value() &&
        *data_source_->Info().size > max_bytes_) {
      Finish(
          absl::ResourceExhaustedError("text payload exceeds its byte limit"));
      return;
    }
    const std::shared_ptr<TextPayloadRequest> self = shared_from_this();
    read_request_ = data_source_->Read(
        0, max_bytes_ + 1, [self](absl::StatusOr<penumbra::SharedBytes> bytes) {
          self->OnRead(std::move(bytes));
        });
  }

  void OnRead(absl::StatusOr<penumbra::SharedBytes> bytes) {
    read_request_.reset();
    if (!bytes.ok()) {
      Finish(bytes.status());
      return;
    }
    if (bytes->size() > max_bytes_) {
      Finish(
          absl::ResourceExhaustedError("text payload exceeds its byte limit"));
      return;
    }
    Finish(std::string(reinterpret_cast<const char*>(bytes->data()),
                       bytes->size()));
  }

  void Finish(absl::StatusOr<std::string> result) {
    if (finished_) {
      return;
    }
    finished_ = true;
    if (open_request_ != nullptr) {
      open_request_->Cancel();
      open_request_.reset();
    }
    if (read_request_ != nullptr) {
      read_request_->Cancel();
      read_request_.reset();
    }
    if (data_source_ != nullptr) {
      data_source_->Close();
      data_source_.reset();
    }
    if (callback_) {
      callback_(std::move(result));
      callback_ = {};
    }
  }

  source::ResolvedSource resolved_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  size_t max_bytes_;
  TextPayloadCallback callback_;
  std::shared_ptr<source::OpenRequest> open_request_;
  std::shared_ptr<source::ReadRequest> read_request_;
  std::shared_ptr<source::DataSource> data_source_;
  bool finished_ = false;
};

}

SourceResolverCallback CreateSourceResolver(
    std::shared_ptr<const source::SourceResolver> resolver,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<penumbra::HttpTransport> transport,
    std::shared_ptr<const base::UrlRedactor> redactor,
    TextTrackUrlLoader text_track_loader) {
  return [resolver = std::move(resolver), runner = std::move(runner),
          transport = std::move(transport), redactor = std::move(redactor),
          text_track_loader = std::move(text_track_loader)](
             std::string_view url, TextTrackList* text_tracks,
             TextTrackRoutingOptions text_options)
             -> std::unique_ptr<pipeline::SampleSource> {
    auto result = resolver->Resolve(url);
    if (!result.ok()) {
      return nullptr;
    }
    return std::make_unique<RoutedSource>(
        std::move(*result), text_tracks, std::move(text_options),
        text_track_loader, runner, transport, redactor);
  };
}

TextTrackUrlLoader CreateTextTrackUrlLoader(
    std::shared_ptr<const source::SourceResolver> resolver,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<const base::UrlRedactor> redactor) {
  return [resolver = std::move(resolver), runner = std::move(runner),
          redactor = std::move(redactor)](const std::string& url,
                                          size_t max_bytes,
                                          TextPayloadCallback callback) {
    auto resolved = resolver->Resolve(url);
    if (!resolved.ok()) {
      callback(resolved.status());
      return;
    }
    auto request = std::make_shared<TextPayloadRequest>(
        std::move(*resolved), runner, redactor, max_bytes, std::move(callback));
    request->Start();
  };
}

}
