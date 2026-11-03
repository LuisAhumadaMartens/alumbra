#include "penumbra/desktop/audio_converter.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"
}

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

constexpr int32_t kMaxAudioChannels = 8;

struct SwrDeleter {
  void operator()(SwrContext* context) const { swr_free(&context); }
};

using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;

AVSampleFormat ToAvFormat(SampleFormat format) {
  switch (format) {
    case SampleFormat::kU8:
      return AV_SAMPLE_FMT_U8;
    case SampleFormat::kS16:
      return AV_SAMPLE_FMT_S16;
    case SampleFormat::kS32:
      return AV_SAMPLE_FMT_S32;
    case SampleFormat::kF32:
      return AV_SAMPLE_FMT_FLT;
    case SampleFormat::kU8Planar:
      return AV_SAMPLE_FMT_U8P;
    case SampleFormat::kS16Planar:
      return AV_SAMPLE_FMT_S16P;
    case SampleFormat::kS32Planar:
      return AV_SAMPLE_FMT_S32P;
    case SampleFormat::kF32Planar:
      return AV_SAMPLE_FMT_FLTP;
    case SampleFormat::kUnknown:
      return AV_SAMPLE_FMT_NONE;
  }
  return AV_SAMPLE_FMT_NONE;
}

AVChannel ToAvChannel(AudioChannel channel) {
  switch (channel) {
    case AudioChannel::kFrontLeft:
      return AV_CHAN_FRONT_LEFT;
    case AudioChannel::kFrontRight:
      return AV_CHAN_FRONT_RIGHT;
    case AudioChannel::kFrontCenter:
      return AV_CHAN_FRONT_CENTER;
    case AudioChannel::kLowFrequency:
      return AV_CHAN_LOW_FREQUENCY;
    case AudioChannel::kBackLeft:
      return AV_CHAN_BACK_LEFT;
    case AudioChannel::kBackRight:
      return AV_CHAN_BACK_RIGHT;
    case AudioChannel::kFrontLeftOfCenter:
      return AV_CHAN_FRONT_LEFT_OF_CENTER;
    case AudioChannel::kFrontRightOfCenter:
      return AV_CHAN_FRONT_RIGHT_OF_CENTER;
    case AudioChannel::kBackCenter:
      return AV_CHAN_BACK_CENTER;
    case AudioChannel::kSideLeft:
      return AV_CHAN_SIDE_LEFT;
    case AudioChannel::kSideRight:
      return AV_CHAN_SIDE_RIGHT;
    case AudioChannel::kTopCenter:
      return AV_CHAN_TOP_CENTER;
    case AudioChannel::kTopFrontLeft:
      return AV_CHAN_TOP_FRONT_LEFT;
    case AudioChannel::kTopFrontCenter:
      return AV_CHAN_TOP_FRONT_CENTER;
    case AudioChannel::kTopFrontRight:
      return AV_CHAN_TOP_FRONT_RIGHT;
    case AudioChannel::kTopBackLeft:
      return AV_CHAN_TOP_BACK_LEFT;
    case AudioChannel::kTopBackCenter:
      return AV_CHAN_TOP_BACK_CENTER;
    case AudioChannel::kTopBackRight:
      return AV_CHAN_TOP_BACK_RIGHT;
    case AudioChannel::kWideLeft:
      return AV_CHAN_WIDE_LEFT;
    case AudioChannel::kWideRight:
      return AV_CHAN_WIDE_RIGHT;
    case AudioChannel::kSurroundDirectLeft:
      return AV_CHAN_SURROUND_DIRECT_LEFT;
    case AudioChannel::kSurroundDirectRight:
      return AV_CHAN_SURROUND_DIRECT_RIGHT;
    case AudioChannel::kLowFrequency2:
      return AV_CHAN_LOW_FREQUENCY_2;
    case AudioChannel::kTopSideLeft:
      return AV_CHAN_TOP_SIDE_LEFT;
    case AudioChannel::kTopSideRight:
      return AV_CHAN_TOP_SIDE_RIGHT;
    case AudioChannel::kBottomFrontCenter:
      return AV_CHAN_BOTTOM_FRONT_CENTER;
    case AudioChannel::kBottomFrontLeft:
      return AV_CHAN_BOTTOM_FRONT_LEFT;
    case AudioChannel::kBottomFrontRight:
      return AV_CHAN_BOTTOM_FRONT_RIGHT;
    case AudioChannel::kUnknown:
      return AV_CHAN_NONE;
  }
  return AV_CHAN_NONE;
}

