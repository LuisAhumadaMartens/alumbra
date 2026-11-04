#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/caption_extractor.h"
#include "umbra/demux/ebml.h"
#include "umbra/demux/mkv_block.h"
#include "umbra/demux/mkv_demuxer.h"
#include "umbra/demux/mkv_elements.h"
#include "umbra/demux/mkv_tracks.h"

namespace umbra::demux {
namespace {

constexpr int64_t kMaxTimelineTicks = int64_t{1} << 61;
constexpr int64_t kNanosecondsPerSecond = 1000000000;

penumbra::Rational NanosecondBase() {
  return penumbra::Rational::Of(1, kNanosecondsPerSecond);
}

penumbra::Timestamp MakeTimestamp(int64_t value, penumbra::Rational base) {
  penumbra::Timestamp timestamp;
  timestamp.value = value;
  timestamp.time_base = base;
  return timestamp;
}

std::optional<int64_t> FrameDuration(const MkvTrackConfig& config,
                                     EbmlBytes frame) {
  if (config.opus) {
    const std::optional<int64_t> ticks = OpusPacketTicks(frame);
    const std::optional<int64_t> nanoseconds =
        ticks.has_value()
            ? penumbra::Rescale(*ticks, config.stream.time_base,
                                NanosecondBase(), penumbra::Rounding::kNearest)
            : std::nullopt;
    if (nanoseconds.has_value()) {
      return nanoseconds;
    }
  }
  return config.default_duration_ns;
}

absl::StatusOr<std::vector<penumbra::SharedBytes>> SliceFrames(
    const penumbra::SharedBytes& element, size_t block_offset,
    const MkvBlock& block, const std::vector<uint8_t>& header_strip) {
  std::vector<penumbra::SharedBytes> frames;
  frames.reserve(block.frames.size());
  for (const FrameRange& range : block.frames) {
    const std::optional<penumbra::SharedBytes> slice =
        element.Slice(block_offset + range.offset, range.size);
    if (!slice.has_value()) {
      return codec::Corrupt("frame lies outside its block");
    }
    if (header_strip.empty() || slice->empty()) {
      frames.push_back(*slice);
      continue;
    }
    std::vector<uint8_t> assembled = header_strip;
    assembled.insert(assembled.end(), slice->span().begin(),
                     slice->span().end());
    frames.push_back(penumbra::SharedBytes::Adopt(std::move(assembled)));
  }
  return frames;
}

struct Trim {
  int64_t start = 0;
  int64_t end = 0;
};

Trim TrimOf(const MkvFramePlan& plan) {
  Trim trim;
  if (plan.pts < 0) {
    trim.start = plan.duration.has_value() ? std::min(*plan.duration, -plan.pts)
                                           : -plan.pts;
  }
  if (plan.padding_ticks.has_value()) {
    if (plan.padding_negative && plan.first) {
      trim.start = std::max(trim.start, *plan.padding_ticks);
    }
    if (!plan.padding_negative && plan.last) {
      trim.end = *plan.padding_ticks;
    }
  }
  if (plan.duration.has_value()) {
    trim.start = std::min(trim.start, *plan.duration);
    trim.end = std::min(trim.end, *plan.duration);
  }
  return trim;
}

std::optional<int64_t> PaddingTicks(const MkvBlockInput& input,
                                    penumbra::Rational base) {
  if (!input.discard_padding_ns.has_value() ||
      *input.discard_padding_ns == std::numeric_limits<int64_t>::min() ||
      *input.discard_padding_ns == 0) {
    return std::nullopt;
  }
  const int64_t magnitude = *input.discard_padding_ns < 0
                                ? -*input.discard_padding_ns
                                : *input.discard_padding_ns;
  return penumbra::Rescale(magnitude,
                           penumbra::Rational::Of(1, kNanosecondsPerSecond),
                           base, penumbra::Rounding::kNearest);
}

absl::StatusOr<std::vector<std::optional<int64_t>>> FrameDurations(
    const MkvTrackConfig& config,
    const std::vector<penumbra::SharedBytes>& frames,
    std::optional<int64_t> block_duration) {
  std::vector<std::optional<int64_t>> durations;
  durations.reserve(frames.size());
  for (const penumbra::SharedBytes& frame : frames) {
    durations.push_back(FrameDuration(config, frame.span()));
  }
  const bool missing = std::ranges::any_of(
      durations,
      [](const std::optional<int64_t>& value) { return !value.has_value(); });
  if (frames.size() == 1 && block_duration.has_value()) {
    durations[0] = block_duration;
  } else if (missing && block_duration.has_value()) {
    const auto count = static_cast<int64_t>(frames.size());
    const int64_t share = *block_duration / count;
    std::ranges::fill(durations, share);
    durations.back() = *block_duration - (share * (count - 1));
  } else if (missing && frames.size() > 1) {
    return codec::Corrupt("laced block has no frame duration");
  }
  return durations;
}

}

absl::Status MatroskaDemuxer::OnSimpleBlock(
    const penumbra::SharedBytes& element, size_t header_size) {
  MkvBlockInput input;
  input.element = &element;
  input.block_offset = header_size;
  input.block_size = element.size() - header_size;
  return EmitBlock(input);
}

absl::Status MatroskaDemuxer::OnBlockGroup(const penumbra::SharedBytes& element,
                                           size_t header_size) {
  ALUMBRA_ASSIGN_OR_RETURN(
      const MkvBlockGroup group,
      ParseBlockGroup(element.span().subspan(header_size)));
  MkvBlockInput input;
  input.element = &element;
  input.block_offset = header_size + group.block_offset;
  input.block_size = group.block_size;
  input.from_group = true;
  input.has_reference = group.has_reference;
  input.duration = group.duration;
  input.discard_padding_ns = group.discard_padding_ns;
  return EmitBlock(input);
}

absl::Status MatroskaDemuxer::EmitBlock(const MkvBlockInput& input) {
  const EbmlBytes bytes =
      input.element->span().subspan(input.block_offset, input.block_size);
  ALUMBRA_ASSIGN_OR_RETURN(const MkvBlock block, ParseBlock(bytes));
  Track* track = FindTrack(block.track_number);
  if (track == nullptr) {
    ++stats_.dropped_blocks;
    return absl::OkStatus();
  }
  if (!cluster_ticks_.has_value()) {
    return codec::Corrupt("a block appears before its cluster timestamp");
  }
  const penumbra::Rational base = track->config.stream.time_base;
  const int64_t raw =
      static_cast<int64_t>(*cluster_ticks_) + block.relative_ticks;
  const std::optional<int64_t> scaled =
      penumbra::Rescale(raw, scale_base_, base, penumbra::Rounding::kNearest);
  if (!scaled.has_value() ||
      track->config.codec_delay_ticks > kMaxTimelineTicks ||
      *scaled > kMaxTimelineTicks ||
      *scaled < track->config.codec_delay_ticks - kMaxTimelineTicks) {
    return codec::Corrupt("block timestamp is out of range");
  }
  return EmitFrames(*track, input, block,
                    *scaled - track->config.codec_delay_ticks);
}

absl::Status MatroskaDemuxer::EmitFrames(Track& track,
                                         const MkvBlockInput& input,
                                         const MkvBlock& block,
                                         int64_t first_pts) {
  const MkvTrackConfig& config = track.config;
  const penumbra::Rational base = config.stream.time_base;
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<penumbra::SharedBytes> frames,
                           SliceFrames(*input.element, input.block_offset,
                                       block, config.header_strip));
  std::optional<int64_t> block_duration;
  if (input.duration.has_value() && *input.duration <= kMaxMkvTimeline) {
    block_duration =
        penumbra::Rescale(static_cast<int64_t>(*input.duration), scale_base_,
                          NanosecondBase(), penumbra::Rounding::kNearest);
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<std::optional<int64_t>> durations,
                           FrameDurations(config, frames, block_duration));
  const std::optional<int64_t> padding = PaddingTicks(input, base);
  int64_t offset_ns = 0;
  for (size_t index = 0; index < frames.size(); ++index) {
    const std::optional<int64_t> duration_ns = durations[index];
    const std::optional<int64_t> offset = penumbra::Rescale(
        offset_ns, NanosecondBase(), base, penumbra::Rounding::kNearest);
    if (!offset.has_value()) {
      return codec::Corrupt("frame offset is out of range");
    }
    if (!frames[index].empty()) {
      MkvFramePlan plan;
      plan.pts = first_pts + *offset;
      if (duration_ns.has_value()) {
        plan.duration = penumbra::Rescale(*duration_ns, NanosecondBase(), base,
                                          penumbra::Rounding::kNearest);
      }
      plan.first = index == 0;
      plan.last = index + 1 == frames.size();
      plan.padding_ticks = padding;
      plan.padding_negative = input.discard_padding_ns.value_or(0) < 0;
      plan.key_frame = !track.video || (input.from_group ? !input.has_reference
                                                         : block.key_frame);
      EmitSample(track, frames[index], plan);
    }
    if (duration_ns.has_value()) {
      if (*duration_ns < 0 || *duration_ns > kMaxTimelineTicks - offset_ns) {
        return codec::Corrupt("frame duration is out of range");
      }
      offset_ns += *duration_ns;
    }
  }
  return absl::OkStatus();
}

