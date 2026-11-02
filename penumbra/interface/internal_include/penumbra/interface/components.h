#ifndef PENUMBRA_INTERFACE_COMPONENTS_H_
#define PENUMBRA_INTERFACE_COMPONENTS_H_

#include <memory>
#include <optional>
#include <span>
#include <variant>

#include "penumbra/interface/player.h"

namespace penumbra {

struct DecoderEvent {
  SeekTicket ticket = 0;
  std::variant<NeedsData, DecodedVideoFrame, DecodedAudioBuffer, StateChanged,
               PortError>
      detail;
};

class DecoderCallbacks {
 public:
  virtual ~DecoderCallbacks() = default;
  virtual absl::Status OnDecoded(DecoderEvent event) = 0;
};

class Decoder {
 public:
  virtual ~Decoder() = default;
  virtual absl::Status WriteSamples(SeekTicket ticket,
                                    std::span<const EncodedSample> samples) = 0;
  virtual absl::Status EndOfStream(SeekTicket ticket) = 0;
  virtual absl::Status Flush(SeekTicket ticket) = 0;
  virtual absl::Status Stop() = 0;
};

class VideoFrameSource {
 public:
  virtual ~VideoFrameSource() = default;
  virtual std::optional<DecodedVideoFrame> TakeDueFrame(
      MediaTime time) noexcept = 0;
};

class VideoSink {
 public:
  virtual ~VideoSink() = default;
  virtual absl::Status Start(std::shared_ptr<VideoFrameSource> frames) = 0;
  virtual absl::Status SetBounds(OutputBounds bounds) = 0;
  virtual absl::Status Stop() = 0;
};

class AudioSource {
 public:
  virtual ~AudioSource() = default;
  virtual size_t Read(std::span<uint8_t> destination) noexcept = 0;
};

class AudioSink {
 public:
  virtual ~AudioSink() = default;
  virtual absl::Status Start(AudioInfo format,
                             std::shared_ptr<AudioSource> audio) = 0;
  [[nodiscard]] virtual absl::StatusOr<TimeDelta> GetOutputLatency() const = 0;
  virtual absl::Status Stop() = 0;
};

class ComponentFactory {
 public:
  virtual ~ComponentFactory() = default;
  virtual absl::StatusOr<std::unique_ptr<Decoder>> CreateVideoDecoder(
      StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) = 0;
  virtual absl::StatusOr<std::unique_ptr<Decoder>> CreateAudioDecoder(
      StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) = 0;
  virtual absl::StatusOr<std::unique_ptr<VideoSink>> CreateVideoSink(
      OutputMode mode) = 0;
  virtual absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() = 0;
};

}

#endif
