#include "umbra/demux/mp4_fragment.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/mp4_box.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {
namespace {

constexpr uint32_t kTraf = FourCc('t', 'r', 'a', 'f');
constexpr uint32_t kTfhd = FourCc('t', 'f', 'h', 'd');
constexpr uint32_t kTfdt = FourCc('t', 'f', 'd', 't');
constexpr uint32_t kTrun = FourCc('t', 'r', 'u', 'n');

constexpr uint32_t kTfhdBaseOffset = 0x1;
constexpr uint32_t kTfhdDescription = 0x2;
constexpr uint32_t kTfhdDuration = 0x8;
constexpr uint32_t kTfhdSize = 0x10;
constexpr uint32_t kTfhdFlags = 0x20;
constexpr uint32_t kTfhdDefaultBaseIsMoof = 0x20000;

constexpr uint32_t kTrunDataOffset = 0x1;
constexpr uint32_t kTrunFirstFlags = 0x4;
constexpr uint32_t kTrunDuration = 0x100;
constexpr uint32_t kTrunSize = 0x200;
constexpr uint32_t kTrunFlags = 0x400;
constexpr uint32_t kTrunCts = 0x800;

struct Tfhd {
  uint32_t track_id = 0;
  std::optional<uint64_t> base_data_offset;
  std::optional<uint32_t> description;
  std::optional<uint32_t> duration;
  std::optional<uint32_t> size;
  std::optional<uint32_t> sample_flags;
  bool default_base_is_moof = false;
};

struct RunState {
  uint64_t base = 0;
  uint64_t cursor = 0;
  int64_t dts = 0;
  bool has_cursor = false;
};

absl::StatusOr<uint32_t> ReadVersionAndFlags(codec::RecordReader& reader,
                                             uint8_t& version) {
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t word, reader.U32());
  version = static_cast<uint8_t>(word >> 24);
  return word & 0x00FFFFFF;
}

absl::StatusOr<std::string> ReadCString(std::span<const uint8_t> payload,
                                        size_t& position) {
  const auto begin = payload.begin() + static_cast<std::ptrdiff_t>(position);
  const auto end = std::find(begin, payload.end(), uint8_t{0});
  if (end == payload.end()) {
    return codec::Corrupt("string is not terminated");
  }
  std::string text(begin, end);
  position = static_cast<size_t>(end - payload.begin()) + 1;
  return text;
}

absl::StatusOr<Tfhd> ParseTfhd(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "tfhd");
  uint8_t version = 0;
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t flags,
                           ReadVersionAndFlags(reader, version));
  Tfhd tfhd;
  ALUMBRA_ASSIGN_OR_RETURN(tfhd.track_id, reader.U32());
  tfhd.default_base_is_moof = (flags & kTfhdDefaultBaseIsMoof) != 0;
  if ((flags & kTfhdBaseOffset) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint64_t offset, reader.Unsigned(8));
    tfhd.base_data_offset = offset;
  }
  if ((flags & kTfhdDescription) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t value, reader.U32());
    tfhd.description = value;
  }
  if ((flags & kTfhdDuration) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t value, reader.U32());
    tfhd.duration = value;
  }
  if ((flags & kTfhdSize) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t value, reader.U32());
    tfhd.size = value;
  }
  if ((flags & kTfhdFlags) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t value, reader.U32());
    tfhd.sample_flags = value;
  }
  return tfhd;
}

absl::StatusOr<int64_t> ParseTfdt(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "tfdt");
  uint8_t version = 0;
  ALUMBRA_RETURN_IF_ERROR(ReadVersionAndFlags(reader, version).status());
  ALUMBRA_ASSIGN_OR_RETURN(const uint64_t time,
                           reader.Unsigned(version == 1 ? 8 : 4));
  if (time > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return codec::Corrupt("tfdt decode time is out of range");
  }
  return static_cast<int64_t>(time);
}

absl::Status AdvanceCursor(RunState& state, uint32_t size) {
  if (std::numeric_limits<uint64_t>::max() - state.cursor < size) {
    return codec::Corrupt("sample offset overflows 64 bits");
  }
  state.cursor += size;
  return absl::OkStatus();
}

