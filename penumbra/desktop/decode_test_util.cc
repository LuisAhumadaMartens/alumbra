#include "penumbra/desktop/decode_test_util.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "penumbra/desktop/decode_errors.h"
#include "penumbra/desktop/ffmpeg_frame.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/channel_layout.h"
#include "libavutil/md5.h"
#include "libavutil/mem.h"
}

namespace penumbra::desktop::decode_test {
namespace {

constexpr int kIoBufferSize = 4096;

struct MemoryReader {
  std::span<const uint8_t> bytes;
  size_t position = 0;
};

int ReadMemory(void* opaque, uint8_t* buffer, int size) {
  auto* reader = static_cast<MemoryReader*>(opaque);
  if (reader->position >= reader->bytes.size()) {
    return AVERROR_EOF;
  }
  const size_t count = std::min(static_cast<size_t>(size),
                                reader->bytes.size() - reader->position);
  std::memcpy(buffer, reader->bytes.data() + reader->position, count);
  reader->position += count;
  return static_cast<int>(count);
}

int64_t SeekMemory(void* opaque, int64_t offset, int whence) {
  auto* reader = static_cast<MemoryReader*>(opaque);
  if (whence == AVSEEK_SIZE) {
    return static_cast<int64_t>(reader->bytes.size());
  }
  int64_t base = 0;
  switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      base = static_cast<int64_t>(reader->position);
      break;
    case SEEK_END:
      base = static_cast<int64_t>(reader->bytes.size());
      break;
    default:
      return -1;
  }
  const int64_t target = base + offset;
  if (target < 0 || target > static_cast<int64_t>(reader->bytes.size())) {
    return -1;
  }
  reader->position = static_cast<size_t>(target);
  return target;
}

struct FormatDeleter {
  void operator()(AVFormatContext* context) const {
    avformat_close_input(&context);
  }
};

struct IoDeleter {
  void operator()(AVIOContext* context) const {
    if (context != nullptr) {
      av_freep(&context->buffer);
    }
    avio_context_free(&context);
  }
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

uint32_t ReadLittleEndian32(const uint8_t* bytes) {
  return uint32_t{bytes[0]} | uint32_t{bytes[1]} << 8 |
         uint32_t{bytes[2]} << 16 | uint32_t{bytes[3]} << 24;
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
  if (parameters.codec_type == AVMEDIA_TYPE_VIDEO) {
    VideoInfo video;
    video.coded_width = parameters.width;
    video.coded_height = parameters.height;
    video.display_width = parameters.width;
    video.display_height = parameters.height;
    video.frame_rate = RationalOf(stream.avg_frame_rate);
    info.format = video;
  } else {
    AudioInfo audio;
    audio.sample_rate = parameters.sample_rate;
    audio.channel_count = parameters.ch_layout.nb_channels;
    info.format = audio;
  }
  return info;
}

size_t BytesPerSample(SampleFormat format) {
  switch (format) {
    case SampleFormat::kU8:
    case SampleFormat::kU8Planar:
      return 1;
    case SampleFormat::kS16:
    case SampleFormat::kS16Planar:
      return 2;
    case SampleFormat::kS32:
    case SampleFormat::kS32Planar:
    case SampleFormat::kF32:
    case SampleFormat::kF32Planar:
      return 4;
    case SampleFormat::kUnknown:
      return 0;
  }
  return 0;
}

bool IsPlanar(SampleFormat format) {
  return format == SampleFormat::kU8Planar ||
         format == SampleFormat::kS16Planar ||
         format == SampleFormat::kS32Planar ||
         format == SampleFormat::kF32Planar;
}

struct PlaneShape {
  size_t width_bytes;
  size_t height;
};

std::array<PlaneShape, 3> ShapeOf(const DecodedVideoFrame& frame) {
  const auto width = static_cast<size_t>(frame.width);
  const auto height = static_cast<size_t>(frame.height);
  const size_t half_width = (width + 1) / 2;
  const size_t half_height = (height + 1) / 2;
  switch (frame.format) {
    case PixelFormat::kI420:
      return {{{width, height},
               {half_width, half_height},
               {half_width, half_height}}};
    case PixelFormat::kI010:
      return {{{width * 2, height},
               {half_width * 2, half_height},
               {half_width * 2, half_height}}};
    case PixelFormat::kNv12:
      return {{{width, height}, {half_width * 2, half_height}, {0, 0}}};
    case PixelFormat::kP010:
      return {{{width * 2, height}, {half_width * 4, half_height}, {0, 0}}};
    case PixelFormat::kUnknown:
    case PixelFormat::kOpaque:
      break;
  }
  return {{{0, 0}, {0, 0}, {0, 0}}};
}

}

std::string FixturePath(std::string_view name) {
  return absl::StrCat(ALUMBRA_DECODE_FIXTURE_DIR, "/", name);
}

absl::StatusOr<std::vector<uint8_t>> ReadFixture(std::string_view name) {
  std::ifstream file(FixturePath(name), std::ios::binary);
  if (!file) {
    return absl::NotFoundError(absl::StrCat("missing fixture ", name));
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>());
}

absl::StatusOr<std::vector<ClipStream>> ReadClip(
    std::span<const uint8_t> bytes) {
  MemoryReader reader{.bytes = bytes, .position = 0};
  auto* buffer = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
  std::unique_ptr<AVIOContext, IoDeleter> io(avio_alloc_context(
      buffer, kIoBufferSize, 0, &reader, ReadMemory, nullptr, SeekMemory));
  if (io == nullptr) {
    av_free(buffer);
    return absl::InternalError("avio_alloc_context failed");
  }
  AVFormatContext* raw = avformat_alloc_context();
  raw->pb = io.get();
  raw->flags |= AVFMT_FLAG_CUSTOM_IO;
  int opened = avformat_open_input(&raw, nullptr, nullptr, nullptr);
  if (opened < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("avformat_open_input: ", AvErrorText(opened)));
  }
  std::unique_ptr<AVFormatContext, FormatDeleter> format(raw);
  opened = avformat_find_stream_info(format.get(), nullptr);
  if (opened < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("avformat_find_stream_info: ", AvErrorText(opened)));
  }
  std::vector<ClipStream> streams(format->nb_streams);
  for (unsigned index = 0; index < format->nb_streams; ++index) {
    streams[index].info = InfoOf(*format->streams[index]);
  }
  AVPacket* packet = av_packet_alloc();
  while (av_read_frame(format.get(), packet) >= 0) {
    ClipStream& stream = streams[static_cast<size_t>(packet->stream_index)];
    const Rational base = stream.info.time_base;
    EncodedSample sample;
    sample.track_id = stream.info.track_id;
    sample.data =
        SharedBytes::Copy({packet->data, static_cast<size_t>(packet->size)});
    sample.pts = TimestampOf(packet->pts, base);
    sample.dts = TimestampOf(packet->dts, base);
    sample.duration = TimestampOf(packet->duration, base);
    sample.key_frame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    size_t side_size = 0;
    const uint8_t* side =
        av_packet_get_side_data(packet, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
    const auto* audio = std::get_if<AudioInfo>(&stream.info.format);
    if (side != nullptr && side_size >= 8 && audio != nullptr &&
        audio->sample_rate > 0) {
      const Rational period =
          Rational::Create(1, audio->sample_rate).value_or(Rational());
      const uint32_t start = ReadLittleEndian32(side);
      const uint32_t end = ReadLittleEndian32(side + 4);
      if (start > 0) {
        sample.trim_start = {.value = start, .time_base = period};
      }
      if (end > 0) {
        sample.trim_end = {.value = end, .time_base = period};
      }
    }
    stream.samples.push_back(std::move(sample));
    av_packet_unref(packet);
  }
  av_packet_free(&packet);
  return streams;
}

