#include "umbra/pipeline/progressive_source.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/progressive_demuxer_registry.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"
#include "umbra/source/source_type.h"
#include "umbra/source/source_type_detector.h"

namespace umbra::pipeline {
namespace {

base::ErrorDetail ErrorFor(const absl::Status& status) {
  const base::ErrorCode code =
      status.code() == absl::StatusCode::kInvalidArgument ||
              status.code() == absl::StatusCode::kFailedPrecondition
          ? base::ErrorCode::kMediaCorrupt
          : base::ErrorCode::kSourceReadFailed;
  const base::ErrorCatalogueEntry* entry = base::FindError(code);
  assert(entry != nullptr);
  return {code,
          entry->category,
          entry->severity,
          entry->retryable,
          {{"status", std::string(absl::StatusCodeToString(status.code()))}},
          nullptr};
}

class StreamOutput final : public demux::DemuxerOutput {
 public:
  explicit StreamOutput(
      std::function<void(std::vector<penumbra::StreamInfo>)> on_streams,
      std::function<void(std::shared_ptr<const demux::SeekMap>)> on_seek_map,
      std::function<void(penumbra::EncodedSample)> on_sample)
      : on_streams_(std::move(on_streams)),
        on_seek_map_(std::move(on_seek_map)),
        on_sample_(std::move(on_sample)) {}

  void OnStreams(std::vector<penumbra::StreamInfo> streams) override {
    on_streams_(std::move(streams));
  }
  void OnMetadataStream(const demux::MetadataStreamInfo& stream) override {
    static_cast<void>(stream);
  }
  void OnSeekMap(std::shared_ptr<const demux::SeekMap> seek_map) override {
    on_seek_map_(std::move(seek_map));
  }
  void OnSample(penumbra::EncodedSample sample) override {
    on_sample_(std::move(sample));
  }
  void OnTimedMetadata(demux::TimedMetadataItem item) override {
    static_cast<void>(item);
  }
  void OnProducerReferenceTime(
      const demux::ProducerReferenceTime& reference) override {
    static_cast<void>(reference);
  }