absl::StatusOr<AVChannelLayout> MakeLayout(
    int32_t channels, uint64_t mask,
    const std::array<AudioChannel, DecodedAudioBuffer::kMaxPlanes>* order =
        nullptr) {
  AVChannelLayout layout{};
  bool has_order = false;
  if (order != nullptr) {
    for (int32_t channel = 0; channel < channels; ++channel) {
      has_order = has_order || order->at(static_cast<size_t>(channel)) !=
                                   AudioChannel::kUnknown;
    }
  }
  if (has_order && mask == 0) {
    std::string description;
    for (int32_t channel = 0; channel < channels; ++channel) {
      const AVChannel id = ToAvChannel(order->at(static_cast<size_t>(channel)));
      if (id == AV_CHAN_NONE) {
        return absl::InvalidArgumentError(
            "Audio channel order contains an unknown channel");
      }
      std::array<char, 64> channel_name{};
      if (av_channel_name(channel_name.data(), channel_name.size(), id) < 0) {
        return absl::InvalidArgumentError(
            "Audio channel order contains an unknown channel");
      }
      if (!description.empty()) {
        description.push_back('+');
      }
      description.append(channel_name.data());
    }
    if (av_channel_layout_from_string(&layout, description.c_str()) < 0) {
      return absl::InvalidArgumentError("Audio channel order is unsupported");
    }
  } else if (mask != 0) {
    const int result = av_channel_layout_from_mask(&layout, mask);
    if (result < 0 || layout.nb_channels != channels) {
      av_channel_layout_uninit(&layout);
      return absl::InvalidArgumentError(
          "Audio channel mask does not match channel count");
    }
  } else {
    av_channel_layout_default(&layout, channels);
  }
  if (layout.nb_channels != channels) {
    av_channel_layout_uninit(&layout);
    return absl::InvalidArgumentError("Audio channel layout is unsupported");
  }
  return layout;
}

std::pair<double, double> MatrixCoefficients(AVChannel channel, double center,
                                             double surround, double lfe) {
  switch (channel) {
    case AV_CHAN_FRONT_LEFT:
      return {1.0, 0.0};
    case AV_CHAN_FRONT_RIGHT:
      return {0.0, 1.0};
    case AV_CHAN_FRONT_LEFT_OF_CENTER:
      return {center, 0.0};
    case AV_CHAN_FRONT_RIGHT_OF_CENTER:
      return {0.0, center};
    case AV_CHAN_SIDE_LEFT:
    case AV_CHAN_BACK_LEFT:
      return {surround, 0.0};
    case AV_CHAN_SIDE_RIGHT:
    case AV_CHAN_BACK_RIGHT:
      return {0.0, surround};
    case AV_CHAN_FRONT_CENTER:
      return {center, center};
    case AV_CHAN_BACK_CENTER:
      return {surround * 0.7071067811865476, surround * 0.7071067811865476};
    case AV_CHAN_LOW_FREQUENCY:
    case AV_CHAN_LOW_FREQUENCY_2:
      return {lfe, lfe};
    default:
      return {0.0, 0.0};
  }
}

double SafeCoefficient(float value, double fallback) {
  if (std::isfinite(value) && value >= 0.0F && value <= 1.0F) {
    return static_cast<double>(value);
  }
  return fallback;
}

int FilterSize(AudioResamplerQuality quality) {
  switch (quality) {
    case AudioResamplerQuality::kLow:
      return 16;
    case AudioResamplerQuality::kMedium:
      return 32;
    case AudioResamplerQuality::kHigh:
      return 64;
  }
  return 64;
}

int PhaseShift(AudioResamplerQuality quality) {
  return quality == AudioResamplerQuality::kLow ? 6 : 10;
}