absl::Status ApplyDataOffset(RunState& state, int32_t offset) {
  if (offset < 0) {
    const auto back = static_cast<uint64_t>(-static_cast<int64_t>(offset));
    if (state.base < back) {
      return codec::Corrupt("trun data offset precedes the file");
    }
    state.cursor = state.base - back;
    return absl::OkStatus();
  }
  if (std::numeric_limits<uint64_t>::max() - state.base <
      static_cast<uint64_t>(offset)) {
    return codec::Corrupt("trun data offset overflows 64 bits");
  }
  state.cursor = state.base + static_cast<uint64_t>(offset);
  return absl::OkStatus();
}

struct TrunDefaults {
  const Tfhd* tfhd = nullptr;
  const TrackDefaults* track = nullptr;
};

absl::Status ReadTrunSample(codec::RecordReader& reader, uint32_t flags,
                            bool first, std::optional<uint32_t> first_flags,
                            const TrunDefaults& defaults, RunState& state,
                            SampleRef& sample) {
  sample.duration = defaults.tfhd->duration.value_or(defaults.track->duration);
  sample.size = defaults.tfhd->size.value_or(defaults.track->size);
  uint32_t sample_flags =
      defaults.tfhd->sample_flags.value_or(defaults.track->flags);
  if ((flags & kTrunDuration) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(sample.duration, reader.U32());
  }
  if ((flags & kTrunSize) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(sample.size, reader.U32());
  }
  if ((flags & kTrunFlags) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(sample_flags, reader.U32());
  }
  if (first && first_flags.has_value()) {
    sample_flags = *first_flags;
  }
  if ((flags & kTrunCts) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t offset, reader.U32());
    sample.cts_offset = static_cast<int32_t>(offset);
  }
  sample.offset = state.cursor;
  sample.dts = state.dts;
  sample.sync = (sample_flags & kSampleIsNonSync) == 0;
  state.dts += sample.duration;
  return AdvanceCursor(state, sample.size);
}

absl::Status StartRun(codec::RecordReader& reader, uint32_t flags,
                      RunState& state) {
  if ((flags & kTrunDataOffset) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t offset, reader.U32());
    ALUMBRA_RETURN_IF_ERROR(
        ApplyDataOffset(state, static_cast<int32_t>(offset)));
    state.has_cursor = true;
  } else if (!state.has_cursor) {
    state.cursor = state.base;
    state.has_cursor = true;
  }
  return absl::OkStatus();
}

absl::Status CheckRunBounds(uint32_t flags, uint32_t count,
                            size_t remaining_bytes, size_t existing,
                            uint64_t budget) {
  size_t fields = 0;
  for (const uint32_t flag : {kTrunDuration, kTrunSize, kTrunFlags, kTrunCts}) {
    fields += (flags & flag) != 0 ? 1 : 0;
  }
  const uint64_t per_sample = uint64_t{4} * fields;
  if (static_cast<uint64_t>(count) * per_sample > remaining_bytes ||
      existing + count > std::min(budget, kMaxSamplesPerTrack)) {
    return codec::Corrupt(
        absl::StrCat("trun of ", count, " samples exceeds its limits"));
  }
  return absl::OkStatus();
}

absl::Status ParseTrun(std::span<const uint8_t> payload,
                       const TrunDefaults& defaults, RunState& state,
                       uint64_t budget, SampleList& samples) {
  codec::RecordReader reader(payload, "trun");
  uint8_t version = 0;
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t flags,
                           ReadVersionAndFlags(reader, version));
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, reader.U32());
  ALUMBRA_RETURN_IF_ERROR(StartRun(reader, flags, state));
  std::optional<uint32_t> first_flags;
  if ((flags & kTrunFirstFlags) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t value, reader.U32());
    first_flags = value;
  }
  ALUMBRA_RETURN_IF_ERROR(
      CheckRunBounds(flags, count, reader.remaining(), samples.size(), budget));
  samples.reserve(samples.size() + count);
  for (uint32_t index = 0; index < count; ++index) {
    SampleRef sample;
    ALUMBRA_RETURN_IF_ERROR(ReadTrunSample(
        reader, flags, index == 0, first_flags, defaults, state, sample));
    sample.description = static_cast<uint16_t>(std::min<uint32_t>(
        defaults.tfhd->description.value_or(defaults.track->description_index),
        std::numeric_limits<uint16_t>::max()));
    samples.push_back(sample);
  }
  return absl::OkStatus();
}

