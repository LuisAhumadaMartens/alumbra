#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "fuzz_target.h"
#include "penumbra/desktop/ffmpeg_decoder.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/codec_id.h"
}

namespace {

class FuzzCallbacks final : public penumbra::DecoderCallbacks {
 public:
  absl::Status OnDecoded(penumbra::DecoderEvent event) override {
    std::lock_guard lock(mutex_);
    if (std::holds_alternative<penumbra::NeedsData>(event.detail)) {
      ++grants_;
    } else if (std::holds_alternative<penumbra::StateChanged>(event.detail)) {
      finished_ = true;
    } else if (const auto* error =
                   std::get_if<penumbra::PortError>(&event.detail);
               error != nullptr &&
               error->recovery != penumbra::RecoveryHint::kTransient) {
      finished_ = true;
    }
    wake_.notify_all();
    return absl::OkStatus();
  }

  void WaitForGrantOrFinish(size_t previous_grants) {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this, previous_grants] {
      return grants_ > previous_grants || finished_;
    });
  }

  [[nodiscard]] size_t grants() const {
    std::lock_guard lock(mutex_);
    return grants_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  size_t grants_ = 0;
  bool finished_ = false;
};

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const bool hevc = (data[0] & 1U) != 0;
  const AVCodecID av_codec = hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
  const penumbra::CodecId codec =
      hevc ? penumbra::CodecId::kHevc : penumbra::CodecId::kH264;
  const AVCodec* decoder = avcodec_find_decoder(av_codec);
  if (decoder == nullptr) {
    return 0;
  }

  penumbra::StreamInfo info;
  info.track_id = 1;
  info.codec = codec;
  info.nal_unit_framing = penumbra::NalUnitFraming::kAnnexB;
  info.time_base = penumbra::Rational::Of(1, 90000);
  penumbra::VideoInfo video;
  video.coded_width = 16;
  video.coded_height = 16;
  video.display_width = 16;
  video.display_height = 16;
  video.frame_rate = penumbra::Rational::Of(30, 1);
  info.format = video;
  const penumbra::StreamConfig config{
      .info = std::move(info),
      .encryption_scheme = std::nullopt,
      .output_mode = penumbra::OutputMode::kTexture};
  const auto callbacks = std::make_shared<FuzzCallbacks>();
  auto created = penumbra::desktop::FfmpegDecoder::Create(*decoder, config, 1,
                                                          callbacks, {});
  if (!created.ok()) {
    return 0;
  }
  std::unique_ptr<penumbra::Decoder> instance = std::move(*created);
  callbacks->WaitForGrantOrFinish(0);
  if (callbacks->grants() == 0) {
    static_cast<void>(instance->Stop());
    return 0;
  }

  const std::span<const uint8_t> bytes(data + 1, size - 1);
  penumbra::EncodedSample sample{};
  sample.track_id = 1;
  sample.key_frame = true;
  const size_t sample_size = std::min<size_t>(bytes.size(), 1024 * 1024);
  sample.data = penumbra::SharedBytes::Copy(
      std::vector<uint8_t>(bytes.begin(), bytes.begin() + sample_size));
  const size_t first_grants = callbacks->grants();
  const absl::Status write_status =
      instance->WriteSamples(1, std::span(&sample, 1));
  if (!write_status.ok()) {
    static_cast<void>(instance->Stop());
    return 0;
  }
  callbacks->WaitForGrantOrFinish(first_grants);
  if (callbacks->grants() > first_grants) {
    const size_t drain_grants = callbacks->grants();
    const absl::Status end_status = instance->EndOfStream(1);
    if (!end_status.ok()) {
      static_cast<void>(instance->Stop());
      return 0;
    }
    callbacks->WaitForGrantOrFinish(drain_grants);
  }
  static_cast<void>(instance->Stop());
  return 0;
}
