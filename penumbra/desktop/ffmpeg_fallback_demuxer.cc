#include "penumbra/desktop/ffmpeg_fallback_demuxer.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/fallback_demuxer.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "libavcodec/codec_id.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/defs.h"
#include "libavcodec/packet.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/avutil.h"
#include "libavutil/buffer.h"
#include "libavutil/channel_layout.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
}

namespace penumbra::desktop {
namespace {

constexpr int kIoBufferSize = 32768;
constexpr size_t kMaxQueuedPackets = 1024;
constexpr size_t kSkipSamplesMinimumSize = 8;
constexpr uint8_t kUnspecifiedColor = 2;

struct FormatDeleter {
  void operator()(AVFormatContext* context) const {
    avformat_close_input(&context);
  }
};

struct IoDeleter {
  void operator()(AVIOContext* context) const {
    if (context != nullptr) {
      void* buffer = context->buffer;
      av_free(buffer);
    }
    avio_context_free(&context);
  }
};

struct PacketDeleter {
  void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};

CodecId CodecOf(AVCodecID id) {
  switch (id) {
    case AV_CODEC_ID_H264:
      return CodecId::kH264;
    case AV_CODEC_ID_HEVC:
      return CodecId::kHevc;
    case AV_CODEC_ID_VP9:
      return CodecId::kVp9;
    case AV_CODEC_ID_AV1:
      return CodecId::kAv1;
    case AV_CODEC_ID_AAC:
      return CodecId::kAac;
    case AV_CODEC_ID_OPUS:
      return CodecId::kOpus;
    case AV_CODEC_ID_AC3:
      return CodecId::kAc3;
    case AV_CODEC_ID_EAC3:
      return CodecId::kEac3;
    case AV_CODEC_ID_FLAC:
      return CodecId::kFlac;
    case AV_CODEC_ID_MP3:
      return CodecId::kMp3;
    default:
      return CodecId::kUnknown;
  }
}

Rational RationalOf(AVRational value) {
  return Rational::Create(value.num, value.den).value_or(Rational());
}

Timestamp TimestampOf(int64_t value, Rational time_base) {
  return {.value = value == AV_NOPTS_VALUE ? Timestamp::kUnknown : value,
          .time_base = time_base};
}

uint32_t ReadLittleEndian32(std::span<const uint8_t> bytes) {
  return uint32_t{bytes[0]} | uint32_t{bytes[1]} << 8 |
         uint32_t{bytes[2]} << 16 | uint32_t{bytes[3]} << 24;
}

uint8_t ColorValue(int value) {
  return value >= 0 && value <= 255 ? static_cast<uint8_t>(value)
                                    : kUnspecifiedColor;
}

std::string AvErrorText(int error) {
  std::string text(AV_ERROR_MAX_STRING_SIZE, '\0');
  if (av_strerror(error, text.data(), text.size()) < 0) {
    return absl::StrCat("error ", error);
  }
  text.resize(text.find('\0'));
  return text;
}

absl::Status StatusOfAvError(int error, std::string_view what) {
  const std::string message = absl::StrCat(what, ": ", AvErrorText(error));
  switch (error) {
    case AVERROR_INVALIDDATA:
    case AVERROR_DEMUXER_NOT_FOUND:
    case AVERROR_STREAM_NOT_FOUND:
      return absl::InvalidArgumentError(message);
    case AVERROR_EOF:
      return absl::DataLossError(message);
    case AVERROR_EXIT:
      return absl::CancelledError(message);
    case AVERROR_BUG:
      return absl::InternalError(message);
    default:
      break;
  }
  if (error == AVERROR(ENOMEM)) {
    return absl::ResourceExhaustedError(message);
  }
  if (error == AVERROR(EIO)) {
    return absl::UnavailableError(message);
  }
  return absl::InternalError(message);
}

bool IsPlayableKind(const AVStream& stream) {
  const AVMediaType type = stream.codecpar->codec_type;
  if ((stream.disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
    return false;
  }
  return type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO;
}

bool IsEncrypted(const AVStream& stream) {
  const AVCodecParameters& parameters = *stream.codecpar;
  return av_packet_side_data_get(parameters.coded_side_data,
                                 parameters.nb_coded_side_data,
                                 AV_PKT_DATA_ENCRYPTION_INIT_INFO) != nullptr;
}

StreamInfo InfoOf(const AVStream& stream) {
  const AVCodecParameters& parameters = *stream.codecpar;
  StreamInfo info;
  info.track_id = static_cast<uint32_t>(stream.index);
  info.codec = CodecOf(parameters.codec_id);
  info.time_base = RationalOf(stream.time_base);
  if (parameters.extradata != nullptr && parameters.extradata_size > 0) {
    info.codec_config = SharedBytes::Copy(
        {parameters.extradata, static_cast<size_t>(parameters.extradata_size)});
  }
  if (const AVDictionaryEntry* language =
          av_dict_get(stream.metadata, "language", nullptr, 0);
      language != nullptr && language->value != nullptr) {
    info.language = language->value;
  }
  if (parameters.codec_type == AVMEDIA_TYPE_VIDEO) {
    VideoInfo video;
    video.coded_width = parameters.width;
    video.coded_height = parameters.height;
    video.display_width = parameters.width;
    video.display_height = parameters.height;
    const AVRational aspect = stream.sample_aspect_ratio.num != 0
                                  ? stream.sample_aspect_ratio
                                  : parameters.sample_aspect_ratio;
    video.sample_aspect_ratio = aspect.num > 0 && aspect.den > 0
                                    ? RationalOf(aspect)
                                    : Rational::Of(1, 1);
    video.frame_rate =
        RationalOf(stream.avg_frame_rate.num != 0 ? stream.avg_frame_rate
                                                  : stream.r_frame_rate);
    video.color = {.primaries = ColorValue(parameters.color_primaries),
                   .transfer = ColorValue(parameters.color_trc),
                   .matrix = ColorValue(parameters.color_space),
                   .full_range = parameters.color_range == AVCOL_RANGE_JPEG,
                   .chroma_siting = ChromaSiting::kUnspecified};
    info.format = video;
  } else {
    AudioInfo audio;
    audio.sample_rate = parameters.sample_rate;
    audio.channel_count = parameters.ch_layout.nb_channels;
    audio.channel_mask = 0;
    audio.sample_format = SampleFormat::kUnknown;
    info.format = audio;
  }
  return info;
}

SharedBytes WrapPacket(const AVPacket& packet) {
  const std::span<const uint8_t> bytes(packet.data,
                                       static_cast<size_t>(packet.size));
  if (packet.buf == nullptr) {
    return SharedBytes::Copy(bytes);
  }
  AVBufferRef* reference = av_buffer_ref(packet.buf);
  if (reference == nullptr) {
    return SharedBytes::Copy(bytes);
  }
  const std::shared_ptr<AVBufferRef> owner(
      reference, [](AVBufferRef* buffer) { av_buffer_unref(&buffer); });
  return SharedBytes::Wrap(owner, bytes);
}

void ApplySkipSamples(const AVPacket& packet, const StreamInfo& info,
                      EncodedSample& sample) {
  size_t side_size = 0;
  const uint8_t* side =
      av_packet_get_side_data(&packet, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
  const auto* audio = std::get_if<AudioInfo>(&info.format);
  if (side == nullptr || side_size < kSkipSamplesMinimumSize ||
      audio == nullptr || audio->sample_rate <= 0) {
    return;
  }
  const std::span<const uint8_t> data(side, side_size);
  const Rational period =
      Rational::Create(1, audio->sample_rate).value_or(Rational());
  const uint32_t start = ReadLittleEndian32(data.first(4));
  const uint32_t end = ReadLittleEndian32(data.subspan(4, 4));
  if (start > 0) {
    sample.trim_start = {.value = start, .time_base = period};
  }
  if (end > 0) {
    sample.trim_end = {.value = end, .time_base = period};
  }
}

struct TrackState {
  StreamInfo info;
  std::deque<EncodedSample> queue;
};

struct Work {
  bool seek = false;
  SeekTicket ticket = 0;
  uint32_t track_id = 0;
  size_t max_samples = 0;
  MediaTime position;
};

class Session final : public FallbackDemuxer {
 public:
  Session(FallbackDemuxerRequest request,
          std::shared_ptr<FallbackByteSource> source,
          std::shared_ptr<FallbackDemuxerCallbacks> callbacks)
      : request_(std::move(request)),
        source_(std::move(source)),
        callbacks_(std::move(callbacks)),
        ticket_(request_.initial_ticket) {}
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session() override {
    static_cast<void>(Halt());
    JoinWorker();
  }

  void Start() {
    worker_ = std::thread([this] { Run(); });
  }

  absl::Status RequestSamples(SeekTicket ticket, uint32_t track_id,
                              size_t max_samples) override {
    const std::scoped_lock lock(mutex_);
    if (stopped_ || failed_ || !opened_) {
      return absl::FailedPreconditionError("the demuxer is not running");
    }
    if (ticket > ticket_) {
      return absl::InvalidArgumentError("future ticket");
    }
    if (ticket < ticket_) {
      return absl::OkStatus();
    }
    if (!track_ids_.contains(track_id) || max_samples == 0) {
      return absl::InvalidArgumentError("unknown track or empty grant");
    }
    if (ended_.contains(track_id) || grants_.contains(track_id)) {
      return absl::FailedPreconditionError("the track has an open grant");
    }
    grants_[track_id] = max_samples;
    wake_.notify_all();
    return absl::OkStatus();
  }

  absl::Status Seek(SeekTicket ticket, MediaTime position) override {
    const std::scoped_lock lock(mutex_);
    if (stopped_ || failed_ || !opened_) {
      return absl::FailedPreconditionError("the demuxer is not running");
    }
    if (!position.is_finite()) {
      return absl::InvalidArgumentError("the position is not finite");
    }
    if (ticket <= ticket_) {
      return absl::InvalidArgumentError("the ticket must increase");
    }
    if (!seekable_) {
      return absl::UnimplementedError("the source is not seekable");
    }
    ticket_ = ticket;
    pending_seek_ = position;
    grants_.clear();
    ended_.clear();
    seek_flag_.store(true);
    wake_.notify_all();
    return absl::OkStatus();
  }

  absl::Status Stop() override {
    if (!Halt()) {
      return absl::UnavailableError("the worker is still draining");
    }
    JoinWorker();
    return absl::OkStatus();
  }

  int ReadFromSource(uint8_t* buffer, int size) {
    if (interrupt_.load()) {
      return AVERROR_EXIT;
    }
    const auto read = source_->ReadAt(
        io_position_, std::span<uint8_t>(buffer, static_cast<size_t>(size)));
    if (!read.ok()) {
      io_status_ = read.status();
      return AVERROR(EIO);
    }
    if (*read == 0) {
      return AVERROR_EOF;
    }
    if (std::cmp_greater(*read, size)) {
      io_status_ = absl::InternalError("the source over-read");
      return AVERROR(EIO);
    }
    io_position_ += *read;
    return static_cast<int>(*read);
  }

  int64_t SeekSource(int64_t offset, int whence) {
    const std::optional<uint64_t> size = source_->Size();
    if (whence == AVSEEK_SIZE) {
      return size.has_value() ? static_cast<int64_t>(*size) : -1;
    }
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
      case SEEK_SET:
        break;
      case SEEK_CUR:
        base = static_cast<int64_t>(io_position_);
        break;
      case SEEK_END:
        if (!size.has_value()) {
          return -1;
        }
        base = static_cast<int64_t>(*size);
        break;
      default:
        return -1;
    }
    const int64_t target = base + offset;
    if (target < 0 || (size.has_value() && std::cmp_greater(target, *size))) {
      return -1;
    }
    io_position_ = static_cast<uint64_t>(target);
    return target;
  }

  [[nodiscard]] bool Interrupted() const { return interrupt_.load(); }

 private:
  static int ReadThunk(void* opaque, uint8_t* data, int size) {
    return static_cast<Session*>(opaque)->ReadFromSource(data, size);
  }

  static int64_t SeekThunk(void* opaque, int64_t offset, int whence) {
    return static_cast<Session*>(opaque)->SeekSource(offset, whence);
  }

  static int InterruptThunk(void* opaque) {
    return static_cast<Session*>(opaque)->Interrupted() ? 1 : 0;
  }

  bool Halt() {
    {
      const std::scoped_lock lock(mutex_);
      stopped_ = true;
      grants_.clear();
    }
    interrupt_.store(true);
    wake_.notify_all();
    return worker_done_.load();
  }

  void JoinWorker() {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  [[nodiscard]] bool Live() {
    const std::scoped_lock lock(mutex_);
    return !stopped_;
  }

  void Run() {
    absl::Status status = Open();
    if (status.ok()) {
      status = ServeLoop();
    }
    if (!status.ok() && Live()) {
      {
        const std::scoped_lock lock(mutex_);
        failed_ = true;
        grants_.clear();
      }
      static_cast<void>(callbacks_->OnError(std::move(status)));
    }
    tracks_.clear();
    format_.reset();
    io_.reset();
    worker_done_.store(true);
  }

  absl::Status Open() {
    auto* buffer = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
    if (buffer == nullptr) {
      return absl::ResourceExhaustedError("no memory for the I/O buffer");
    }
    io_.reset(avio_alloc_context(
        buffer, kIoBufferSize, 0, this, &Session::ReadThunk, nullptr,
        source_->Seekable() ? &Session::SeekThunk : nullptr));
    if (io_ == nullptr) {
      av_free(buffer);
      return absl::ResourceExhaustedError("no memory for the I/O context");
    }
    AVFormatContext* raw = avformat_alloc_context();
    if (raw == nullptr) {
      return absl::ResourceExhaustedError("no memory for the format context");
    }
    raw->pb = io_.get();
    raw->flags |= AVFMT_FLAG_CUSTOM_IO;
    raw->interrupt_callback.opaque = this;
    raw->interrupt_callback.callback = &Session::InterruptThunk;
    const char* hint =
        request_.name_hint.empty() ? nullptr : request_.name_hint.c_str();
    int result = avformat_open_input(&raw, hint, nullptr, nullptr);
    if (result < 0) {
      return OpenFailure(result, "avformat_open_input");
    }
    format_.reset(raw);
    result = avformat_find_stream_info(format_.get(), nullptr);
    if (result < 0) {
      return OpenFailure(result, "avformat_find_stream_info");
    }
    return Announce();
  }

  absl::Status OpenFailure(int error, std::string_view what) {
    if (!io_status_.ok()) {
      return io_status_;
    }
    return StatusOfAvError(error, what);
  }

  absl::Status Announce() {
    FallbackDemuxerInfo info;
    info.container = format_->iformat != nullptr ? format_->iformat->name : "";
    const std::span<AVStream*> streams(format_->streams, format_->nb_streams);
    for (AVStream* stream : streams) {
      if (!IsPlayableKind(*stream)) {
        stream->discard = AVDISCARD_ALL;
        continue;
      }
      if (IsEncrypted(*stream)) {
        return absl::UnimplementedError(
            "the container carries an encrypted stream");
      }
      TrackState track;
      track.info = InfoOf(*stream);
      info.streams.push_back(track.info);
      tracks_.emplace(track.info.track_id, std::move(track));
    }
    if (tracks_.empty()) {
      return absl::UnimplementedError("no audio or video stream");
    }
    if (format_->duration != AV_NOPTS_VALUE && format_->duration >= 0) {
      info.duration = TimeDelta::FromMicroseconds(format_->duration);
    }
    info.seekable = source_->Seekable() && format_->pb != nullptr &&
                    (format_->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0;
    packet_.reset(av_packet_alloc());
    if (packet_ == nullptr) {
      return absl::ResourceExhaustedError("no memory for a packet");
    }
    {
      const std::scoped_lock lock(mutex_);
      for (const auto& [id, track] : tracks_) {
        track_ids_.insert(id);
      }
      seekable_ = info.seekable;
      opened_ = true;
    }
    if (!Live()) {
      return absl::OkStatus();
    }
    return callbacks_->OnOpen(std::move(info));
  }

  absl::Status ServeLoop() {
    while (true) {
      Work work;
      if (!NextWork(work)) {
        return absl::OkStatus();
      }
      absl::Status status = work.seek ? PerformSeek(work) : Serve(work);
      if (!status.ok()) {
        return status;
      }
    }
  }

  bool NextWork(Work& work) {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] {
      return stopped_ || pending_seek_.has_value() || !grants_.empty();
    });
    if (stopped_) {
      return false;
    }
    work.ticket = ticket_;
    if (pending_seek_.has_value()) {
      work.seek = true;
      work.position = *pending_seek_;
      pending_seek_.reset();
      seek_flag_.store(false);
      return true;
    }
    work.seek = false;
    work.track_id = grants_.begin()->first;
    work.max_samples = grants_.begin()->second;
    return true;
  }

  absl::Status PerformSeek(const Work& work) {
    const int64_t micros = std::max<int64_t>(work.position.InMicroseconds(), 0);
    const int result =
        av_seek_frame(format_.get(), -1, micros, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
      return !io_status_.ok() ? io_status_
                              : StatusOfAvError(result, "av_seek_frame");
    }
    for (auto& [id, track] : tracks_) {
      track.queue.clear();
    }
    end_of_file_ = false;
    return absl::OkStatus();
  }

  absl::Status Serve(const Work& work) {
    TrackState& track = tracks_.at(work.track_id);
    while (track.queue.empty() && !end_of_file_) {
      if (interrupt_.load() || seek_flag_.load()) {
        return absl::OkStatus();
      }
      absl::Status status = ReadOnePacket();
      if (!status.ok()) {
        return status;
      }
    }
    if (interrupt_.load() || seek_flag_.load()) {
      return absl::OkStatus();
    }
    if (track.queue.empty()) {
      if (!ConsumeGrant(work, true)) {
        return absl::OkStatus();
      }
      return callbacks_->OnEndOfStream(work.ticket, work.track_id);
    }
    std::vector<EncodedSample> batch;
    const size_t count = std::min(work.max_samples, track.queue.size());
    batch.reserve(count);
    for (size_t index = 0; index < count; ++index) {
      batch.push_back(std::move(track.queue.front()));
      track.queue.pop_front();
    }
    if (!ConsumeGrant(work, false)) {
      return absl::OkStatus();
    }
    return callbacks_->OnSamples(work.ticket, work.track_id, std::move(batch));
  }

  bool ConsumeGrant(const Work& work, bool end) {
    const std::scoped_lock lock(mutex_);
    if (stopped_ || ticket_ != work.ticket ||
        !grants_.contains(work.track_id)) {
      return false;
    }
    grants_.erase(work.track_id);
    if (end) {
      ended_.insert(work.track_id);
    }
    return true;
  }

  absl::Status ReadOnePacket() {
    const int result = av_read_frame(format_.get(), packet_.get());
    if (result == AVERROR_EOF) {
      end_of_file_ = true;
      return absl::OkStatus();
    }
    if (result < 0) {
      return !io_status_.ok() ? io_status_
                              : StatusOfAvError(result, "av_read_frame");
    }
    const auto found =
        tracks_.find(static_cast<uint32_t>(packet_->stream_index));
    if (found == tracks_.end() || packet_->size <= 0) {
      av_packet_unref(packet_.get());
      return absl::OkStatus();
    }
    auto sample = MapPacket(*packet_, found->second);
    av_packet_unref(packet_.get());
    if (!sample.ok()) {
      return sample.status();
    }
    found->second.queue.push_back(*std::move(sample));
    if (found->second.queue.size() > kMaxQueuedPackets) {
      return absl::ResourceExhaustedError(
          "the container interleaves its streams too far apart");
    }
    return absl::OkStatus();
  }

  static absl::StatusOr<EncodedSample> MapPacket(const AVPacket& packet,
                                                 TrackState& track) {
    if (av_packet_get_side_data(&packet, AV_PKT_DATA_ENCRYPTION_INFO,
                                nullptr) != nullptr) {
      return absl::UnimplementedError("an encrypted packet");
    }
    size_t extradata_size = 0;
    const uint8_t* extradata = av_packet_get_side_data(
        &packet, AV_PKT_DATA_NEW_EXTRADATA, &extradata_size);
    EncodedSample sample;
    if (extradata != nullptr && extradata_size > 0) {
      track.info.codec_config = SharedBytes::Copy({extradata, extradata_size});
      sample.config_change = std::make_shared<const StreamInfo>(track.info);
    }
    sample.track_id = track.info.track_id;
    sample.data = WrapPacket(packet);
    sample.dts = TimestampOf(packet.dts, track.info.time_base);
    sample.pts =
        TimestampOf(packet.pts == AV_NOPTS_VALUE ? packet.dts : packet.pts,
                    track.info.time_base);
    if (packet.duration > 0) {
      sample.duration = TimestampOf(packet.duration, track.info.time_base);
    }
    sample.key_frame = (packet.flags & AV_PKT_FLAG_KEY) != 0;
    ApplySkipSamples(packet, track.info, sample);
    return sample;
  }

  const FallbackDemuxerRequest request_;
  const std::shared_ptr<FallbackByteSource> source_;
  const std::shared_ptr<FallbackDemuxerCallbacks> callbacks_;

  std::mutex mutex_;
  std::condition_variable wake_;
  SeekTicket ticket_;
  std::map<uint32_t, size_t> grants_;
  std::set<uint32_t> ended_;
  std::set<uint32_t> track_ids_;
  std::optional<MediaTime> pending_seek_;
  bool opened_ = false;
  bool seekable_ = false;
  bool failed_ = false;
  bool stopped_ = false;

  std::atomic<bool> interrupt_{false};
  std::atomic<bool> seek_flag_{false};
  std::atomic<bool> worker_done_{false};

  uint64_t io_position_ = 0;
  absl::Status io_status_;
  bool end_of_file_ = false;
  std::map<uint32_t, TrackState> tracks_;
  std::unique_ptr<AVPacket, PacketDeleter> packet_;
  std::unique_ptr<AVFormatContext, FormatDeleter> format_;
  std::unique_ptr<AVIOContext, IoDeleter> io_;
  std::thread worker_;
};

}

absl::StatusOr<FallbackProbe> FfmpegFallbackProvider::ProbeFallbackDemuxer(
    std::span<const uint8_t> window, std::string_view name_hint) const {
  std::vector<uint8_t> padded(window.begin(), window.end());
  padded.resize(window.size() + AVPROBE_PADDING_SIZE, 0);
  const std::string name(name_hint);
  AVProbeData probe;
  probe.filename = name.c_str();
  probe.buf = padded.data();
  probe.buf_size = static_cast<int>(window.size());
  probe.mime_type = nullptr;
  int score = 0;
  const AVInputFormat* format = av_probe_input_format3(&probe, 1, &score);
  if (format == nullptr || score < AVPROBE_SCORE_RETRY) {
    return FallbackProbe{.supported = false, .container = ""};
  }
  return FallbackProbe{.supported = true, .container = format->name};
}

absl::StatusOr<std::unique_ptr<FallbackDemuxer>>
FfmpegFallbackProvider::OpenFallbackDemuxer(
    FallbackDemuxerRequest request, std::shared_ptr<FallbackByteSource> source,
    std::shared_ptr<FallbackDemuxerCallbacks> callbacks) {
  if (source == nullptr || callbacks == nullptr ||
      request.initial_ticket == 0) {
    return absl::InvalidArgumentError(
        "a fallback demuxer needs a source, callbacks and a nonzero ticket");
  }
  auto session = std::make_unique<Session>(
      std::move(request), std::move(source), std::move(callbacks));
  session->Start();
  return std::unique_ptr<FallbackDemuxer>(std::move(session));
}

}
