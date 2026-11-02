#include "penumbra/desktop/dav1d_decoder.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/desktop/decode_errors.h"
#include "penumbra/desktop/decoder_config.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "dav1d/common.h"
#include "dav1d/data.h"
#include "dav1d/dav1d.h"
#include "dav1d/headers.h"
#include "dav1d/picture.h"
}

namespace penumbra::desktop {
namespace {

constexpr size_t kGrantSamples = 8;
constexpr int kMaxFeedAttempts = 4;

struct PictureOwner {
  PictureOwner() = default;
  PictureOwner(const PictureOwner&) = delete;
  PictureOwner& operator=(const PictureOwner&) = delete;
  ~PictureOwner() { dav1d_picture_unref(&picture); }

  Dav1dPicture picture{};
};

void ReleaseBytes(const uint8_t* buffer, void* cookie) {
  (void)buffer;
  const std::unique_ptr<SharedBytes> owner(static_cast<SharedBytes*>(cookie));
}

absl::Status StatusFromDav1dError(int code, std::string_view operation) {
  const std::string text = absl::StrCat(operation, " failed with ", code);
  if (code == DAV1D_ERR(EINVAL)) {
    return CorruptSampleError(text);
  }
  if (code == DAV1D_ERR(ENOMEM)) {
    return absl::ResourceExhaustedError(text);
  }
  if (code == DAV1D_ERR(ENOPROTOOPT) || code == DAV1D_ERR(ENOTSUP)) {
    return CodecUnsupportedError(text);
  }
  return DecodeFailedError(text);
}

MediaTime TimeOf(int64_t value, Rational time_base) {
  if (value == Timestamp::kUnknown) {
    return MediaTime::Unknown();
  }
  return ToMediaTime(Timestamp{.value = value, .time_base = time_base},
                     Rounding::kNearest)
      .value_or(MediaTime::Unknown());
}

TimeDelta DurationOf(int64_t value, Rational time_base) {
  if (value <= 0) {
    return TimeDelta::Unknown();
  }
  return ToTimeDelta(Timestamp{.value = value, .time_base = time_base},
                     Rounding::kNearest)
      .value_or(TimeDelta::Unknown());
}

uint8_t CodePoint(int value) {
  return value >= 0 && value <= 255 ? static_cast<uint8_t>(value)
                                    : ColorSpace::kUnspecified;
}

}

void Dav1dContextDeleter::operator()(Dav1dContext* context) const {
  dav1d_close(&context);
}

absl::StatusOr<std::unique_ptr<Decoder>> Dav1dDecoder::Create(
    StreamConfig config, SeekTicket ticket,
    std::shared_ptr<DecoderCallbacks> callbacks,
    const Dav1dDecoderOptions& options) {
  if (options.threads < 0 || options.threads > kMaxThreads) {
    return absl::InvalidArgumentError(
        absl::StrCat("decode.video.threads must be from 0 to ", kMaxThreads,
                     ", not ", options.threads));
  }
  if (options.max_frame_delay < 0 || options.max_frame_delay > kMaxFrameDelay) {
    return absl::InvalidArgumentError(
        absl::StrCat("decode.av1.max_frame_delay must be from 0 to ",
                     kMaxFrameDelay, ", not ", options.max_frame_delay));
  }
  if (options.operating_point < 0 ||
      options.operating_point > kMaxOperatingPoint) {
    return absl::InvalidArgumentError(
        "the AV1 operating point must be from 0 to 31");
  }
  auto decoder = std::make_unique<Dav1dDecoder>(
      config.info, ticket, std::move(callbacks), kGrantSamples);
  auto status = decoder->Open(config, options);
  if (!status.ok()) {
    return status;
  }
  decoder->Start();
  return decoder;
}

Dav1dDecoder::Dav1dDecoder(StreamInfo info, SeekTicket ticket,
                           std::shared_ptr<DecoderCallbacks> callbacks,
                           size_t grant_samples)
    : WorkerDecoder(std::move(info), ticket, std::move(callbacks),
                    grant_samples) {}

Dav1dDecoder::~Dav1dDecoder() { (void)Stop(); }

absl::Status Dav1dDecoder::Open(const StreamConfig& config,
                                const Dav1dDecoderOptions& options) {
  const StreamInfo& info = config.info;
  time_base_ =
      info.time_base.is_positive() ? info.time_base : kMicrosecondTimeBase;
  if (const auto* video = std::get_if<VideoInfo>(&info.format)) {
    frame_rate_ = video->frame_rate;
  }
  auto obus = Av1ConfigObus(info);
  if (!obus.ok()) {
    return obus.status();
  }
  config_obus_ = *std::move(obus);
  Dav1dSettings settings;
  dav1d_default_settings(&settings);
  settings.n_threads = options.threads;
  settings.max_frame_delay = options.max_frame_delay;
  settings.apply_grain = options.apply_film_grain ? 1 : 0;
  settings.operating_point = options.operating_point;
  settings.all_layers = 0;
  settings.logger.callback = nullptr;
  Dav1dContext* raw = nullptr;
  const int opened = dav1d_open(&raw, &settings);
  if (opened < 0) {
    return DecoderInitializationError(
        absl::StrCat("dav1d_open failed with ", opened));
  }
  context_.reset(raw);
  if (!config_obus_.empty()) {
    const auto status =
        FeedBytes(SharedBytes::Copy(config_obus_), Timestamp::kUnknown, 0);
    if (!status.ok()) {
      return DecoderInitializationError(status.message());
    }
  }
  return absl::OkStatus();
}

int64_t Dav1dDecoder::ToStreamTicks(Timestamp timestamp) const {
  if (!timestamp.is_known()) {
    return Timestamp::kUnknown;
  }
  if (timestamp.time_base == time_base_) {
    return timestamp.value;
  }
  const auto converted = Rescale(timestamp, time_base_, Rounding::kNearest);
  return converted.has_value() ? converted->value : Timestamp::kUnknown;
}

absl::Status Dav1dDecoder::CheckConfigChange(const StreamInfo& from,
                                             const StreamInfo& to) const {
  if (to.codec != from.codec || to.type() != from.type()) {
    return absl::UnimplementedError(
        "a codec change needs a new decoder instance");
  }
  auto obus = Av1ConfigObus(to);
  if (!obus.ok()) {
    return obus.status();
  }
  return absl::OkStatus();
}

absl::Status Dav1dDecoder::FeedBytes(const SharedBytes& bytes,
                                     int64_t timestamp, int64_t duration) {
  if (bytes.empty()) {
    return CorruptSampleError("an empty sample");
  }
  auto owner = std::make_unique<SharedBytes>(bytes);
  Dav1dData data{};
  const int wrapped = dav1d_data_wrap(&data, owner->data(), owner->size(),
                                      ReleaseBytes, owner.get());
  if (wrapped < 0) {
    return StatusFromDav1dError(wrapped, "dav1d_data_wrap");
  }
  (void)owner.release();
  data.m.timestamp = timestamp;
  data.m.duration = duration;
  return Feed(data);
}

absl::Status Dav1dDecoder::Feed(Dav1dData& data) {
  absl::Status first_error;
  int stalled = 0;
  while (data.sz > 0) {
    const int sent = dav1d_send_data(context_.get(), &data);
    if (sent < 0 && sent != DAV1D_ERR(EAGAIN)) {
      dav1d_data_unref(&data);
      Resynchronise();
      return StatusFromDav1dError(sent, "dav1d_send_data");
    }
    if (sent == DAV1D_ERR(EAGAIN) && ++stalled > kMaxFeedAttempts) {
      dav1d_data_unref(&data);
      Resynchronise();
      return DecodeFailedError("dav1d did not consume a sample");
    }
    bool more = false;
    auto taken = TakePicture(more);
    if (!taken.ok()) {
      if (taken.code() != absl::StatusCode::kDataLoss) {
        dav1d_data_unref(&data);
        return taken;
      }
      if (first_error.ok()) {
        first_error = std::move(taken);
      }
    }
  }
  return first_error;
}

void Dav1dDecoder::Resynchronise() {
  dav1d_flush(context_.get());
  if (!config_obus_.empty()) {
    Dav1dData data{};
    auto owner = std::make_unique<SharedBytes>(SharedBytes::Copy(config_obus_));
    if (dav1d_data_wrap(&data, owner->data(), owner->size(), ReleaseBytes,
                        owner.get()) == 0) {
      (void)owner.release();
      if (dav1d_send_data(context_.get(), &data) < 0) {
        dav1d_data_unref(&data);
      }
    }
  }
}

absl::Status Dav1dDecoder::Decode(const EncodedSample& sample) {
  if (sample.config_change != nullptr) {
    if (const auto* video =
            std::get_if<VideoInfo>(&sample.config_change->format)) {
      frame_rate_ = video->frame_rate;
    }
    auto obus = Av1ConfigObus(*sample.config_change);
    if (obus.ok() && !obus->empty() && *obus != config_obus_) {
      config_obus_ = *std::move(obus);
      auto status =
          FeedBytes(SharedBytes::Copy(config_obus_), Timestamp::kUnknown, 0);
      if (!status.ok()) {
        return status;
      }
    }
  }
  const int64_t duration = ToStreamTicks(sample.duration);
  return FeedBytes(sample.data, ToStreamTicks(sample.pts),
                   duration == Timestamp::kUnknown ? 0 : duration);
}

absl::Status Dav1dDecoder::Drain() {
  absl::Status first_error;
  for (int pass = 0; pass < 2; ++pass) {
    bool more = true;
    while (more) {
      auto taken = TakePicture(more);
      if (!taken.ok()) {
        if (taken.code() != absl::StatusCode::kDataLoss) {
          return taken;
        }
        if (first_error.ok()) {
          first_error = std::move(taken);
        }
      }
    }
  }
  return first_error;
}

void Dav1dDecoder::Reset() {
  if (context_ != nullptr) {
    Resynchronise();
  }
}

absl::Status Dav1dDecoder::TakePicture(bool& produced) {
  produced = false;
  auto owner = std::make_shared<PictureOwner>();
  const int received = dav1d_get_picture(context_.get(), &owner->picture);
  if (received == DAV1D_ERR(EAGAIN)) {
    return absl::OkStatus();
  }
  if (received < 0) {
    return StatusFromDav1dError(received, "dav1d_get_picture");
  }
  produced = true;
  const Dav1dPicture& picture = owner->picture;
  PixelFormat format = PixelFormat::kUnknown;
  if (picture.p.layout == DAV1D_PIXEL_LAYOUT_I420) {
    format = picture.p.bpc == 8    ? PixelFormat::kI420
             : picture.p.bpc == 10 ? PixelFormat::kI010
                                   : PixelFormat::kUnknown;
  }
  if (format == PixelFormat::kUnknown) {
    return CodecUnsupportedError(absl::StrCat(
        "AV1 pictures with layout ", static_cast<int>(picture.p.layout),
        " and ", picture.p.bpc, " bits have no frame descriptor"));
  }
  DecodedVideoFrame frame;
  frame.width = picture.p.w;
  frame.height = picture.p.h;
  frame.format = format;
  frame.presentation_time = TimeOf(picture.m.timestamp, time_base_);
  frame.duration = DurationOf(picture.m.duration, time_base_);
  if (picture.m.duration <= 0 && frame_rate_.is_positive()) {
    const auto base =
        Rational::Create(frame_rate_.denominator(), frame_rate_.numerator());
    if (base.has_value()) {
      frame.duration = DurationOf(1, *base);
    }
  }
  if (picture.seq_hdr != nullptr) {
    frame.color = {.primaries = CodePoint(picture.seq_hdr->pri),
                   .transfer = CodePoint(picture.seq_hdr->trc),
                   .matrix = CodePoint(picture.seq_hdr->mtrx),
                   .full_range = picture.seq_hdr->color_range != 0};
  }
  frame.planes[0] = {.data = static_cast<const uint8_t*>(picture.data[0]),
                     .stride = static_cast<int32_t>(picture.stride[0])};
  frame.planes[1] = {.data = static_cast<const uint8_t*>(picture.data[1]),
                     .stride = static_cast<int32_t>(picture.stride[1])};
  frame.planes[2] = {.data = static_cast<const uint8_t*>(picture.data[2]),
                     .stride = static_cast<int32_t>(picture.stride[1])};
  frame.storage = std::move(owner);
  return Emit(std::move(frame));
}

}
