#include "mp4_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

#include "../codec/fixtures.h"

namespace umbra::demux::test {

Writer::Writer() { bytes_.reserve(256); }

Writer& Writer::U8(uint32_t value) {
  bytes_.push_back(static_cast<uint8_t>(value));
  return *this;
}

Writer& Writer::U16(uint32_t value) {
  U8(value >> 8);
  return U8(value);
}

Writer& Writer::U32(uint32_t value) {
  U16(value >> 16);
  return U16(value);
}

Writer& Writer::U64(uint64_t value) {
  U32(static_cast<uint32_t>(value >> 32));
  return U32(static_cast<uint32_t>(value));
}

Writer& Writer::Zeros(size_t count) {
  bytes_.insert(bytes_.end(), count, 0);
  return *this;
}

Writer& Writer::Text(std::string_view text) {
  bytes_.insert(bytes_.end(), text.begin(), text.end());
  return *this;
}

Writer& Writer::Add(const Bytes& bytes) {
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  return *this;
}

Bytes Box(std::string_view type, const Bytes& payload) {
  Writer writer;
  writer.U32(static_cast<uint32_t>(payload.size() + 8)).Text(type).Add(payload);
  return writer.Data();
}

Bytes LargeBox(std::string_view type, const Bytes& payload) {
  Writer writer;
  writer.U32(1).Text(type).U64(payload.size() + 16).Add(payload);
  return writer.Data();
}

Bytes FullBox(std::string_view type, uint8_t version, uint32_t flags,
              const Bytes& payload) {
  Writer writer;
  writer.U8(version).U8(flags >> 16).U8(flags >> 8).U8(flags).Add(payload);
  return Box(type, writer.Data());
}

Bytes Concat(const std::vector<Bytes>& parts) {
  Bytes result;
  for (const Bytes& part : parts) {
    result.insert(result.end(), part.begin(), part.end());
  }
  return result;
}

Bytes FromHex(std::string_view hex) {
  Bytes bytes;
  const auto digit = [](char c) {
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
  };
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(
        static_cast<uint8_t>((digit(hex[i]) << 4) | digit(hex[i + 1])));
  }
  return bytes;
}

Bytes BuiltFile::Flatten() const {
  Bytes result(static_cast<size_t>(size), 0);
  for (const Segment& segment : segments) {
    std::ranges::copy(
        segment.bytes,
        result.begin() + static_cast<std::ptrdiff_t>(segment.offset));
  }
  return result;
}

Bytes BuiltFile::Read(uint64_t offset, size_t size) const {
  Bytes result;
  for (const Segment& segment : segments) {
    const uint64_t begin = segment.offset;
    const uint64_t end = begin + segment.bytes.size();
    for (uint64_t position = std::max(offset, begin);
         position < std::min(offset + size, end); ++position) {
      result.push_back(segment.bytes[static_cast<size_t>(position - begin)]);
    }
  }
  return result;
}

Bytes SampleBytes(uint32_t track_id, size_t index, uint32_t size) {
  Bytes bytes(size);
  for (size_t position = 0; position < bytes.size(); ++position) {
    bytes[position] = static_cast<uint8_t>(
        (static_cast<size_t>(track_id) * 37) + (index * 11) + (position * 3));
  }
  return bytes;
}

Bytes Ftyp() {
  Writer writer;
  writer.Text("isom").U32(512).Text("isom").Text("iso2").Text("mp41");
  return Box("ftyp", writer.Data());
}