absl::Status SetDownmixMatrix(SwrContext* context, const AVChannelLayout& input,
                              const AVChannelLayout& output,
                              AudioDownmixMode mode,
                              const DecodedAudioBuffer& buffer) {
  if (output.nb_channels != 2 || input.nb_channels <= 2 ||
      mode == AudioDownmixMode::kNone) {
    return absl::OkStatus();
  }
  const double center =
      buffer.has_downmix_metadata
          ? SafeCoefficient(buffer.center_mix_level, 0.7071067811865476)
          : 0.7071067811865476;
  const double surround =
      buffer.has_downmix_metadata
          ? SafeCoefficient(buffer.surround_mix_level, 0.7071067811865476)
          : 0.7071067811865476;
  const double lfe = buffer.has_downmix_metadata
                         ? SafeCoefficient(buffer.lfe_mix_level, 0.0)
                         : 0.0;
  std::vector<double> matrix(static_cast<size_t>(input.nb_channels) * 2U, 0.0);
  for (int channel = 0; channel < input.nb_channels; ++channel) {
    const AVChannel id = av_channel_layout_channel_from_index(&input, channel);
    const auto [left, right] = MatrixCoefficients(id, center, surround, lfe);
    matrix[static_cast<size_t>(channel)] = left;
    matrix[static_cast<size_t>(input.nb_channels) +
           static_cast<size_t>(channel)] = right;
  }
  if (swr_set_matrix(context, matrix.data(), input.nb_channels) < 0) {
    return absl::InternalError("Could not set audio downmix matrix");
  }
  return absl::OkStatus();
}

}

struct AudioConverter::State {
  explicit State(AudioConversionConfig value) : config(value) {}

  AudioConversionConfig config;
  SwrPtr resampler;
  AVChannelLayout input_layout{};
  AVSampleFormat input_format = AV_SAMPLE_FMT_NONE;
  int32_t input_rate = 0;
  double time_stretch_ratio = 1.0;
  MediaTime time_anchor = MediaTime::Unknown();
  int64_t total_output_frames = 0;
  MediaTime next_time = MediaTime::Unknown();
  float current_gain = 1.0F;
  float target_gain = 1.0F;
  int32_t ramp_remaining = 0;
};

AudioConverter::AudioConverter(AudioConversionConfig config)
    : state_(std::make_unique<State>(config)) {
  state_->current_gain = config.muted ? 0.0F : config.volume;
  state_->target_gain = state_->current_gain;
}

AudioConverter::~AudioConverter() {
  av_channel_layout_uninit(&state_->input_layout);
}

absl::StatusOr<std::unique_ptr<AudioConverter>> AudioConverter::Create(
    AudioConversionConfig config) {
  if (config.output_sample_rate <= 0 || config.output_channel_count <= 0 ||
      config.output_channel_count > 8 || !std::isfinite(config.volume) ||
      config.volume < 0.0F || config.volume > 1.0F ||
      config.gain_ramp_frames < 0) {
    return absl::InvalidArgumentError(
        "Audio conversion configuration is invalid");
  }
  if (config.downmix_mode == AudioDownmixMode::kStereo) {
    config.output_channel_count = 2;
    config.output_channel_mask = 0;
  }
  return std::unique_ptr<AudioConverter>(new AudioConverter(config));
}

absl::Status AudioConverter::ValidateInput(
    const DecodedAudioBuffer& input) const {
  const AVSampleFormat input_format = ToAvFormat(input.format);
  if (input.sample_rate <= 0 || input.channel_count <= 0 ||
      input.channel_count > kMaxAudioChannels || input.frame_count < 0 ||
      (input.frame_count > 0 && input_format == AV_SAMPLE_FMT_NONE)) {
    return absl::InvalidArgumentError("Decoded audio format is invalid");
  }
  if (state_->config.downmix_mode == AudioDownmixMode::kNone &&
      input.channel_count != state_->config.output_channel_count) {
    return absl::InvalidArgumentError(
        "Audio downmix is disabled for differing channel counts");
  }
  return absl::OkStatus();
}

