#ifndef PENUMBRA_DESKTOP_AUDIO_CONVERTER_H_
#define PENUMBRA_DESKTOP_AUDIO_CONVERTER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

enum class AudioDownmixMode : uint8_t {
  kAuto,
  kStereo,
  kNone,
};

enum class AudioResamplerQuality : uint8_t {
  kLow,
  kMedium,
  kHigh,
};

struct AudioConversionConfig {
  int32_t output_sample_rate = 48000;
  int32_t output_channel_count = 2;
  uint64_t output_channel_mask = 0;
  AudioDownmixMode downmix_mode = AudioDownmixMode::kAuto;
  AudioResamplerQuality quality = AudioResamplerQuality::kHigh;
  float volume = 1.0F;
  bool muted = false;
  bool preserves_pitch = true;
  double minimum_audible_rate = 0.25;
  double maximum_audible_rate = 4.0;
  int32_t gain_ramp_frames = 128;
};

struct ConvertedAudio {
  MediaTime presentation_time = MediaTime::Unknown();
  int32_t frame_count = 0;
  int32_t sample_rate = 0;
  int32_t channel_count = 0;
  std::vector<float> samples;
};

class AudioConverter final {
 public:
  static absl::StatusOr<std::unique_ptr<AudioConverter>> Create(
      AudioConversionConfig config);

  ~AudioConverter();
  AudioConverter(const AudioConverter&) = delete;
  AudioConverter& operator=(const AudioConverter&) = delete;

  absl::StatusOr<ConvertedAudio> Convert(const DecodedAudioBuffer& input,
                                         uint64_t input_channel_mask);
  absl::StatusOr<ConvertedAudio> Drain();
  absl::Status SetVolume(float volume);
  void SetMuted(bool muted);
  absl::Status SetTimeStretchRatio(double ratio);
  void SetPreservesPitch(bool preserves_pitch);
  absl::Status SetDisplayResampleRatio(double ratio);
  void Reset();

 private:
  struct State;
  explicit AudioConverter(AudioConversionConfig config);
  absl::Status ValidateInput(const DecodedAudioBuffer& input) const;
  absl::Status ConfigureResampler(const DecodedAudioBuffer& input,
                                  uint64_t input_channel_mask);
  void ApplyGain(ConvertedAudio& output);
  void ApplyGainRamp(ConvertedAudio& output);
  void SmoothOutputBoundary(ConvertedAudio& output);
  std::unique_ptr<State> state_;
};

}

#endif