namespace {

Bytes AvcSampleEntryPayload(const std::vector<Bytes>& extra_video_boxes) {
  Writer writer;
  writer.Zeros(6)
      .U16(1)
      .Zeros(16)
      .U16(1920)
      .U16(1080)
      .U32(0x00480000)
      .U32(0x00480000)
      .U32(0)
      .U16(1)
      .Zeros(32)
      .U16(0x18)
      .U16(0xFFFF);
  writer.Add(Box("avcC", FromHex(codec::test_data::kAvcC1080)));
  for (const Bytes& box : extra_video_boxes) {
    writer.Add(box);
  }
  return writer.Data();
}

Bytes DataFor(const TrackSpec& track, size_t index) {
  const auto found = track.data_override.find(index);
  if (found != track.data_override.end()) {
    return found->second;
  }
  return SampleBytes(track.id, index, track.samples[index].size);
}

Bytes AacSampleEntryPayload() {
  Writer entry;
  entry.Zeros(6).U16(1).U16(0).U16(0).U32(0).U16(2).U16(16).U16(0).U16(0).U32(
      44100U << 16);
  Writer descriptor;
  descriptor.U8(0x03).U8(25).U16(1).U8(0);
  descriptor.U8(0x04).U8(17).U8(0x40).U8(0x15).Zeros(3).U32(0).U32(0);
  descriptor.U8(0x05).U8(2).U8(0x12).U8(0x10);
  descriptor.U8(0x06).U8(1).U8(2);
  entry.Add(FullBox("esds", 0, 0, descriptor.Data()));
  return entry.Data();
}

Bytes SampleEntry(const TrackSpec& track) {
  if (track.codec == TestCodec::kAvc) {
    return Box("avc1", AvcSampleEntryPayload(track.extra_video_boxes));
  }
  if (track.codec == TestCodec::kUnknown) {
    return Box("xxxx", Bytes(28, 0));
  }
  return Box("mp4a", AacSampleEntryPayload());
}

Bytes Stsd(const TrackSpec& track) {
  Writer writer;
  writer.U32(1).Add(SampleEntry(track));
  return FullBox("stsd", 0, 0, writer.Data());
}

Bytes Stts(const TrackSpec& track) {
  Writer writer;
  std::vector<std::pair<uint32_t, uint32_t>> runs;
  for (const SampleSpec& sample : track.samples) {
    if (!runs.empty() && runs.back().second == sample.duration) {
      ++runs.back().first;
    } else {
      runs.emplace_back(1, sample.duration);
    }
  }
  writer.U32(static_cast<uint32_t>(runs.size()));
  for (const auto& [count, delta] : runs) {
    writer.U32(count).U32(delta);
  }
  return FullBox("stts", 0, 0, writer.Data());
}

Bytes Ctts(const TrackSpec& track) {
  Writer writer;
  writer.U32(static_cast<uint32_t>(track.samples.size()));
  for (const SampleSpec& sample : track.samples) {
    writer.U32(1).U32(static_cast<uint32_t>(sample.cts));
  }
  return FullBox("ctts", track.ctts_version, 0, writer.Data());
}

Bytes Stsc(const TrackSpec& track) {
  Writer writer;
  const size_t chunks = (track.samples.size() + track.samples_per_chunk - 1) /
                        track.samples_per_chunk;
  const size_t tail = track.samples.size() % track.samples_per_chunk;
  const bool has_tail = tail != 0 && chunks > 0;
  writer.U32(has_tail ? 2 : 1);
  writer.U32(1).U32(track.samples_per_chunk).U32(1);
  if (has_tail) {
    writer.U32(static_cast<uint32_t>(chunks))
        .U32(static_cast<uint32_t>(tail))
        .U32(1);
  }
  return FullBox("stsc", 0, 0, writer.Data());
}

Bytes Stsz(const TrackSpec& track) {
  Writer writer;
  if (track.stz2) {
    writer.U8(0).U8(0).U8(0).U8(16).U32(
        static_cast<uint32_t>(track.samples.size()));
    for (const SampleSpec& sample : track.samples) {
      writer.U16(sample.size);
    }
    return FullBox("stz2", 0, 0, writer.Data());
  }
  writer.U32(0).U32(static_cast<uint32_t>(track.samples.size()));
  for (const SampleSpec& sample : track.samples) {
    writer.U32(sample.size);
  }
  return FullBox("stsz", 0, 0, writer.Data());
}

Bytes Stco(const TrackSpec& track, const std::vector<uint64_t>& offsets) {
  Writer writer;
  writer.U32(static_cast<uint32_t>(offsets.size()));
  for (const uint64_t offset : offsets) {
    if (track.co64) {
      writer.U64(offset);
    } else {
      writer.U32(static_cast<uint32_t>(offset));
    }
  }
  return FullBox(track.co64 ? "co64" : "stco", 0, 0, writer.Data());
}

Bytes Stss(const TrackSpec& track) {
  Writer writer;
  std::vector<uint32_t> sync;
  for (size_t index = 0; index < track.samples.size(); ++index) {
    if (track.samples[index].sync) {
      sync.push_back(static_cast<uint32_t>(index + 1));
    }
  }
  writer.U32(static_cast<uint32_t>(sync.size()));
  for (const uint32_t number : sync) {
    writer.U32(number);
  }
  return FullBox("stss", 0, 0, writer.Data());
}

bool HasNonSync(const TrackSpec& track) {
  return std::ranges::any_of(
      track.samples, [](const SampleSpec& sample) { return !sample.sync; });
}

Bytes Elst(const TrackSpec& track) {
  Writer writer;
  writer.U32(static_cast<uint32_t>(track.edits.size()));
  for (const EditSpec& edit : track.edits) {
    if (track.elst_version == 1) {
      writer.U64(edit.segment_duration)
          .U64(static_cast<uint64_t>(edit.media_time));
    } else {
      writer.U32(static_cast<uint32_t>(edit.segment_duration))
          .U32(static_cast<uint32_t>(edit.media_time));
    }
    writer.U16(1).U16(0);
  }
  return FullBox("elst", track.elst_version, 0, writer.Data());
}

Bytes Mdhd(const TrackSpec& track) {
  Writer writer;
  uint64_t total = 0;
  for (const SampleSpec& sample : track.samples) {
    total += sample.duration;
  }
  writer.U32(0)
      .U32(0)
      .U32(track.timescale)
      .U32(static_cast<uint32_t>(total))
      .U16(0x55C4)
      .U16(0);
  return FullBox("mdhd", 0, 0, writer.Data());
}

Bytes Hdlr(const TrackSpec& track) {
  Writer writer;
  std::string_view handler = "soun";
  if (track.codec == TestCodec::kAvc) {
    handler = "vide";
  } else if (track.codec == TestCodec::kUnknown) {
    handler = "text";
  }
  writer.U32(0).Text(handler).Zeros(12).U8(0);
  return FullBox("hdlr", 0, 0, writer.Data());
}

Bytes Tkhd(const TrackSpec& track) {
  Writer writer;
  writer.U32(0).U32(0).U32(track.id).U32(0).U32(0).Zeros(60);
  return FullBox("tkhd", 0, 3, writer.Data());
}

Bytes Mvhd(uint32_t timescale, uint32_t duration) {
  Writer writer;
  writer.U32(0)
      .U32(0)
      .U32(timescale)
      .U32(duration)
      .U32(0x00010000)
      .U16(0x0100)
      .Zeros(70)
      .U32(2);
  return FullBox("mvhd", 0, 0, writer.Data());
}

Bytes Trak(const TrackSpec& track, const std::vector<uint64_t>& offsets,
           bool fragmented) {
  std::vector<Bytes> stbl{Stsd(track), Stts(track)};
  if (track.use_ctts) {
    stbl.push_back(Ctts(track));
  }
  if (HasNonSync(track)) {
    stbl.push_back(Stss(track));
  }
  stbl.push_back(Stsc(track));
  stbl.push_back(Stsz(track));
  stbl.push_back(Stco(track, offsets));
  if (fragmented) {
    stbl.clear();
    Writer entries;
    entries.U32(0);
    stbl = {Stsd(track), FullBox("stts", 0, 0, entries.Data()),
            FullBox("stsc", 0, 0, entries.Data()),
            FullBox("stsz", 0, 0, Writer().U32(0).U32(0).Data()),
            FullBox("stco", 0, 0, entries.Data())};
  }
  const Bytes minf = Box("minf", Box("stbl", Concat(stbl)));
  const Bytes mdia = Box("mdia", Concat({Mdhd(track), Hdlr(track), minf}));
  std::vector<Bytes> parts{Tkhd(track)};
  if (!track.edits.empty()) {
    parts.push_back(Box("edts", Elst(track)));
  }
  parts.push_back(mdia);
  return Box("trak", Concat(parts));
}

struct ChunkPlan {
  size_t track = 0;
  size_t first_sample = 0;
  size_t count = 0;
};

std::vector<ChunkPlan> PlanChunks(const std::vector<TrackSpec>& tracks) {
  std::vector<ChunkPlan> plan;
  std::vector<size_t> next(tracks.size(), 0);
  bool progress = true;
  while (progress) {
    progress = false;
    for (size_t index = 0; index < tracks.size(); ++index) {
      const TrackSpec& track = tracks[index];
      if (next[index] >= track.samples.size()) {
        continue;
      }
      const size_t count = std::min<size_t>(track.samples_per_chunk,
                                            track.samples.size() - next[index]);
      plan.push_back(ChunkPlan{
          .track = index, .first_sample = next[index], .count = count});
      next[index] += count;
      progress = true;
    }
  }
  return plan;
}

Bytes BuildMoov(const ProgressiveSpec& spec,
                const std::vector<std::vector<uint64_t>>& offsets) {
  std::vector<Bytes> parts{Mvhd(spec.movie_timescale, spec.movie_duration)};
  for (size_t index = 0; index < spec.tracks.size(); ++index) {
    parts.push_back(Trak(spec.tracks[index], offsets[index], false));
  }
  return Box("moov", Concat(parts));
}

}