void MatroskaDemuxer::EmitSample(Track& track,
                                 const penumbra::SharedBytes& frame,
                                 const MkvFramePlan& plan) {
  const MkvTrackConfig& config = track.config;
  const penumbra::Rational base = config.stream.time_base;
  penumbra::EncodedSample sample;
  sample.track_id = static_cast<uint32_t>(config.number);
  sample.data = frame;
  sample.pts = MakeTimestamp(plan.pts, base);
  sample.dts = sample.pts;
  sample.duration = MakeTimestamp(
      plan.duration.value_or(penumbra::Timestamp::kUnknown), base);
  sample.key_frame = plan.key_frame;
  sample.discontinuity = track.discontinuity_pending;
  track.discontinuity_pending = false;
  if (track.config_change_pending) {
    sample.config_change =
        std::make_shared<const penumbra::StreamInfo>(config.stream);
    track.config_change_pending = false;
  }
  if (!track.video) {
    const Trim trim = TrimOf(plan);
    if (trim.start > 0) {
      sample.trim_start = MakeTimestamp(trim.start, base);
    }
    if (trim.end > 0) {
      sample.trim_end = MakeTimestamp(trim.end, base);
    }
  }
  if (config.stream.nal_unit_framing ==
      penumbra::NalUnitFraming::kLengthPrefixed) {
    std::optional<CaptionCodec> caption_codec;
    if (config.stream.codec == penumbra::CodecId::kH264) {
      caption_codec = CaptionCodec::kH264;
    } else if (config.stream.codec == penumbra::CodecId::kHevc) {
      caption_codec = CaptionCodec::kHevc;
    }
    if (caption_codec.has_value()) {
      std::vector<uint8_t> metadata = ExtractHdrDynamicMetadata(
          *caption_codec, config.stream.nal_length_size, sample.data.span());
      if (!metadata.empty()) {
        sample.hdr_dynamic_metadata =
            penumbra::SharedBytes::Adopt(std::move(metadata));
      }
    }
  }
  output_.OnSample(std::move(sample));
}

}
