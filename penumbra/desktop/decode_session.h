#ifndef PENUMBRA_DESKTOP_DECODE_SESSION_H_
#define PENUMBRA_DESKTOP_DECODE_SESSION_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "penumbra/desktop/decode_test_util.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop::decode_test {

inline StreamConfig ConfigOf(const ClipStream& stream) {
  return {.info = stream.info,
          .encryption_scheme = std::nullopt,
          .output_mode = OutputMode::kTexture};
}

class Session {
 public:
  Session(std::string_view decoder_name, const ClipStream& stream,
          const DesktopDecoderOptions& options = {}, SeekTicket ticket = 1)
      : factory_(options), events_(std::make_shared<EventCollector>()) {
    auto created = factory_.CreateDecoder(
        FindCandidate(factory_, stream.info.type(), decoder_name),
        ConfigOf(stream), ticket, events_);
    status_ = created.status();
    if (created.ok()) {
      decoder_ = *std::move(created);
    }
  }

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  ~Session() {
    if (decoder_ != nullptr) {
      EXPECT_TRUE(decoder_->Stop().ok());
    }
  }

  [[nodiscard]] DecodeOutput Run(std::span<const EncodedSample> samples,
                                 SeekTicket ticket = 1, bool end_stream = true,
                                 size_t stop_after_writes = 0) const {
    return RunDecode({.decoder = decoder_.get(),
                      .events = events_.get(),
                      .ticket = ticket,
                      .samples = samples,
                      .end_stream = end_stream,
                      .stop_after_writes = stop_after_writes});
  }

  [[nodiscard]] const absl::Status& Status() const { return status_; }
  [[nodiscard]] Decoder& Underlying() const { return *decoder_; }
  [[nodiscard]] EventCollector& Events() const { return *events_; }

 private:
  DesktopComponentFactory factory_;
  std::shared_ptr<EventCollector> events_;
  std::unique_ptr<Decoder> decoder_;
  absl::Status status_;
};

inline ClipStream Load(std::string_view file, StreamType type) {
  auto stream = ReadFirstStream(file, type);
  EXPECT_TRUE(stream.ok()) << stream.status();
  return stream.ok() ? *std::move(stream) : ClipStream{};
}

inline std::vector<std::string> Digests(
    const std::vector<DecodedVideoFrame>& frames) {
  std::vector<std::string> digests;
  digests.reserve(frames.size());
  for (const auto& frame : frames) {
    digests.push_back(DigestVideoFrame(frame));
  }
  return digests;
}

inline std::vector<std::string> Digests(
    const std::vector<DecodedAudioBuffer>& audio) {
  std::vector<std::string> digests;
  digests.reserve(audio.size());
  for (const auto& buffer : audio) {
    digests.push_back(DigestAudioBuffer(buffer));
  }
  return digests;
}

}

#endif
