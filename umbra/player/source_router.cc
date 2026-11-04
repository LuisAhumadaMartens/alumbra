#include "umbra/player/source_router.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/hls_parser.h"
#include "umbra/manifest/presentation.h"
#include "umbra/pipeline/adaptive_source.h"
#include "umbra/pipeline/progressive_source.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/byte_stream_data_source.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_resolver.h"
#include "umbra/source/source_type.h"
#include "umbra/source/source_type_detector.h"

namespace umbra::player::internal {
namespace {

constexpr size_t kMaxManifestBytes = size_t{16} * 1024 * 1024;

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
  RoutedSource(source::ResolvedSource resolved,
               std::shared_ptr<base::SequencedTaskRunner> runner,
               std::shared_ptr<penumbra::HttpTransport> transport,
               std::shared_ptr<const base::UrlRedactor> redactor)
      : impl_(std::make_shared<Impl>(std::move(resolved), std::move(runner),
                                     std::move(transport),
                                     std::move(redactor))) {}

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
  class Impl final : public std::enable_shared_from_this<Impl> {
   public:
    Impl(source::ResolvedSource resolved,
         std::shared_ptr<base::SequencedTaskRunner> runner,
         std::shared_ptr<penumbra::HttpTransport> transport,
         std::shared_ptr<const base::UrlRedactor> redactor)
        : resolved_(std::move(resolved)),
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
      auto created = pipeline::ProgressiveSource::Create(
          data_source_, runner_, redactor_,
          {.extension = resolved_.url.PathExtension(),
           .content_type = data_source_->Info().content_type});
      if (!created.ok()) {
        Fail(created.status());
        return;
      }
      active_ = std::move(*created);
      active_->Open(sink_);
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
      config.presentation =
          std::make_shared<manifest::Presentation>(std::move(*presentation));
      config.manifest_url = resolved_.url.ToString();
      config.transport = transport_;
      config.runner = runner_;
      auto created = pipeline::AdaptiveSource::Create(std::move(config));
      if (!created.ok()) {
        Fail(created.status());
        return;
      }
      active_ = std::move(*created);
      active_->Open(sink_);
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
    source::SourceType manifest_type_ = source::SourceType::kHls;
    bool closed_ = false;
  };

  std::shared_ptr<Impl> impl_;
};

}

SourceResolverCallback CreateSourceResolver(
    std::shared_ptr<const source::SourceResolver> resolver,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<penumbra::HttpTransport> transport,
    std::shared_ptr<const base::UrlRedactor> redactor) {
  return [resolver = std::move(resolver), runner = std::move(runner),
          transport = std::move(transport), redactor = std::move(redactor)](
             std::string_view url) -> std::unique_ptr<pipeline::SampleSource> {
    auto result = resolver->Resolve(url);
    if (!result.ok()) {
      return nullptr;
    }
    return std::make_unique<RoutedSource>(std::move(*result), runner, transport,
                                          redactor);
  };
}

}
