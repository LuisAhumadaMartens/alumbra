#include "penumbra/desktop/ffmpeg_decoder.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/desktop/decode_errors.h"
#include "penumbra/desktop/decoder_config.h"
#include "penumbra/desktop/ffmpeg_frame.h"
#include "penumbra/desktop/worker_decoder.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"
#include "libavcodec/defs.h"
#include "libavcodec/packet.h"
#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
}

namespace penumbra::desktop {
namespace {

constexpr size_t kVideoGrantSamples = 8;
constexpr size_t kAudioGrantSamples = 16;
constexpr int kSkipSamplesSideDataSize = 10;
constexpr int kMaxSendAttempts = 2;

bool IsVideo(CodecId codec) {
  return codec == CodecId::kH264 || codec == CodecId::kHevc ||
         codec == CodecId::kVp9 || codec == CodecId::kAv1;
}

bool AcceptsNewExtradata(CodecId codec) {
  return codec == CodecId::kH264 || codec == CodecId::kHevc;
}

std::optional<AVRational> ToAvRational(Rational value) {
  constexpr int64_t kMax = std::numeric_limits<int>::max();
  if (!value.is_positive() || value.numerator() > kMax ||
      value.denominator() > kMax) {
    return std::nullopt;
  }
  return AVRational{static_cast<int>(value.numerator()),
                    static_cast<int>(value.denominator())};
}

void WriteLittleEndian32(std::span<uint8_t> destination, uint32_t value) {
  for (size_t index = 0; index < 4; ++index) {
    destination[index] = static_cast<uint8_t>(value >> (8 * index));
  }
}

int64_t TrimSamples(Timestamp trim, int sample_rate) {
  if (!trim.is_known() || sample_rate <= 0) {
    return 0;
  }
  const auto period = Rational::Create(1, sample_rate);
  if (!period.has_value()) {
    return 0;
  }
  const auto samples = Rescale(trim, *period, Rounding::kNearest);
  return samples.has_value() ? std::max<int64_t>(samples->value, 0) : 0;
}

}

void AvCodecContextDeleter::operator()(AVCodecContext* context) const {
  avcodec_free_context(&context);
}

void AvPacketDeleter::operator()(AVPacket* packet) const {
  av_packet_free(&packet);
}

absl::StatusOr<std::unique_ptr<Decoder>> FfmpegDecoder::Create(
    const AVCodec& codec, const StreamConfig& config, SeekTicket ticket,
    std::shared_ptr<DecoderCallbacks> callbacks,
    const FfmpegDecoderOptions& options) {
  if (options.video_threads < 0 || options.video_threads > kMaxVideoThreads) {
    return absl::InvalidArgumentError(
        absl::StrCat("decode.video.threads must be from 0 to ",
                     kMaxVideoThreads, ", not ", options.video_threads));
  }
  const size_t grant =
      IsVideo(config.info.codec) ? kVideoGrantSamples : kAudioGrantSamples;
  auto decoder = std::make_unique<FfmpegDecoder>(config.info, ticket,
                                                 std::move(callbacks), grant);
  auto status = decoder->Open(codec, config, options);
  if (!status.ok()) {
    return status;
  }
  decoder->Start();
  return decoder;
}

FfmpegDecoder::FfmpegDecoder(StreamInfo info, SeekTicket ticket,
                             std::shared_ptr<DecoderCallbacks> callbacks,
                             size_t grant_samples)
    : WorkerDecoder(std::move(info), ticket, std::move(callbacks),
                    grant_samples) {}

FfmpegDecoder::~FfmpegDecoder() { (void)Stop(); }

absl::Status FfmpegDecoder::Open(const AVCodec& codec,
                                 const StreamConfig& config,
                                 const FfmpegDecoderOptions& options) {
  const StreamInfo& info = config.info;
  type_ = IsVideo(info.codec) ? StreamType::kVideo : StreamType::kAudio;
  time_base_ =
      info.time_base.is_positive() ? info.time_base : kMicrosecondTimeBase;
  const auto time_base = ToAvRational(time_base_);
  if (!time_base.has_value()) {
    return DecoderInitializationError("the stream time base is out of range");
  }
  auto extradata = FfmpegExtradata(info);
  if (!extradata.ok()) {
    return extradata.status();
  }
  extradata_ = *std::move(extradata);

  context_.reset(avcodec_alloc_context3(&codec));
  packet_.reset(av_packet_alloc());
  scratch_ = AllocateAvFrame();
  if (context_ == nullptr || packet_ == nullptr || scratch_ == nullptr) {
    return DecoderInitializationError("out of memory");
  }
  context_->pkt_timebase = *time_base;
  if (!extradata_.empty()) {
    auto* buffer = static_cast<uint8_t*>(
        av_mallocz(extradata_.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (buffer == nullptr) {
      return DecoderInitializationError("out of memory");
    }
    std::memcpy(buffer, extradata_.data(), extradata_.size());
    context_->extradata = buffer;
    context_->extradata_size = static_cast<int>(extradata_.size());
  }
  if (const auto* video = std::get_if<VideoInfo>(&info.format)) {
    frame_rate_ = video->frame_rate;
    context_->thread_count = options.video_threads;
    context_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  } else if (const auto* audio = std::get_if<AudioInfo>(&info.format)) {
    if (audio->sample_rate > 0) {
      context_->sample_rate = audio->sample_rate;
    }
    if (audio->channel_count > 0) {
      av_channel_layout_default(&context_->ch_layout, audio->channel_count);
    }
  }
  const int opened = avcodec_open2(context_.get(), &codec, nullptr);
  if (opened < 0) {
    return DecoderInitializationError(absl::StrCat(
        "avcodec_open2 for ", codec.name, ": ", AvErrorText(opened)));
  }
  return absl::OkStatus();
}

int64_t FfmpegDecoder::ToStreamTicks(Timestamp timestamp) const {
  if (!timestamp.is_known()) {
    return AV_NOPTS_VALUE;
  }
  if (timestamp.time_base == time_base_) {
    return timestamp.value;
  }
  const auto converted = Rescale(timestamp, time_base_, Rounding::kNearest);
  return converted.has_value() ? converted->value : AV_NOPTS_VALUE;
}

absl::Status FfmpegDecoder::CheckConfigChange(const StreamInfo& from,
                                              const StreamInfo& to) const {
  if (to.codec != from.codec || to.type() != from.type()) {
    return absl::UnimplementedError(
        "a codec change needs a new decoder instance");
  }
  const auto before = FfmpegExtradata(from);
  const auto after = FfmpegExtradata(to);
  if (!after.ok()) {
    return after.status();
  }
  if (before.ok() && *before != *after && !AcceptsNewExtradata(to.codec)) {
    return absl::UnimplementedError(
        "this decoder cannot take a new configuration in place");
  }
  return absl::OkStatus();
}

absl::Status FfmpegDecoder::ApplyConfigChange(AVPacket& packet,
                                              const EncodedSample& sample) {
  if (sample.config_change == nullptr) {
    return absl::OkStatus();
  }
  const StreamInfo& change = *sample.config_change;
  if (const auto* video = std::get_if<VideoInfo>(&change.format)) {
    frame_rate_ = video->frame_rate;
  }
  auto extradata = FfmpegExtradata(change);
  if (!extradata.ok() || *extradata == extradata_ || extradata->empty() ||
      !AcceptsNewExtradata(change.codec)) {
    return absl::OkStatus();
  }
  extradata_ = *std::move(extradata);
  uint8_t* side = av_packet_new_side_data(&packet, AV_PKT_DATA_NEW_EXTRADATA,
                                          extradata_.size());
  if (side == nullptr) {
    return StatusFromAvError(AVERROR(ENOMEM), "av_packet_new_side_data");
  }
  std::ranges::copy(extradata_,
                    std::span<uint8_t>(side, extradata_.size()).begin());
  return absl::OkStatus();
}

absl::Status FfmpegDecoder::AttachTrim(AVPacket& packet,
                                       const EncodedSample& sample) {
  if (type_ != StreamType::kAudio ||
      (!sample.trim_start.is_known() && !sample.trim_end.is_known())) {
    return absl::OkStatus();
  }
  const int rate = context_->sample_rate;
  const int64_t start = TrimSamples(sample.trim_start, rate);
  const int64_t end = TrimSamples(sample.trim_end, rate);
  if (start <= 0 && end <= 0) {
    return absl::OkStatus();
  }
  uint8_t* side = av_packet_new_side_data(&packet, AV_PKT_DATA_SKIP_SAMPLES,
                                          kSkipSamplesSideDataSize);
  if (side == nullptr) {
    return StatusFromAvError(AVERROR(ENOMEM), "av_packet_new_side_data");
  }
  const std::span<uint8_t> data(side, kSkipSamplesSideDataSize);
  std::ranges::fill(data, 0);
  WriteLittleEndian32(data.first(4), static_cast<uint32_t>(start));
  WriteLittleEndian32(data.subspan(4, 4), static_cast<uint32_t>(end));
  return absl::OkStatus();
}

absl::Status FfmpegDecoder::Decode(const EncodedSample& sample) {
  AVPacket* packet = packet_.get();
  av_packet_unref(packet);
  if (sample.data.empty()) {
    return CorruptSampleError("an empty sample");
  }
  if (sample.data.size() >
      static_cast<size_t>(std::numeric_limits<int>::max())) {
    return CorruptSampleError("a sample is too large");
  }
  const int allocated =
      av_new_packet(packet, static_cast<int>(sample.data.size()));
  if (allocated < 0) {
    return StatusFromAvError(allocated, "av_new_packet");
  }
  std::memcpy(packet->data, sample.data.data(), sample.data.size());
  packet->pts = ToStreamTicks(sample.pts);
  packet->dts = ToStreamTicks(sample.dts);
  const int64_t duration = ToStreamTicks(sample.duration);
  packet->duration = duration == AV_NOPTS_VALUE ? 0 : duration;
  if (sample.key_frame) {
    packet->flags |= AV_PKT_FLAG_KEY;
  }
  auto prepared = ApplyConfigChange(*packet, sample);
  if (!prepared.ok()) {
    return prepared;
  }
  prepared = AttachTrim(*packet, sample);
  if (!prepared.ok()) {
    return prepared;
  }

  int sent = AVERROR(EAGAIN);
  absl::Status received;
  for (int attempt = 0; attempt < kMaxSendAttempts && sent == AVERROR(EAGAIN);
       ++attempt) {
    sent = avcodec_send_packet(context_.get(), packet);
    auto drained = ReceiveFrames();
    if (received.ok()) {
      received = std::move(drained);
    }
  }
  if (sent < 0) {
    return StatusFromAvError(sent, "avcodec_send_packet");
  }
  return received;
}

absl::Status FfmpegDecoder::Drain() {
  const int sent = avcodec_send_packet(context_.get(), nullptr);
  if (sent < 0 && sent != AVERROR_EOF) {
    return StatusFromAvError(sent, "avcodec_send_packet");
  }
  return ReceiveFrames();
}

void FfmpegDecoder::Reset() {
  if (context_ != nullptr) {
    avcodec_flush_buffers(context_.get());
  }
  next_audio_time_ = MediaTime::Unknown();
}

absl::Status FfmpegDecoder::ReceiveFrames() {
  absl::Status first_error;
  while (true) {
    const int received = avcodec_receive_frame(context_.get(), scratch_.get());
    if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
      return first_error;
    }
    if (received < 0) {
      return first_error.ok()
                 ? StatusFromAvError(received, "avcodec_receive_frame")
                 : first_error;
    }
    AvFramePtr frame = AllocateAvFrame();
    if (frame == nullptr) {
      return StatusFromAvError(AVERROR(ENOMEM), "av_frame_alloc");
    }
    av_frame_move_ref(frame.get(), scratch_.get());
    if (IsCorruptFrame(*frame)) {
      if (first_error.ok()) {
        first_error =
            CorruptSampleError("the decoder flagged a frame as corrupt");
      }
      continue;
    }
    auto status = Output(std::move(frame));
    if (!status.ok()) {
      return status;
    }
  }
}

absl::Status FfmpegDecoder::Output(AvFramePtr frame) {
  if (type_ == StreamType::kVideo) {
    auto wrapped = WrapVideoFrame(std::move(frame), time_base_, frame_rate_);
    if (!wrapped.ok()) {
      return wrapped.status();
    }
    return Emit(*std::move(wrapped));
  }
  auto wrapped = WrapAudioFrame(std::move(frame), time_base_, next_audio_time_);
  if (!wrapped.ok()) {
    return wrapped.status();
  }
  if (wrapped->presentation_time != MediaTime::Unknown() &&
      wrapped->sample_rate > 0) {
    const int64_t micros =
        int64_t{wrapped->frame_count} * 1'000'000 / wrapped->sample_rate;
    next_audio_time_ =
        wrapped->presentation_time + TimeDelta::FromMicroseconds(micros);
  }
  return Emit(*std::move(wrapped));
}

}