BuiltFile BuildProgressive(const ProgressiveSpec& spec) {
  const std::vector<ChunkPlan> plan = PlanChunks(spec.tracks);
  std::vector<std::vector<uint64_t>> dummy(spec.tracks.size());
  for (const ChunkPlan& chunk : plan) {
    dummy[chunk.track].push_back(0);
  }
  const Bytes ftyp = Ftyp();
  uint64_t payload_offset = ftyp.size() + 8;
  if (!spec.moov_at_end) {
    payload_offset += BuildMoov(spec, dummy).size();
  }
  BuiltFile built;
  built.mdat_payload_offset = payload_offset;
  std::vector<std::vector<uint64_t>> offsets(spec.tracks.size());
  Bytes payload;
  for (const ChunkPlan& chunk : plan) {
    offsets[chunk.track].push_back(payload_offset + payload.size() +
                                   spec.data_shift);
    for (size_t index = 0; index < chunk.count; ++index) {
      const size_t sample = chunk.first_sample + index;
      const TrackSpec& track = spec.tracks[chunk.track];
      Bytes data = DataFor(track, sample);
      ExpectedSample expected;
      expected.track_id = track.id;
      expected.index = sample;
      expected.offset = payload_offset + payload.size() + spec.data_shift;
      expected.data = data;
      built.expected.push_back(std::move(expected));
      payload.insert(payload.end(), data.begin(), data.end());
    }
  }
  const Bytes moov = BuildMoov(spec, offsets);
  Writer header;
  header.U32(static_cast<uint32_t>(payload.size() + 8)).Text("mdat");
  built.moov_size = moov.size();
  if (spec.moov_at_end) {
    built.moov_offset = payload_offset + payload.size();
    built.segments.push_back({0, Concat({ftyp, header.Data()})});
    built.segments.push_back({payload_offset + spec.data_shift, payload});
    built.segments.push_back({built.moov_offset + spec.data_shift, moov});
    built.size = built.moov_offset + spec.data_shift + moov.size();
    return built;
  }
  built.moov_offset = ftyp.size();
  built.segments.push_back({0, Concat({ftyp, moov, header.Data()})});
  built.segments.push_back({payload_offset + spec.data_shift, payload});
  built.size = payload_offset + spec.data_shift + payload.size();
  return built;
}