absl::Status AudioConverter::ConfigureResampler(const DecodedAudioBuffer& input,
                                                uint64_t input_channel_mask) {
  const AVSampleFormat input_format = ToAvFormat(input.format);
  auto input_layout = MakeLayout(input.channel_count, input_channel_mask,
                                 &input.channel_layout);
  if (!input_layout.ok()) {
    return input_layout.status();
  }
  SwrContext* raw = nullptr;
  auto output_layout = MakeLayout(state_->config.output_channel_count,
                                  state_->config.output_channel_mask);
  if (!output_layout.ok()) {
    av_channel_layout_uninit(&*input_layout);
    return output_layout.status();
  }
  const int allocated = swr_alloc_set_opts2(
      &raw, &*output_layout, AV_SAMPLE_FMT_FLT,
      state_->config.output_sample_rate, &*input_layout, input_format,
      input.sample_rate, SWR_FLAG_RESAMPLE, nullptr);
  if (allocated < 0 || raw == nullptr) {
    av_channel_layout_uninit(&*output_layout);
    av_channel_layout_uninit(&*input_layout);
    return absl::InternalError("Could not allocate audio resampler");
  }
  SwrPtr replacement(raw);
  av_opt_set_int(replacement.get(), "filter_size",
                 FilterSize(state_->config.quality), 0);
  av_opt_set_int(replacement.get(), "phase_shift",
                 PhaseShift(state_->config.quality), 0);
  auto matrix_status =
      SetDownmixMatrix(replacement.get(), *input_layout, *output_layout,
                       state_->config.downmix_mode, input);
  if (!matrix_status.ok()) {
    av_channel_layout_uninit(&*output_layout);
    av_channel_layout_uninit(&*input_layout);
    return matrix_status;
  }
  av_channel_layout_uninit(&*output_layout);
  if (swr_init(replacement.get()) < 0) {
    av_channel_layout_uninit(&*input_layout);
    return absl::InvalidArgumentError("Could not initialise audio resampler");
  }
  const int compensation_distance = state_->config.output_sample_rate;
  const int compensation_delta = static_cast<int>(
      std::llround((state_->time_stretch_ratio - 1.0) * compensation_distance));
  if (compensation_delta != 0 &&
      swr_set_compensation(replacement.get(), compensation_delta,
                           compensation_distance) < 0) {
    av_channel_layout_uninit(&*input_layout);
    return absl::InternalError("Could not set audio time stretch ratio");
  }
  state_->resampler = std::move(replacement);
  av_channel_layout_uninit(&state_->input_layout);
  if (av_channel_layout_copy(&state_->input_layout, &*input_layout) < 0) {
    av_channel_layout_uninit(&*input_layout);
    return absl::InternalError("Could not retain input channel layout");
  }
  av_channel_layout_uninit(&*input_layout);
  state_->input_rate = input.sample_rate;
  state_->input_format = input_format;
  state_->time_anchor = input.presentation_time;
  state_->total_output_frames = 0;
  state_->next_time = input.presentation_time;
  return absl::OkStatus();
}

void AudioConverter::ApplyGain(ConvertedAudio& output) {
  state_->target_gain = state_->config.muted ? 0.0F : state_->config.volume;
  if (state_->ramp_remaining == 0 &&
      state_->current_gain != state_->target_gain) {
    state_->ramp_remaining = state_->config.gain_ramp_frames;
    if (state_->ramp_remaining == 0) {
      state_->current_gain = state_->target_gain;
    }
  }
  for (int32_t frame = 0; frame < output.frame_count; ++frame) {
    if (state_->ramp_remaining > 0) {
      state_->current_gain += (state_->target_gain - state_->current_gain) /
                              static_cast<float>(state_->ramp_remaining);
      --state_->ramp_remaining;
    }
    for (int32_t channel = 0; channel < output.channel_count; ++channel) {
      const size_t index = (static_cast<size_t>(frame) *
                            static_cast<size_t>(output.channel_count)) +
                           static_cast<size_t>(channel);
      output.samples.at(index) *= state_->current_gain;
    }
  }
}