absl::StatusOr<ClipStream> ReadFirstStream(std::string_view fixture,
                                           StreamType type) {
  auto bytes = ReadFixture(fixture);
  if (!bytes.ok()) {
    return bytes.status();
  }
  auto streams = ReadClip(*bytes);
  if (!streams.ok()) {
    return streams.status();
  }
  for (ClipStream& stream : *streams) {
    if (stream.info.type() == type) {
      return std::move(stream);
    }
  }
  return absl::NotFoundError("no stream of that type");
}

std::string Md5Hex(std::span<const uint8_t> bytes) {
  std::array<uint8_t, 16> digest{};
  av_md5_sum(digest.data(), bytes.data(), bytes.size());
  std::string text;
  for (const uint8_t byte : digest) {
    absl::StrAppend(&text, absl::Hex(byte, absl::kZeroPad2));
  }
  return text;
}

std::string DigestVideoFrame(const DecodedVideoFrame& frame) {
  std::vector<uint8_t> packed;
  const auto shape = ShapeOf(frame);
  for (size_t plane = 0; plane < shape.size(); ++plane) {
    for (size_t row = 0; row < shape[plane].height; ++row) {
      const uint8_t* source =
          frame.planes[plane].data +
          static_cast<ptrdiff_t>(row) * frame.planes[plane].stride;
      packed.insert(packed.end(), source, source + shape[plane].width_bytes);
    }
  }
  return Md5Hex(packed);
}