namespace {

Bytes Mvex(const std::vector<TrackSpec>& tracks) {
  std::vector<Bytes> parts;
  for (const TrackSpec& track : tracks) {
    Writer writer;
    writer.U32(track.id).U32(1).U32(0).U32(0).U32(0);
    parts.push_back(FullBox("trex", 0, 0, writer.Data()));
  }
  return Box("mvex", Concat(parts));
}

uint32_t SampleFlags(const SampleSpec& sample) {
  return sample.sync ? 0x02000000U : 0x01010000U;
}

Bytes Trun(const TrackSpec& track, size_t first, size_t count,
           int32_t data_offset) {
  Writer writer;
  writer.U32(static_cast<uint32_t>(count))
      .U32(static_cast<uint32_t>(data_offset));
  for (size_t index = first; index < first + count; ++index) {
    const SampleSpec& sample = track.samples[index];
    writer.U32(sample.duration)
        .U32(sample.size)
        .U32(SampleFlags(sample))
        .U32(static_cast<uint32_t>(sample.cts));
  }
  return FullBox("trun", track.ctts_version,
                 0x1 | 0x100 | 0x200 | 0x400 | 0x800, writer.Data());
}

Bytes Traf(const FragmentedSpec& spec, const TrackSpec& track, size_t first,
           size_t count, int32_t data_offset, int64_t base_time) {
  Writer tfhd;
  tfhd.U32(track.id);
  std::vector<Bytes> parts{FullBox(
      "tfhd", 0, spec.default_base_is_moof ? 0x20000U : 0U, tfhd.Data())};
  if (spec.with_tfdt) {
    parts.push_back(FullBox(
        "tfdt", 1, 0, Writer().U64(static_cast<uint64_t>(base_time)).Data()));
  }
  parts.push_back(Trun(track, first, count, data_offset));
  return Box("traf", Concat(parts));
}

int64_t BaseTime(const TrackSpec& track, size_t first) {
  int64_t total = 0;
  for (size_t index = 0; index < first; ++index) {
    total += track.samples[index].duration;
  }
  return total;
}

Bytes Moof(const FragmentedSpec& spec, size_t fragment, size_t moof_size) {
  std::vector<Bytes> parts{FullBox(
      "mfhd", 0, 0, Writer().U32(static_cast<uint32_t>(fragment + 1)).Data())};
  size_t data = moof_size + 8;
  bool first_track = true;
  for (const TrackSpec& track : spec.tracks) {
    const size_t first = fragment * spec.samples_per_fragment;
    const size_t count =
        std::min(spec.samples_per_fragment,
                 track.samples.size() - std::min(first, track.samples.size()));
    int64_t base = BaseTime(track, std::min(first, track.samples.size()));
    const auto override_time = spec.tfdt_override.find(fragment);
    if (override_time != spec.tfdt_override.end()) {
      base = override_time->second;
    }
    const bool relative_to_moof = spec.default_base_is_moof || first_track;
    parts.push_back(
        Traf(spec, track, std::min(first, track.samples.size()), count,
             relative_to_moof ? static_cast<int32_t>(data) : 0, base));
    first_track = false;
    for (size_t index = 0; index < count; ++index) {
      data += track.samples[first + index].size;
    }
  }
  return Box("moof", Concat(parts));
}

}