class MoofParser {
 public:
  MoofParser(uint64_t moof_offset, const FragmentContext& context)
      : moof_offset_(moof_offset),
        context_(&context),
        budget_(context.sample_budget) {}

  absl::StatusOr<Fragment> Parse(std::span<const uint8_t> payload,
                                 uint64_t moof_size) {
    ALUMBRA_ASSIGN_OR_RETURN(const std::vector<ChildBox> children,
                             SplitChildren(payload, 2));
    for (const ChildBox& child : children) {
      if (child.type == kTraf) {
        ALUMBRA_RETURN_IF_ERROR(ParseTraf(child.payload));
      }
    }
    fragment_.moof_offset = moof_offset_;
    fragment_.moof_size = moof_size;
    return std::move(fragment_);
  }

 private:
  absl::Status ParseTraf(std::span<const uint8_t> payload) {
    ALUMBRA_ASSIGN_OR_RETURN(const std::vector<ChildBox> children,
                             SplitChildren(payload, 3));
    const ChildBox* tfhd_box = nullptr;
    const ChildBox* tfdt_box = nullptr;
    for (const ChildBox& child : children) {
      if (child.type == kTfhd && tfhd_box == nullptr) {
        tfhd_box = &child;
      } else if (child.type == kTfdt && tfdt_box == nullptr) {
        tfdt_box = &child;
      }
    }
    if (tfhd_box == nullptr) {
      return codec::Corrupt("traf has no tfhd");
    }
    ALUMBRA_ASSIGN_OR_RETURN(const Tfhd tfhd, ParseTfhd(tfhd_box->payload));
    const size_t index = FindTrack(tfhd.track_id);
    if (index == context_->tracks->size()) {
      return absl::OkStatus();
    }
    TrackFragment track;
    track.track_id = tfhd.track_id;
    RunState state;
    ALUMBRA_RETURN_IF_ERROR(StartRun(tfhd, tfdt_box, index, track, state));
    const TrunDefaults defaults{.tfhd = &tfhd,
                                .track = &(*context_->tracks)[index].defaults};
    for (const ChildBox& child : children) {
      if (child.type != kTrun) {
        continue;
      }
      ALUMBRA_RETURN_IF_ERROR(
          ParseTrun(child.payload, defaults, state, budget_, track.samples));
    }
    budget_ -= std::min<uint64_t>(budget_, track.samples.size());
    next_data_offset_ = state.has_cursor ? state.cursor : next_data_offset_;
    fragment_.tracks.push_back(std::move(track));
    return absl::OkStatus();
  }

  absl::Status StartRun(const Tfhd& tfhd, const ChildBox* tfdt_box,
                        size_t index, TrackFragment& track, RunState& state) {
    if (tfhd.base_data_offset.has_value()) {
      state.base = *tfhd.base_data_offset;
    } else if (tfhd.default_base_is_moof || !next_data_offset_.has_value()) {
      state.base = moof_offset_;
    } else {
      state.base = *next_data_offset_;
    }
    if (tfdt_box != nullptr) {
      ALUMBRA_ASSIGN_OR_RETURN(track.first_dts, ParseTfdt(tfdt_box->payload));
      track.has_decode_time = true;
    } else {
      track.first_dts = (*context_->expected_dts)[index].value_or(0);
    }
    state.dts = track.first_dts;
    return absl::OkStatus();
  }

  [[nodiscard]] size_t FindTrack(uint32_t track_id) const {
    for (size_t index = 0; index < context_->tracks->size(); ++index) {
      if ((*context_->tracks)[index].track_id == track_id) {
        return index;
      }
    }
    return context_->tracks->size();
  }

  uint64_t moof_offset_;
  const FragmentContext* context_;
  uint64_t budget_;
  std::optional<uint64_t> next_data_offset_;
  Fragment fragment_;
};

}

absl::StatusOr<Fragment> ParseMoof(std::span<const uint8_t> payload,
                                   uint64_t moof_offset, uint64_t moof_size,
                                   const FragmentContext& context) {
  MoofParser parser(moof_offset, context);
  return parser.Parse(payload, moof_size);
}