std::vector<uint8_t> InterleavedPcm(const DecodedAudioBuffer& buffer) {
  std::vector<uint8_t> pcm;
  const size_t width = BytesPerSample(buffer.format);
  const auto frames = static_cast<size_t>(buffer.frame_count);
  const auto channels = static_cast<size_t>(buffer.channel_count);
  if (!IsPlanar(buffer.format)) {
    pcm.assign(buffer.planes[0], buffer.planes[0] + frames * channels * width);
    return pcm;
  }
  for (size_t frame = 0; frame < frames; ++frame) {
    for (size_t channel = 0; channel < channels; ++channel) {
      const uint8_t* source = buffer.planes[channel] + frame * width;
      pcm.insert(pcm.end(), source, source + width);
    }
  }
  return pcm;
}

std::string DigestAudioBuffer(const DecodedAudioBuffer& buffer) {
  return Md5Hex(InterleavedPcm(buffer));
}

std::vector<std::string> ReadFrameMd5(std::string_view fixture) {
  std::ifstream file(FixturePath(fixture));
  std::vector<std::string> hashes;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const std::vector<std::string_view> fields = absl::StrSplit(line, ',');
    if (fields.size() == 6) {
      std::string hash(fields[5]);
      hash.erase(0, hash.find_first_not_of(' '));
      hashes.push_back(std::move(hash));
    }
  }
  return hashes;
}

absl::StatusOr<ReferenceOutput> ReferenceDecode(const ClipStream& stream) {
  const StreamInfo& info = stream.info;
  AVCodecID id = AV_CODEC_ID_NONE;
  switch (info.codec) {
    case CodecId::kH264:
      id = AV_CODEC_ID_H264;
      break;
    case CodecId::kHevc:
      id = AV_CODEC_ID_HEVC;
      break;
    case CodecId::kVp9:
      id = AV_CODEC_ID_VP9;
      break;
    case CodecId::kAac:
      id = AV_CODEC_ID_AAC;
      break;
    case CodecId::kOpus:
      id = AV_CODEC_ID_OPUS;
      break;
    case CodecId::kAc3:
      id = AV_CODEC_ID_AC3;
      break;
    case CodecId::kEac3:
      id = AV_CODEC_ID_EAC3;
      break;
    case CodecId::kFlac:
      id = AV_CODEC_ID_FLAC;
      break;
    case CodecId::kMp3:
      id = AV_CODEC_ID_MP3;
      break;
    default:
      return absl::UnimplementedError("no reference decoder");
  }
  const AVCodec* codec = avcodec_find_decoder(id);
  if (codec == nullptr) {
    return absl::UnimplementedError("the reference decoder is not built");
  }
  AVCodecContext* context = avcodec_alloc_context3(codec);
  const auto base = Rational::Create(info.time_base.numerator(),
                                     info.time_base.denominator());
  context->pkt_timebase = {static_cast<int>(base->numerator()),
                           static_cast<int>(base->denominator())};
  context->thread_count = 1;
  if (!info.codec_config.empty()) {
    const size_t size = info.codec_config.size();
    auto* extradata =
        static_cast<uint8_t*>(av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE));
    std::memcpy(extradata, info.codec_config.data(), size);
    context->extradata = extradata;
    context->extradata_size = static_cast<int>(size);
  }
  if (const auto* audio = std::get_if<AudioInfo>(&info.format)) {
    context->sample_rate = audio->sample_rate;
    av_channel_layout_default(&context->ch_layout, audio->channel_count);
  }
  int rc = avcodec_open2(context, codec, nullptr);
  if (rc < 0) {
    avcodec_free_context(&context);
    return absl::InternalError(
        absl::StrCat("reference avcodec_open2: ", AvErrorText(rc)));
  }
  ReferenceOutput output;
  AVPacket* packet = av_packet_alloc();
  const auto drain = [&] {
    while (true) {
      AvFramePtr frame = AllocateAvFrame();
      if (avcodec_receive_frame(context, frame.get()) < 0) {
        return;
      }
      if (info.type() == StreamType::kVideo) {
        auto wrapped =
            WrapVideoFrame(std::move(frame), info.time_base, Rational());
        if (wrapped.ok()) {
          output.video.push_back(*std::move(wrapped));
        }
      } else {
        auto wrapped = WrapAudioFrame(std::move(frame), info.time_base,
                                      MediaTime::Unknown());
        if (wrapped.ok()) {
          output.audio.push_back(*std::move(wrapped));
        }
      }
    }
  };
  for (const EncodedSample& sample : stream.samples) {
    av_new_packet(packet, static_cast<int>(sample.data.size()));
    std::memcpy(packet->data, sample.data.data(), sample.data.size());
    packet->pts = sample.pts.value;
    packet->dts = sample.dts.value;
    packet->duration = sample.duration.is_known() ? sample.duration.value : 0;
    if (sample.trim_start.is_known() || sample.trim_end.is_known()) {
      uint8_t* side =
          av_packet_new_side_data(packet, AV_PKT_DATA_SKIP_SAMPLES, 10);
      std::memset(side, 0, 10);
      const auto start = static_cast<uint32_t>(
          sample.trim_start.is_known() ? sample.trim_start.value : 0);
      const auto end = static_cast<uint32_t>(
          sample.trim_end.is_known() ? sample.trim_end.value : 0);
      for (size_t index = 0; index < 4; ++index) {
        side[index] = static_cast<uint8_t>(start >> (8 * index));
        side[4 + index] = static_cast<uint8_t>(end >> (8 * index));
      }
    }
    rc = avcodec_send_packet(context, packet);
    av_packet_unref(packet);
    drain();
  }
  avcodec_send_packet(context, nullptr);
  drain();
  av_packet_free(&packet);
  avcodec_free_context(&context);
  return output;
}