absl::StatusOr<ConvertedAudio> AudioConverter::Convert(
    const DecodedAudioBuffer& input, uint64_t input_channel_mask) {
  auto validation = ValidateInput(input);
  if (!validation.ok()) {
    return validation;
  }
  const AVSampleFormat input_format = ToAvFormat(input.format);
  if (input.frame_count == 0) {
    return ConvertedAudio{.presentation_time = input.presentation_time,
                          .frame_count = 0,
                          .sample_rate = state_->config.output_sample_rate,
                          .channel_count = state_->config.output_channel_count,
                          .samples = {}};
  }
  auto input_layout = MakeLayout(input.channel_count, input_channel_mask,
                                 &input.channel_layout);
  if (!input_layout.ok()) {
    return input_layout.status();
  }
  if (state_->resampler == nullptr || state_->input_rate != input.sample_rate ||
      state_->input_format != input_format ||
      av_channel_layout_compare(&state_->input_layout, &*input_layout) != 0) {
    av_channel_layout_uninit(&*input_layout);
    auto status = ConfigureResampler(input, input_channel_mask);
    if (!status.ok()) {
      return status;
    }
    input_layout = MakeLayout(input.channel_count, input_channel_mask,
                              &input.channel_layout);
    if (!input_layout.ok()) {
      return input_layout.status();
    }
  }
  av_channel_layout_uninit(&*input_layout);
  if (state_->next_time.is_unknown() && !input.presentation_time.is_unknown()) {
    const int64_t delay =
        swr_get_delay(state_->resampler.get(), input.sample_rate);
    const int64_t delay_us =
        av_rescale_rnd(delay, 1'000'000, input.sample_rate, AV_ROUND_NEAR_INF);
    state_->next_time =
        input.presentation_time - TimeDelta::FromMicroseconds(delay_us);
    state_->time_anchor = state_->next_time;
    state_->total_output_frames = 0;
  }
  const int64_t delay =
      swr_get_delay(state_->resampler.get(), input.sample_rate);
  const int64_t capacity = av_rescale_rnd(delay + input.frame_count,
                                          state_->config.output_sample_rate,
                                          input.sample_rate, AV_ROUND_UP);
  if (capacity < 0 || capacity > std::numeric_limits<int32_t>::max()) {
    return absl::ResourceExhaustedError("Audio conversion buffer is too large");
  }
  ConvertedAudio output{
      .presentation_time = state_->next_time,
      .frame_count = 0,
      .sample_rate = state_->config.output_sample_rate,
      .channel_count = state_->config.output_channel_count,
      .samples = std::vector<float>(
          static_cast<size_t>(capacity) *
          static_cast<size_t>(state_->config.output_channel_count))};
  std::array<const uint8_t*, DecodedAudioBuffer::kMaxPlanes> input_planes{};
  const bool planar = av_sample_fmt_is_planar(input_format) != 0;
  const size_t plane_count =
      planar ? static_cast<size_t>(input.channel_count) : 1U;
  for (size_t plane = 0; plane < plane_count; ++plane) {
    if (input.planes.at(plane) == nullptr) {
      return absl::InvalidArgumentError("Decoded audio plane is missing");
    }
    input_planes.at(plane) = input.planes.at(plane);
  }
  std::array<uint8_t*, 1> output_planes = {
      reinterpret_cast<uint8_t*>(output.samples.data())};
  const int converted = swr_convert(
      state_->resampler.get(), output_planes.data(), static_cast<int>(capacity),
      input_planes.data(), input.frame_count);
  if (converted < 0) {
    return absl::InternalError("Audio resampling failed");
  }
  output.frame_count = converted;
  output.samples.resize(static_cast<size_t>(converted) *
                        static_cast<size_t>(output.channel_count));
  ApplyGain(output);
  state_->total_output_frames += converted;
  const int64_t output_duration =
      av_rescale_q(state_->total_output_frames,
                   AVRational{1, output.sample_rate}, AVRational{1, 1'000'000});
  state_->next_time =
      state_->time_anchor + TimeDelta::FromMicroseconds(output_duration);
  return output;
}

