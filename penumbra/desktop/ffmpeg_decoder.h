#ifndef PENUMBRA_DESKTOP_FFMPEG_DECODER_H_
#define PENUMBRA_DESKTOP_FFMPEG_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/ffmpeg_frame.h"
#include "penumbra/desktop/worker_decoder.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

struct AVCodec;
struct AVCodecContext;
struct AVPacket;

namespace penumbra::desktop {

struct AvCodecContextDeleter {
  void operator()(AVCodecContext* context) const;
};

struct AvPacketDeleter {
  void operator()(AVPacket* packet) const;
};

struct FfmpegDecoderOptions {
  int32_t video_threads = 0;
};

class FfmpegDecoder final : public WorkerDecoder {
 public:
  static constexpr int32_t kMaxVideoThreads = 64;

  static absl::StatusOr<std::unique_ptr<Decoder>> Create(
      const AVCodec& codec, const StreamConfig& config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks,
      const FfmpegDecoderOptions& options);

  FfmpegDecoder(StreamInfo info, SeekTicket ticket,
                std::shared_ptr<DecoderCallbacks> callbacks,
                size_t grant_samples);
  FfmpegDecoder(const FfmpegDecoder&) = delete;
  FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;
  ~FfmpegDecoder() override;

 protected:
  absl::Status Decode(const EncodedSample& sample) override;
  absl::Status Drain() override;
  void Reset() override;
  [[nodiscard]] absl::Status CheckConfigChange(
      const StreamInfo& from, const StreamInfo& to) const override;

 private:
  absl::Status Open(const AVCodec& codec, const StreamConfig& config,
                    const FfmpegDecoderOptions& options);
  absl::Status ApplyConfigChange(AVPacket& packet, const EncodedSample& sample);
  [[nodiscard]] absl::Status PreparePacketData(
      const EncodedSample& sample, std::vector<uint8_t>& owned_data,
      std::span<const uint8_t>& data) const;
  absl::Status AttachTrim(AVPacket& packet, const EncodedSample& sample);
  absl::Status ReceiveFrames();
  absl::Status Output(AvFramePtr frame);
  [[nodiscard]] int64_t ToStreamTicks(Timestamp timestamp) const;

  std::unique_ptr<AVCodecContext, AvCodecContextDeleter> context_;
  std::unique_ptr<AVPacket, AvPacketDeleter> packet_;
  AvFramePtr scratch_;
  StreamType type_ = StreamType::kVideo;
  Rational time_base_;
  Rational frame_rate_;
  std::vector<uint8_t> extradata_;
  StreamInfo info_;
  MediaTime next_audio_time_ = MediaTime::Unknown();
};

}

#endif
