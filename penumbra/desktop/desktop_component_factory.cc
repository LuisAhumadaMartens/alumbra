#include "penumbra/desktop/desktop_component_factory.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/desktop/decoder_table.h"
#include "penumbra/desktop/ffmpeg_decoder.h"
#include "penumbra/desktop/software_decoder_probe.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "libavcodec/codec.h"
}

namespace penumbra::desktop {
namespace {

bool IsVideoCodec(CodecId codec) {
  return codec == CodecId::kH264 || codec == CodecId::kHevc ||
         codec == CodecId::kVp9 || codec == CodecId::kAv1;
}

bool IsFfmpegEntry(const DecoderEntry& entry) {
  return entry.codec != CodecId::kAv1;
}

std::string CandidateName(const DecoderEntry& entry) {
  return absl::StrCat(kFfmpegDecoderPrefix, entry.decoder_name);
}

const DecoderEntry* EntryForCandidate(const DecoderCandidate& candidate) {
  for (const DecoderEntry& entry : SoftwareDecoderTable()) {
    if (IsFfmpegEntry(entry) && CandidateName(entry) == candidate.name) {
      return &entry;
    }
  }
  return nullptr;
}

}

DesktopComponentFactory::DesktopComponentFactory(
    DesktopDecoderOptions options, std::shared_ptr<const DecoderProbe> probe)
    : options_(options),
      probe_(probe != nullptr ? std::move(probe)
                              : std::make_shared<SoftwareDecoderProbe>()) {}

absl::StatusOr<std::vector<DecoderCandidate>>
DesktopComponentFactory::GetDecoderCandidates(StreamType type) const {
  std::vector<DecoderCandidate> candidates;
  if (type == StreamType::kText) {
    return candidates;
  }
  for (const DecoderEntry& entry : SoftwareDecoderTable()) {
    if (!IsFfmpegEntry(entry) ||
        IsVideoCodec(entry.codec) != (type == StreamType::kVideo)) {
      continue;
    }
    auto compiled = probe_->IsCompiledIn(entry.decoder_name);
    if (!compiled.ok()) {
      return compiled.status();
    }
    if (!*compiled) {
      continue;
    }
    candidates.push_back({.name = CandidateName(entry),
                          .kind = DecoderKind::kBundledSoftware,
                          .output = DecoderOutput::kCpuBuffer,
                          .secure = false,
                          .codecs = {entry.codec},
                          .max_width = 0,
                          .max_height = 0,
                          .quirks = {}});
  }
  return candidates;
}

absl::StatusOr<std::unique_ptr<Decoder>> DesktopComponentFactory::CreateDecoder(
    const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
    std::shared_ptr<DecoderCallbacks> callbacks) {
  if (callbacks == nullptr) {
    return absl::InvalidArgumentError("a decoder needs callbacks");
  }
  const DecoderEntry* entry = EntryForCandidate(candidate);
  if (entry == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("unknown decoder candidate ", candidate.name));
  }
  if (config.info.codec != entry->codec ||
      std::ranges::find(candidate.codecs, config.info.codec) ==
          candidate.codecs.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat(candidate.name, " does not decode ", config.info.codec));
  }
  if (config.encryption_scheme.has_value()) {
    return absl::FailedPreconditionError(
        "a software decoder cannot decrypt a protected stream");
  }
  const std::string name(entry->decoder_name);
  const AVCodec* codec = avcodec_find_decoder_by_name(name.c_str());
  if (codec == nullptr || av_codec_is_decoder(codec) == 0) {
    return absl::UnimplementedError(absl::StrCat(
        "ALUMBRA-4001: FFmpeg was built without the ", name, " decoder"));
  }
  return FfmpegDecoder::Create(*codec, config, ticket, std::move(callbacks),
                               {.video_threads = options_.video_threads});
}

absl::StatusOr<std::unique_ptr<VideoSink>>
DesktopComponentFactory::CreateVideoSink([[maybe_unused]] OutputMode mode) {
  return absl::UnimplementedError(
      "the desktop video sink arrives with the SDL renderer");
}

absl::StatusOr<std::unique_ptr<AudioSink>>
DesktopComponentFactory::CreateAudioSink() {
  return absl::UnimplementedError(
      "the desktop audio sink arrives with the SDL audio output");
}

}