 private:
  std::function<void(std::vector<penumbra::StreamInfo>)> on_streams_;
  std::function<void(std::shared_ptr<const demux::SeekMap>)> on_seek_map_;
  std::function<void(penumbra::EncodedSample)> on_sample_;
};

}

class ProgressiveSource::Impl final
    : public std::enable_shared_from_this<ProgressiveSource::Impl> {
 public:
  Impl(std::shared_ptr<source::DataSource> data_source,
       std::shared_ptr<source::BufferedReader> reader,
       std::shared_ptr<base::SequencedTaskRunner> runner,
       std::shared_ptr<const base::UrlRedactor> redactor,
       source::SniffHints hints)
      : data_source_(std::move(data_source)),
        reader_(std::move(reader)),
        runner_(std::move(runner)),
        redactor_(std::move(redactor)),
        detector_(std::make_unique<source::SourceTypeDetector>(reader_.get(),
                                                               redactor_)),
        hints_(std::move(hints)) {}

  void Open(std::shared_ptr<SampleSink> sink) {
    sink_ = std::move(sink);
    const std::weak_ptr<Impl> weak = shared_from_this();
    detector_->Start(hints_,
                     [weak](absl::StatusOr<source::DetectedType> result) {
                       if (const auto self = weak.lock()) {
                         self->OnDetected(std::move(result));
                       }
                     });
  }

  void OnDetected(absl::StatusOr<source::DetectedType> result) {
    if (closed_ || sink_ == nullptr) {
      return;
    }
    if (!result.ok()) {
      Fail(result.status());
      return;
    }
    output_ = std::make_unique<StreamOutput>(
        [this](std::vector<penumbra::StreamInfo> streams) {
          OnStreams(std::move(streams));
        },
        [this](std::shared_ptr<const demux::SeekMap> seek_map) {
          seek_map_ = std::move(seek_map);
          MaybeOpen();
        },
        [this](penumbra::EncodedSample sample) {
          const uint32_t track_id = sample.track_id;
          samples_[track_id].push_back(std::move(sample));
          Deliver(track_id);
        });
    auto created = demux::CreateProgressiveDemuxer(result->type, *output_);
    if (!created.ok()) {
      Fail(created.status());
      return;
    }
    demuxer_ = std::move(*created);
    demuxer_->SetSourceInfo(data_source_->Info().size,
                            data_source_->Info().seekable);
    Pump();
  }

  void OnStreams(std::vector<penumbra::StreamInfo> streams) {
    stream_configs_.clear();
    for (penumbra::StreamInfo& stream : streams) {
      if (stream.type() == penumbra::StreamType::kText) {
        continue;
      }
      track_ids_.push_back(stream.track_id);
      stream_configs_.push_back(
          {.info = std::move(stream),
           .encryption_scheme = std::nullopt,
           .output_mode = penumbra::OutputMode::kTexture});
    }
    MaybeOpen();
  }

  void MaybeOpen() {
    if (opened_ || stream_configs_.empty() || seek_map_ == nullptr ||
        sink_ == nullptr) {
      return;
    }
    SourceDescription description;
    description.streams = std::move(stream_configs_);
    if (seek_map_->Duration().is_finite()) {
      description.duration = penumbra::MediaTime::FromMicroseconds(
          seek_map_->Duration().InMicroseconds());
    }
    if (seek_map_->IsSeekable() && description.duration.is_finite()) {
      description.seekable_range = SeekableRange{
          .start = penumbra::MediaTime::Zero(), .end = description.duration};
    }
    opened_ = true;
    sink_->OnOpened(std::move(description));
  }

  void Pump() {
    if (closed_ || demuxer_ == nullptr || read_ != nullptr) {
      return;
    }
    if (opened_ && !HasGrant()) {
      return;
    }
    for (size_t count = 0; count < 10000; ++count) {
      auto step = demuxer_->Step();
      if (!step.ok()) {
        Fail(step.status());
        return;
      }
      switch (step->kind) {
        case demux::StepKind::kYield: {
          const std::weak_ptr<Impl> weak = shared_from_this();
          runner_->PostTask([weak] {
            if (const auto self = weak.lock()) {
              self->Pump();
            }
          });
          return;
        }
        case demux::StepKind::kNeedMoreData:
          Read(step->byte_offset, step->wanted_bytes);
          return;
        case demux::StepKind::kSeek:
          Read(step->byte_offset, source::kDefaultChunkBytes);
          return;
        case demux::StepKind::kEndOfStream:
          End();
          return;
      }
      if (opened_ && !HasGrant()) {
        return;
      }
    }
    const std::weak_ptr<Impl> weak = shared_from_this();
    runner_->PostTask([weak] {
      if (const auto self = weak.lock()) {
        self->Pump();
      }
    });
  }

  void Read(uint64_t offset, size_t size) {
    const std::weak_ptr<Impl> weak = shared_from_this();
    read_ = reader_->Read(offset, size, [weak, offset](auto result) {
      if (const auto self = weak.lock()) {
        self->read_.reset();
        if (self->closed_) {
          return;
        }
        if (!result.ok()) {
          self->Fail(result.status());
          return;
        }
        if (result->empty()) {
          self->demuxer_->EndOfInput();
        } else {
          const absl::Status status = self->demuxer_->Append(offset, *result);
          if (!status.ok()) {
            self->Fail(status);
            return;
          }
        }
        self->Pump();
      }
    });
  }

  bool HasGrant() const {
    return std::ranges::any_of(
        grants_, [](const auto& entry) { return entry.second != 0; });
  }

  void Deliver(uint32_t track_id) {
    if (sink_ == nullptr || !opened_ || grants_[track_id] == 0) {
      return;
    }
    auto found = samples_.find(track_id);
    if (found == samples_.end() || found->second.empty()) {
      return;
    }
    std::vector<penumbra::EncodedSample> batch;
    while (!found->second.empty() && grants_[track_id] != 0) {
      batch.push_back(std::move(found->second.front()));
      found->second.pop_front();
      --grants_[track_id];
    }
    sink_->OnSamples(ticket_, track_id, std::move(batch));
  }

  void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                      size_t max_samples) {
    ticket_ = ticket;
    grants_[track_id] += max_samples;
    Deliver(track_id);
    if (ended_ && samples_[track_id].empty() && sink_ != nullptr &&
        eos_sent_.insert(track_id).second) {
      sink_->OnEndOfStream(ticket_, track_id);
    }
    Pump();
  }

  SeekPoint ResolveSeek(penumbra::MediaTime position, SeekMode mode,
                        FastSeekSnap snap) const {
    static_cast<void>(snap);
    penumbra::MediaTime decode = position;
    if (mode == SeekMode::kFast && seek_map_ != nullptr) {
      const auto point = seek_map_->SeekPointFor(position);
      if (point.has_value()) {
        decode = point->time;
      }
    }
    return {.presentation_time = position, .decode_time = decode};
  }