absl::StatusOr<Sidx> ParseSidx(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "sidx");
  uint8_t version = 0;
  ALUMBRA_RETURN_IF_ERROR(ReadVersionAndFlags(reader, version).status());
  Sidx sidx;
  ALUMBRA_ASSIGN_OR_RETURN(sidx.reference_id, reader.U32());
  ALUMBRA_ASSIGN_OR_RETURN(sidx.timescale, reader.U32());
  const size_t width = version == 1 ? 8 : 4;
  ALUMBRA_ASSIGN_OR_RETURN(sidx.earliest_presentation_time,
                           reader.Unsigned(width));
  ALUMBRA_ASSIGN_OR_RETURN(sidx.first_offset, reader.Unsigned(width));
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(2));
  ALUMBRA_ASSIGN_OR_RETURN(const uint16_t count, reader.U16());
  if (static_cast<uint64_t>(count) * 12 > reader.remaining()) {
    return codec::Corrupt("sidx references exceed their box");
  }
  sidx.references.resize(count);
  for (SidxReference& reference : sidx.references) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t type_and_size, reader.U32());
    reference.hierarchical = (type_and_size >> 31) != 0;
    reference.size = type_and_size & 0x7FFFFFFF;
    ALUMBRA_ASSIGN_OR_RETURN(reference.duration, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t sap, reader.U32());
    reference.starts_with_sap = (sap >> 31) != 0;
  }
  return sidx;
}

absl::StatusOr<Emsg> ParseEmsg(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "emsg");
  uint8_t version = 0;
  ALUMBRA_RETURN_IF_ERROR(ReadVersionAndFlags(reader, version).status());
  if (version > 1) {
    return codec::Unsupported("emsg version is not supported");
  }
  Emsg emsg;
  emsg.version = version;
  size_t position = 4;
  if (version == 0) {
    ALUMBRA_ASSIGN_OR_RETURN(emsg.scheme, ReadCString(payload, position));
    ALUMBRA_ASSIGN_OR_RETURN(emsg.value, ReadCString(payload, position));
    codec::RecordReader fields(payload.subspan(position), "emsg fields");
    ALUMBRA_ASSIGN_OR_RETURN(emsg.timescale, fields.U32());
    ALUMBRA_ASSIGN_OR_RETURN(emsg.time, fields.Unsigned(4));
    ALUMBRA_ASSIGN_OR_RETURN(emsg.event_duration, fields.U32());
    ALUMBRA_ASSIGN_OR_RETURN(emsg.id, fields.U32());
    ALUMBRA_ASSIGN_OR_RETURN(const std::span<const uint8_t> message,
                             fields.Take(fields.remaining()));
    emsg.message.assign(message.begin(), message.end());
    return emsg;
  }
  ALUMBRA_ASSIGN_OR_RETURN(emsg.timescale, reader.U32());
  ALUMBRA_ASSIGN_OR_RETURN(emsg.time, reader.Unsigned(8));
  ALUMBRA_ASSIGN_OR_RETURN(emsg.event_duration, reader.U32());
  ALUMBRA_ASSIGN_OR_RETURN(emsg.id, reader.U32());
  position = 4 + 4 + 8 + 4 + 4;
  ALUMBRA_ASSIGN_OR_RETURN(emsg.scheme, ReadCString(payload, position));
  ALUMBRA_ASSIGN_OR_RETURN(emsg.value, ReadCString(payload, position));
  emsg.message.assign(payload.begin() + static_cast<std::ptrdiff_t>(position),
                      payload.end());
  return emsg;
}

absl::StatusOr<Prft> ParsePrft(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "prft");
  uint8_t version = 0;
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t flags,
                           ReadVersionAndFlags(reader, version));
  Prft prft;
  prft.flags = flags;
  ALUMBRA_ASSIGN_OR_RETURN(prft.track_id, reader.U32());
  ALUMBRA_ASSIGN_OR_RETURN(prft.ntp_timestamp, reader.Unsigned(8));
  ALUMBRA_ASSIGN_OR_RETURN(prft.media_time,
                           reader.Unsigned(version == 1 ? 8 : 4));
  return prft;
}

}