Bytes InitSegment(const FragmentedSpec& spec) {
  std::vector<Bytes> parts{Mvhd(spec.movie_timescale, 0)};
  for (const TrackSpec& track : spec.tracks) {
    parts.push_back(Trak(track, {}, true));
  }
  parts.push_back(Mvex(spec.tracks));
  return Concat({Ftyp(), Box("moov", Concat(parts))});
}

FragmentedFile BuildFragmented(const FragmentedSpec& spec) {
  FragmentedFile file;
  const Bytes init = InitSegment(spec);
  file.init_size = init.size();
  std::vector<Bytes> fragments;
  for (size_t fragment = 0; fragment < spec.fragments; ++fragment) {
    const size_t first = fragment * spec.samples_per_fragment;
    Bytes payload;
    for (const TrackSpec& track : spec.tracks) {
      for (size_t index = first;
           index <
           std::min(first + spec.samples_per_fragment, track.samples.size());
           ++index) {
        const Bytes data = DataFor(track, index);
        payload.insert(payload.end(), data.begin(), data.end());
      }
    }
    const size_t moof_size = Moof(spec, fragment, 0).size();
    Bytes unit;
    const auto extra = spec.boxes_before_fragment.find(fragment);
    if (extra != spec.boxes_before_fragment.end()) {
      unit = extra->second;
    }
    const Bytes moof = Moof(spec, fragment, moof_size);
    const Bytes mdat = Box("mdat", payload);
    unit.insert(unit.end(), moof.begin(), moof.end());
    unit.insert(unit.end(), mdat.begin(), mdat.end());
    fragments.push_back(std::move(unit));
  }
  Bytes sidx;
  if (spec.with_sidx) {
    Writer writer;
    writer.U32(spec.tracks[0].id)
        .U32(spec.tracks[0].timescale)
        .U64(0)
        .U64(0)
        .U16(0)
        .U16(static_cast<uint32_t>(fragments.size()));
    for (size_t fragment = 0; fragment < fragments.size(); ++fragment) {
      uint32_t duration = 0;
      const size_t first = fragment * spec.samples_per_fragment;
      for (size_t index = first;
           index < std::min(first + spec.samples_per_fragment,
                            spec.tracks[0].samples.size());
           ++index) {
        duration += spec.tracks[0].samples[index].duration;
      }
      writer.U32(static_cast<uint32_t>(fragments[fragment].size()))
          .U32(duration)
          .U32(0x90000000U);
    }
    sidx = FullBox("sidx", 1, 0, writer.Data());
  }
  file.bytes = init;
  file.bytes.insert(file.bytes.end(), sidx.begin(), sidx.end());
  for (const Bytes& fragment : fragments) {
    file.fragment_offsets.push_back(file.bytes.size());
    file.bytes.insert(file.bytes.end(), fragment.begin(), fragment.end());
  }
  return file;
}