absl::Status EventCollector::OnDecoded(DecoderEvent event) {
  {
    const std::scoped_lock lock(mutex_);
    events_.push_back(std::move(event));
  }
  wake_.notify_all();
  return absl::OkStatus();
}

std::optional<DecoderEvent> EventCollector::Next(
    std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  if (!wake_.wait_for(lock, timeout, [this] { return !events_.empty(); })) {
    return std::nullopt;
  }
  DecoderEvent event = std::move(events_.front());
  events_.pop_front();
  return event;
}

DecodeOutput RunDecode(const DecodeRequest& request) {
  constexpr std::chrono::seconds kTimeout(20);
  DecodeOutput output;
  size_t next = 0;
  size_t writes = 0;
  bool stopping = false;
  bool end_sent = false;
  while (true) {
    auto event = request.events->Next(kTimeout);
    if (!event.has_value()) {
      output.timed_out = true;
      break;
    }
    if (auto* needs = std::get_if<NeedsData>(&event->detail)) {
      if (stopping) {
        break;
      }
      if (next < request.samples.size()) {
        const size_t count =
            std::min(needs->max_samples, request.samples.size() - next);
        auto status = request.decoder->WriteSamples(
            request.ticket, request.samples.subspan(next, count));
        if (!status.ok()) {
          output.errors.push_back({.status = status,
                                   .category = PortErrorCategory::kDecode,
                                   .recovery = RecoveryHint::kFatal});
          break;
        }
        next += count;
        ++writes;
        stopping = request.stop_after_writes != 0 &&
                   writes >= request.stop_after_writes;
      } else if (request.end_stream && !end_sent) {
        end_sent = true;
        auto status = request.decoder->EndOfStream(request.ticket);
        if (!status.ok()) {
          output.errors.push_back({.status = status,
                                   .category = PortErrorCategory::kDecode,
                                   .recovery = RecoveryHint::kFatal});
          break;
        }
      } else {
        break;
      }
    } else if (auto* video = std::get_if<DecodedVideoFrame>(&event->detail)) {
      output.frame_tickets.push_back(event->ticket);
      output.video.push_back(std::move(*video));
    } else if (auto* audio = std::get_if<DecodedAudioBuffer>(&event->detail)) {
      output.frame_tickets.push_back(event->ticket);
      output.audio.push_back(std::move(*audio));
    } else if (auto* error = std::get_if<PortError>(&event->detail)) {
      output.errors.push_back(*error);
      if (error->recovery != RecoveryHint::kTransient) {
        break;
      }
    } else if (std::get_if<StateChanged>(&event->detail) != nullptr) {
      output.end_of_stream = true;
      break;
    }
  }
  return output;
}

DecoderCandidate FindCandidate(ComponentFactory& factory, StreamType type,
                               std::string_view name) {
  const auto candidates = factory.GetDecoderCandidates(type);
  if (candidates.ok()) {
    for (const DecoderCandidate& candidate : *candidates) {
      if (candidate.name == name) {
        return candidate;
      }
    }
  }
  return {};
}

}