absl::StatusOr<ConvertedAudio> AudioConverter::Drain() {
  ConvertedAudio output{.presentation_time = state_->next_time,
                        .frame_count = 0,
                        .sample_rate = state_->config.output_sample_rate,
                        .channel_count = state_->config.output_channel_count,
                        .samples = {}};
  if (state_->resampler == nullptr) {
    return output;
  }
  const int64_t delay =
      swr_get_delay(state_->resampler.get(), state_->config.output_sample_rate);
  if (delay <= 0) {
    return output;
  }
  if (delay > std::numeric_limits<int32_t>::max()) {
    return absl::ResourceExhaustedError("Audio drain buffer is too large");
  }
  output.samples.resize(static_cast<size_t>(delay) *
                        static_cast<size_t>(output.channel_count));
  std::array<uint8_t*, 1> output_planes = {
      {reinterpret_cast<uint8_t*>(output.samples.data())}};
  const int converted =
      swr_convert(state_->resampler.get(), output_planes.data(),
                  static_cast<int>(delay), nullptr, 0);
  if (converted < 0) {
    return absl::InternalError("Audio resampler drain failed");
  }
  output.frame_count = converted;
  output.samples.resize(static_cast<size_t>(converted) *
                        static_cast<size_t>(output.channel_count));
  state_->target_gain = state_->config.muted ? 0.0F : state_->config.volume;
  if (state_->ramp_remaining == 0 &&
      state_->current_gain != state_->target_gain) {
    state_->ramp_remaining = state_->config.gain_ramp_frames;
    if (state_->ramp_remaining == 0) {
      state_->current_gain = state_->target_gain;
    }
  }
  for (int32_t frame = 0; frame < converted; ++frame) {
    if (state_->ramp_remaining > 0) {
      state_->current_gain += (state_->target_gain - state_->current_gain) /
                              static_cast<float>(state_->ramp_remaining);
      --state_->ramp_remaining;
    }
    for (int32_t channel = 0; channel < output.channel_count; ++channel) {
      const size_t index = (static_cast<size_t>(frame) *
                            static_cast<size_t>(output.channel_count)) +
                           static_cast<size_t>(channel);
      output.samples[index] *= state_->current_gain;
    }
  }
  state_->total_output_frames += converted;
  const int64_t output_duration =
      av_rescale_q(state_->total_output_frames,
                   AVRational{1, state_->config.output_sample_rate},
                   AVRational{1, 1'000'000});
  state_->next_time =
      state_->time_anchor + TimeDelta::FromMicroseconds(output_duration);
  return output;
}

absl::Status AudioConverter::SetVolume(float volume) {
  if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
    return absl::InvalidArgumentError(
        "Audio volume must be in the range 0 to 1");
  }
  state_->config.volume = volume;
  return absl::OkStatus();
}

void AudioConverter::SetMuted(bool muted) { state_->config.muted = muted; }

absl::Status AudioConverter::SetTimeStretchRatio(double ratio) {
  if (!std::isfinite(ratio) || ratio < 0.25 || ratio > 4.0) {
    return absl::InvalidArgumentError(
        "Audio time stretch ratio is out of range");
  }
  state_->time_stretch_ratio = ratio;
  if (state_->resampler == nullptr) {
    return absl::OkStatus();
  }
  const int distance = state_->config.output_sample_rate;
  const int delta = static_cast<int>(
      std::llround((ratio - 1.0) * static_cast<double>(distance)));
  if (swr_set_compensation(state_->resampler.get(), delta, distance) < 0) {
    return absl::InternalError("Could not set audio time stretch ratio");
  }
  return absl::OkStatus();
}

void AudioConverter::Reset() {
  if (state_->resampler != nullptr) {
    swr_close(state_->resampler.get());
    swr_init(state_->resampler.get());
  }
  state_->next_time = MediaTime::Unknown();
  state_->time_anchor = MediaTime::Unknown();
  state_->total_output_frames = 0;
  state_->current_gain = state_->config.muted ? 0.0F : state_->config.volume;
  state_->target_gain = state_->current_gain;
  state_->ramp_remaining = 0;
}

}