Bytes EmsgV0(std::string_view scheme, std::string_view value,
             uint32_t timescale, uint32_t delta, uint32_t duration, uint32_t id,
             const Bytes& message) {
  Writer writer;
  writer.Text(scheme)
      .U8(0)
      .Text(value)
      .U8(0)
      .U32(timescale)
      .U32(delta)
      .U32(duration)
      .U32(id)
      .Add(message);
  return FullBox("emsg", 0, 0, writer.Data());
}

Bytes EmsgV1(std::string_view scheme, std::string_view value,
             uint32_t timescale, uint64_t time, uint32_t duration, uint32_t id,
             const Bytes& message) {
  Writer writer;
  writer.U32(timescale)
      .U64(time)
      .U32(duration)
      .U32(id)
      .Text(scheme)
      .U8(0)
      .Text(value)
      .U8(0)
      .Add(message);
  return FullBox("emsg", 1, 0, writer.Data());
}

Bytes PrftBox(uint32_t track_id, uint64_t ntp, uint64_t media_time,
              uint8_t version) {
  Writer writer;
  writer.U32(track_id).U64(ntp);
  if (version == 1) {
    writer.U64(media_time);
  } else {
    writer.U32(static_cast<uint32_t>(media_time));
  }
  return FullBox("prft", version, 8, writer.Data());
}

}