  void Seek(penumbra::SeekTicket ticket, penumbra::MediaTime position) {
    if (closed_ || demuxer_ == nullptr || seek_map_ == nullptr) {
      return;
    }
    const auto point = seek_map_->SeekPointFor(position);
    if (!point.has_value()) {
      Fail(absl::OutOfRangeError("the source cannot seek to this position"));
      return;
    }
    auto offset = demuxer_->Seek(*point);
    if (!offset.ok()) {
      Fail(offset.status());
      return;
    }
    if (read_ != nullptr) {
      read_->Cancel();
      read_.reset();
    }
    ticket_ = ticket;
    ended_ = false;
    samples_.clear();
    grants_.clear();
    eos_sent_.clear();
    demuxer_->Reset(*offset);
    Pump();
  }

  void End() {
    if (ended_ || sink_ == nullptr) {
      return;
    }
    ended_ = true;
    for (const uint32_t track_id : track_ids_) {
      if (grants_[track_id] != 0 && samples_[track_id].empty() &&
          eos_sent_.insert(track_id).second) {
        sink_->OnEndOfStream(ticket_, track_id);
      }
    }
  }

  void Fail(const absl::Status& status) {
    if (sink_ != nullptr && !closed_) {
      if (opened_) {
        sink_->OnSourceError(ErrorFor(status));
      } else {
        sink_->OnOpenFailed(ErrorFor(status));
      }
    }
    Close();
  }

  void Close() {
    if (closed_) {
      return;
    }
    closed_ = true;
    detector_->Cancel();
    if (read_ != nullptr) {
      read_->Cancel();
      read_.reset();
    }
    reader_->Close();
    data_source_->Close();
    demuxer_.reset();
    output_.reset();
    sink_.reset();
  }

  std::shared_ptr<source::DataSource> data_source_;
  std::shared_ptr<source::BufferedReader> reader_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  std::unique_ptr<source::SourceTypeDetector> detector_;
  source::SniffHints hints_;
  std::shared_ptr<SampleSink> sink_;
  std::unique_ptr<StreamOutput> output_;
  std::unique_ptr<demux::Demuxer> demuxer_;
  std::shared_ptr<const demux::SeekMap> seek_map_;
  std::shared_ptr<source::ReadRequest> read_;
  std::vector<penumbra::StreamConfig> stream_configs_;
  std::vector<uint32_t> track_ids_;
  std::set<uint32_t> eos_sent_;
  std::map<uint32_t, std::deque<penumbra::EncodedSample>> samples_;
  std::map<uint32_t, size_t> grants_;
  penumbra::SeekTicket ticket_ = 0;
  bool opened_ = false;
  bool ended_ = false;
  bool closed_ = false;
};

absl::StatusOr<std::unique_ptr<ProgressiveSource>> ProgressiveSource::Create(
    std::shared_ptr<source::DataSource> data_source,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<const base::UrlRedactor> redactor, source::SniffHints hints,
    source::BufferedReaderOptions reader_options) {
  if (data_source == nullptr || runner == nullptr || redactor == nullptr) {
    return absl::InvalidArgumentError(
        "progressive source dependencies must be provided");
  }
  auto reader =
      source::BufferedReader::Create(data_source, runner, reader_options);
  if (!reader.ok()) {
    return reader.status();
  }
  auto shared_reader =
      std::shared_ptr<source::BufferedReader>(std::move(*reader));
  auto impl = std::make_shared<Impl>(
      std::move(data_source), std::move(shared_reader), std::move(runner),
      std::move(redactor), std::move(hints));
  return std::unique_ptr<ProgressiveSource>(
      new ProgressiveSource(std::move(impl)));
}

ProgressiveSource::ProgressiveSource(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
ProgressiveSource::~ProgressiveSource() { Close(); }
void ProgressiveSource::Open(std::shared_ptr<SampleSink> sink) {
  impl_->Open(std::move(sink));
}
void ProgressiveSource::RequestSamples(penumbra::SeekTicket ticket,
                                       uint32_t track_id, size_t max_samples) {
  impl_->RequestSamples(ticket, track_id, max_samples);
}
SeekPoint ProgressiveSource::ResolveSeek(penumbra::MediaTime position,
                                         SeekMode mode,
                                         FastSeekSnap snap) const {
  return impl_->ResolveSeek(position, mode, snap);
}
void ProgressiveSource::Seek(penumbra::SeekTicket ticket,
                             penumbra::MediaTime decode_position) {
  impl_->Seek(ticket, decode_position);
}
void ProgressiveSource::Close() { impl_->Close(); }

}
