#include "ts_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "mp4_builder.h"
#include "umbra/demux/ts_packet.h"

namespace umbra::demux::test {
namespace {

constexpr size_t kPayloadCapacity = 184;
constexpr uint64_t kWrapMask = (uint64_t{1} << 33) - 1;

Bytes Timestamp(uint8_t prefix, uint64_t value) {
  value &= kWrapMask;
  return {
      static_cast<uint8_t>((prefix << 4U) | (((value >> 30U) & 7U) << 1U) | 1U),
      static_cast<uint8_t>((value >> 22U) & 0xFFU),
      static_cast<uint8_t>((((value >> 15U) & 0x7FU) << 1U) | 1U),
      static_cast<uint8_t>((value >> 7U) & 0xFFU),
      static_cast<uint8_t>(((value & 0x7FU) << 1U) | 1U)};
}

void AppendCrc(Bytes& section) {
  const uint32_t crc = ts::Crc32Mpeg(section);
  for (int shift = 24; shift >= 0; shift -= 8) {
    section.push_back(static_cast<uint8_t>((crc >> shift) & 0xFFU));
  }
}

Bytes SeiMessage(const Bytes& payload) {
  Bytes message = {4, static_cast<uint8_t>(payload.size())};
  message.insert(message.end(), payload.begin(), payload.end());
  return message;
}

}

std::vector<Bytes> TsWriter::Section(uint16_t pid, const Bytes& section) {
  Bytes payload = {0};
  payload.insert(payload.end(), section.begin(), section.end());
  std::vector<Bytes> packets;
  bool start = true;
  for (size_t offset = 0; offset < payload.size(); offset += kPayloadCapacity) {
    const size_t count = std::min(kPayloadCapacity, payload.size() - offset);
    Bytes chunk(payload.begin() + static_cast<ptrdiff_t>(offset),
                payload.begin() + static_cast<ptrdiff_t>(offset + count));
    chunk.resize(kPayloadCapacity, 0xFF);
    packets.push_back(Packet(pid, start, chunk, PesOptions{}));
    start = false;
  }
  return packets;
}

std::vector<Bytes> TsWriter::Pat(
    const std::vector<std::pair<uint16_t, uint16_t>>& programs,
    uint8_t version) {
  const size_t length = 5 + (4 * programs.size()) + 4;
  Bytes section = {0x00,
                   static_cast<uint8_t>(0xB0U | (length >> 8U)),
                   static_cast<uint8_t>(length & 0xFFU),
                   0x00,
                   0x01,
                   static_cast<uint8_t>(0xC1U | (version << 1U)),
                   0x00,
                   0x00};
  for (const auto& [program, pid] : programs) {
    section.push_back(static_cast<uint8_t>(program >> 8U));
    section.push_back(static_cast<uint8_t>(program & 0xFFU));
    section.push_back(static_cast<uint8_t>(0xE0U | (pid >> 8U)));
    section.push_back(static_cast<uint8_t>(pid & 0xFFU));
  }
  AppendCrc(section);
  return Section(ts::kPatPid, section);
}

std::vector<Bytes> TsWriter::Pmt(uint16_t pmt_pid, uint16_t program,
                                 uint16_t pcr_pid,
                                 const std::vector<EsSpec>& streams,
                                 uint8_t version, bool corrupt_crc) {
  size_t length = 9 + 4;
  for (const EsSpec& stream : streams) {
    length += 5 + stream.descriptors.size();
  }
  Bytes section = {0x02,
                   static_cast<uint8_t>(0xB0U | (length >> 8U)),
                   static_cast<uint8_t>(length & 0xFFU),
                   static_cast<uint8_t>(program >> 8U),
                   static_cast<uint8_t>(program & 0xFFU),
                   static_cast<uint8_t>(0xC1U | (version << 1U)),
                   0x00,
                   0x00,
                   static_cast<uint8_t>(0xE0U | (pcr_pid >> 8U)),
                   static_cast<uint8_t>(pcr_pid & 0xFFU),
                   0xF0,
                   0x00};
  for (const EsSpec& stream : streams) {
    const size_t info = stream.descriptors.size();
    section.push_back(stream.stream_type);
    section.push_back(static_cast<uint8_t>(0xE0U | (stream.pid >> 8U)));
    section.push_back(static_cast<uint8_t>(stream.pid & 0xFFU));
    section.push_back(static_cast<uint8_t>(0xF0U | (info >> 8U)));
    section.push_back(static_cast<uint8_t>(info & 0xFFU));
    section.insert(section.end(), stream.descriptors.begin(),
                   stream.descriptors.end());
  }
  AppendCrc(section);
  if (corrupt_crc) {
    section.back() ^= 0x5A;
  }
  return Section(pmt_pid, section);
}

Bytes TsWriter::Packet(uint16_t pid, bool start, const Bytes& payload,
                       const PesOptions& adaptation) {
  Bytes field;
  const bool flagged = adaptation.random_access || adaptation.pcr.has_value() ||
                       adaptation.discontinuity;
  if (flagged) {
    field.push_back(
        static_cast<uint8_t>((adaptation.discontinuity ? 0x80U : 0U) |
                             (adaptation.random_access ? 0x40U : 0U) |
                             (adaptation.pcr.has_value() ? 0x10U : 0U)));
    if (adaptation.pcr.has_value()) {
      const uint64_t base = (*adaptation.pcr / 300) & kWrapMask;
      const uint64_t extension = *adaptation.pcr % 300;
      field.push_back(static_cast<uint8_t>(base >> 25U));
      field.push_back(static_cast<uint8_t>(base >> 17U));
      field.push_back(static_cast<uint8_t>(base >> 9U));
      field.push_back(static_cast<uint8_t>(base >> 1U));
      field.push_back(static_cast<uint8_t>(((base & 1U) << 7U) | 0x7EU |
                                           ((extension >> 8U) & 1U)));
      field.push_back(static_cast<uint8_t>(extension & 0xFFU));
    }
  }
  const size_t field_total = field.empty() ? 0 : field.size() + 1;
  const size_t stuffing = kPayloadCapacity - field_total - payload.size();
  bool has_field = flagged;
  if (stuffing > 0) {
    has_field = true;
    if (field.empty()) {
      if (stuffing > 1) {
        field.push_back(0);
        field.insert(field.end(), stuffing - 2, 0xFF);
      }
    } else {
      field.insert(field.end(), stuffing, 0xFF);
    }
  }
  Bytes packet = {0x47,
                  static_cast<uint8_t>((start ? 0x40U : 0U) | (pid >> 8U)),
                  static_cast<uint8_t>(pid & 0xFFU), 0};
  const auto control =
      static_cast<uint8_t>((has_field ? 2U : 0U) | (payload.empty() ? 0U : 1U));
  uint8_t& counter = continuity_.at(pid);
  packet[3] = static_cast<uint8_t>((control << 4U) | counter);
  if (!payload.empty()) {
    counter = static_cast<uint8_t>((counter + 1U) & 15U);
  }
  if (has_field) {
    packet.push_back(static_cast<uint8_t>(field.size()));
    packet.insert(packet.end(), field.begin(), field.end());
  }
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

std::vector<Bytes> TsWriter::Pes(uint16_t pid, const Bytes& payload,
                                 const PesOptions& options) {
  Bytes pes = {0, 0, 1, options.stream_id, 0, 0, 0x80};
  const bool has_dts = options.pts.has_value() && options.dts.has_value();
  uint8_t flags = 0;
  uint8_t header_length = 0;
  if (has_dts) {
    flags = 0xC0;
    header_length = 10;
  } else if (options.pts.has_value()) {
    flags = 0x80;
    header_length = 5;
  }
  pes.push_back(flags);
  pes.push_back(header_length);
  if (options.pts.has_value()) {
    const Bytes pts = Timestamp(has_dts ? 3 : 2, *options.pts);
    pes.insert(pes.end(), pts.begin(), pts.end());
  }
  if (has_dts) {
    const Bytes dts = Timestamp(1, *options.dts);
    pes.insert(pes.end(), dts.begin(), dts.end());
  }
  pes.insert(pes.end(), payload.begin(), payload.end());
  if (options.bounded && pes.size() - 6 <= 0xFFFF) {
    pes[4] = static_cast<uint8_t>((pes.size() - 6) >> 8U);
    pes[5] = static_cast<uint8_t>((pes.size() - 6) & 0xFFU);
  }
  const bool flagged =
      options.random_access || options.pcr.has_value() || options.discontinuity;
  const size_t first_field =
      flagged ? 2 + (options.pcr.has_value() ? 6 : 0) : 0;
  std::vector<Bytes> packets;
  size_t offset = 0;
  bool start = true;
  while (offset < pes.size() || start) {
    const size_t capacity = kPayloadCapacity - (start ? first_field : 0);
    const size_t count = std::min(capacity, pes.size() - offset);
    const Bytes chunk(pes.begin() + static_cast<ptrdiff_t>(offset),
                      pes.begin() + static_cast<ptrdiff_t>(offset + count));
    packets.push_back(
        Packet(pid, start, chunk, start ? options : PesOptions{}));
    offset += count;
    start = false;
  }
  return packets;
}

Bytes Flatten(const std::vector<Bytes>& packets) {
  Bytes result;
  for (const Bytes& packet : packets) {
    result.insert(result.end(), packet.begin(), packet.end());
  }
  return result;
}

Bytes H264AccessUnit(bool key, size_t filler, const Bytes& sei,
                     bool large_picture) {
  Bytes unit = {0, 0, 0, 1, 0x09, 0xF0};
  if (key) {
    const Bytes parameters =
        large_picture ? FromHex(
                            "00000001674d400cda05067e7c0440000003004000000c83"
                            "c50aa80000000168ef0fc8")
                      : FromHex(
                            "00000001674d400beca146fc9808800000030080000019078a"
                            "14cb0000000168ef8fc8");
    unit.insert(unit.end(), parameters.begin(), parameters.end());
  }
  unit.insert(unit.end(), sei.begin(), sei.end());
  const Bytes slice = {0,   0, 0, 1, static_cast<uint8_t>(key ? 0x65 : 0x41),
                       0x88};
  unit.insert(unit.end(), slice.begin(), slice.end());
  unit.insert(unit.end(), filler, 0x55);
  return unit;
}

Bytes H264CaptionSei() {
  const Bytes payload = {0xB5, 0x00, 0x31, 'G',  'A',  '9',  '4',  0x03, 0x42,
                         0xFF, 0xFC, 0x80, 0x80, 0xFD, 0x81, 0x82, 0xFF};
  Bytes unit = {0, 0, 0, 1, 0x06};
  const Bytes message = SeiMessage(payload);
  unit.insert(unit.end(), message.begin(), message.end());
  unit.push_back(0x80);
  return unit;
}

Bytes AdtsFrames(size_t count, size_t payload_bytes) {
  Bytes frames;
  const size_t length = 7 + payload_bytes;
  for (size_t index = 0; index < count; ++index) {
    frames.insert(
        frames.end(),
        {0xFF, 0xF1, 0x4C, static_cast<uint8_t>(0x80U | ((length >> 11U) & 3U)),
         static_cast<uint8_t>((length >> 3U) & 0xFFU),
         static_cast<uint8_t>(((length & 7U) << 5U) | 0x1FU), 0xFC});
    frames.insert(frames.end(), payload_bytes, static_cast<uint8_t>(index));
  }
  return frames;
}

Bytes Ac3Frames(size_t count) {
  Bytes frames;
  for (size_t index = 0; index < count; ++index) {
    Bytes frame = {0x0B, 0x77, 0x00, 0x00, 0x0A, 0x40, 0xE1};
    frame.resize(320, static_cast<uint8_t>(index));
    frames.insert(frames.end(), frame.begin(), frame.end());
  }
  return frames;
}

Bytes Id3Tag(size_t payload_bytes) {
  Bytes tag = {'I', 'D', '3', 4, 0, 0};
  tag.push_back(static_cast<uint8_t>((payload_bytes >> 21U) & 0x7FU));
  tag.push_back(static_cast<uint8_t>((payload_bytes >> 14U) & 0x7FU));
  tag.push_back(static_cast<uint8_t>((payload_bytes >> 7U) & 0x7FU));
  tag.push_back(static_cast<uint8_t>(payload_bytes & 0x7FU));
  tag.insert(tag.end(), payload_bytes, 0x33);
  return tag;
}

Synthetic BuildSynthetic(const SyntheticSpec& spec) {
  Synthetic result;
  TsWriter writer;
  std::vector<EsSpec> streams = {{0x1B, spec.video_pid, {}}};
  if (spec.audio) {
    streams.push_back({0x0F, spec.audio_pid, {}});
  }
  if (spec.id3) {
    streams.push_back({0x15, spec.id3_pid, {}});
  }
  auto append = [&result](const std::vector<Bytes>& packets) {
    result.packets.insert(result.packets.end(), packets.begin(), packets.end());
  };
  size_t audio_index = 0;
  for (size_t frame = 0; frame < spec.frames; ++frame) {
    const uint64_t dts = spec.start + (frame * kFrameTicks);
    if (frame % spec.psi_every == 0) {
      result.psi_positions.push_back(result.packets.size());
      append(writer.Pat({{1, spec.pmt_pid}}));
      append(writer.Pmt(spec.pmt_pid, 1, spec.video_pid, streams));
    }
    while (spec.audio && spec.start + (audio_index * kAudioPesTicks) <= dts) {
      PesOptions options;
      options.pts = spec.start + (audio_index * kAudioPesTicks);
      options.stream_id = 0xC0;
      options.bounded = true;
      append(writer.Pes(
          spec.audio_pid,
          AdtsFrames(kAudioFramesPerPes, 100 + ((audio_index % 5) * 10)),
          options));
      ++audio_index;
    }
    if (spec.id3 && frame % 25 == 0) {
      PesOptions options;
      options.pts = dts;
      options.stream_id = 0xFC;
      options.bounded = true;
      append(writer.Pes(spec.id3_pid, Id3Tag(20), options));
    }
    const bool key = frame % spec.gop == 0;
    PesOptions options;
    options.dts = dts;
    options.pts = dts + kVideoDelayTicks;
    options.random_access = key;
    options.bounded = spec.bounded_video;
    if (key) {
      options.pcr = dts * 300;
    }
    const Bytes sei =
        spec.captions && frame % 5 == 0 ? H264CaptionSei() : Bytes{};
    append(writer.Pes(spec.video_pid,
                      H264AccessUnit(key, spec.video_bytes, sei), options));
  }
  return result;
}

}
