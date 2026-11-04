#include "mkv_builder.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "../codec/fixtures.h"
#include "mp4_builder.h"
#include "umbra/demux/ebml.h"

namespace umbra::demux::test {
namespace {

Bytes UintFixed(uint32_t id, uint64_t value, size_t length) {
  Bytes payload(length);
  for (size_t index = 0; index < length; ++index) {
    payload[length - 1 - index] = static_cast<uint8_t>(value >> (8 * index));
  }
  return Element(id, payload);
}

Bytes VintOfLength(uint64_t value, size_t length) {
  Bytes bytes(length);
  for (size_t index = 0; index < length; ++index) {
    bytes[length - 1 - index] = static_cast<uint8_t>(value >> (8 * index));
  }
  bytes[0] |= static_cast<uint8_t>(0x80U >> (length - 1));
  return bytes;
}

Bytes SignedVint(int64_t value) {
  for (size_t length = 1; length <= 8; ++length) {
    const auto bias =
        static_cast<int64_t>((uint64_t{1} << (7 * length - 1)) - 1);
    if (value >= -bias && value <= bias) {
      return VintOfLength(static_cast<uint64_t>(value + bias), length);
    }
  }
  return {};
}

Bytes LacingBytes(const MkvBlockSpec& block) {
  Bytes bytes;
  if (block.lacing == MkvLacing::kNone) {
    return bytes;
  }
  bytes.push_back(static_cast<uint8_t>(block.frames.size() - 1));
  if (block.lacing == MkvLacing::kXiph) {
    for (size_t index = 0; index + 1 < block.frames.size(); ++index) {
      size_t size = block.frames[index].size();
      while (size >= 255) {
        bytes.push_back(255);
        size -= 255;
      }
      bytes.push_back(static_cast<uint8_t>(size));
    }
  } else if (block.lacing == MkvLacing::kEbml && block.frames.size() > 1) {
    const Bytes first = EbmlSize(block.frames[0].size());
    bytes.insert(bytes.end(), first.begin(), first.end());
    for (size_t index = 1; index + 1 < block.frames.size(); ++index) {
      const Bytes diff =
          SignedVint(static_cast<int64_t>(block.frames[index].size()) -
                     static_cast<int64_t>(block.frames[index - 1].size()));
      bytes.insert(bytes.end(), diff.begin(), diff.end());
    }
  }
  return bytes;
}

Bytes BlockPayload(const MkvBlockSpec& block, bool with_key_flag) {
  Writer writer;
  writer.Add(EbmlSize(block.track));
  writer.U16(static_cast<uint16_t>(block.relative_ticks));
  uint32_t flags = with_key_flag && block.key ? 0x80U : 0U;
  if (block.lacing == MkvLacing::kXiph) {
    flags |= 0x02U;
  } else if (block.lacing == MkvLacing::kFixed) {
    flags |= 0x04U;
  } else if (block.lacing == MkvLacing::kEbml) {
    flags |= 0x06U;
  }
  writer.U8(flags);
  writer.Add(LacingBytes(block));
  for (const Bytes& frame : block.frames) {
    writer.Add(frame);
  }
  return writer.Data();
}

std::string_view CodecIdOf(MkvTestCodec codec) {
  switch (codec) {
    case MkvTestCodec::kVp8:
      return "V_VP8";
    case MkvTestCodec::kVp9:
      return "V_VP9";
    case MkvTestCodec::kAv1:
      return "V_AV1";
    case MkvTestCodec::kOpus:
      return "A_OPUS";
    case MkvTestCodec::kAac:
      return "A_AAC";
    case MkvTestCodec::kFlac:
      return "A_FLAC";
    case MkvTestCodec::kAc3:
      return "A_AC3";
    case MkvTestCodec::kMp3:
      return "A_MPEG/L3";
    case MkvTestCodec::kAss:
      return "S_TEXT/ASS";
    case MkvTestCodec::kSsa:
      return "S_TEXT/SSA";
    case MkvTestCodec::kUnsupported:
      return "V_UNKNOWN";
    case MkvTestCodec::kVorbis:
      return "A_VORBIS";
    case MkvTestCodec::kTheora:
      return "V_THEORA";
  }
  return "";
}

bool IsVideo(MkvTestCodec codec) {
  return codec == MkvTestCodec::kVp8 || codec == MkvTestCodec::kVp9 ||
         codec == MkvTestCodec::kAv1 || codec == MkvTestCodec::kTheora ||
         codec == MkvTestCodec::kUnsupported;
}

uint64_t TrackType(MkvTestCodec codec) {
  if (codec == MkvTestCodec::kAss || codec == MkvTestCodec::kSsa) {
    return 17;
  }
  return IsVideo(codec) ? 1 : 2;
}

Bytes CodecPrivate(const MkvTrackSpec& track) {
  if (!track.codec_private.empty()) {
    return track.codec_private;
  }
  if (track.codec == MkvTestCodec::kAv1) {
    return FromHex(codec::test_data::kAv1C);
  }
  if (track.codec == MkvTestCodec::kOpus) {
    return OpusHead(312, static_cast<uint8_t>(track.channels));
  }
  if (track.codec == MkvTestCodec::kVorbis) {
    return {2,   7,   7,   0x01, 'v', 'o',  'r', 'b', 'i', 's', 0x03, 'v',
            'o', 'r', 'b', 'i',  's', 0x05, 'v', 'o', 'r', 'b', 'i',  's'};
  }
  if (track.codec == MkvTestCodec::kTheora) {
    return {2,   7,   7,   0x80, 't', 'h',  'e', 'o', 'r', 'a', 0x81, 't',
            'h', 'e', 'o', 'r',  'a', 0x82, 't', 'h', 'e', 'o', 'r',  'a'};
  }
  if (track.codec == MkvTestCodec::kAac) {
    return FromHex(codec::test_data::kAscAacMono);
  }
  if (track.codec == MkvTestCodec::kFlac) {
    Bytes bytes = {'f', 'L', 'a', 'C'};
    const Bytes dfla = FromHex(codec::test_data::kDfLa);
    bytes.insert(bytes.end(), dfla.begin() + 4, dfla.end());
    return bytes;
  }
  return {};
}

Bytes ContentEncodings(const MkvTrackSpec& track) {
  if (track.header_strip.empty() && !track.encrypted && !track.zlib) {
    return {};
  }
  Bytes encoding = UintElement(kIdContentEncodingScope, 1);
  if (track.encrypted) {
    const Bytes type = UintElement(kIdContentEncodingType, 1);
    encoding.insert(encoding.end(), type.begin(), type.end());
    const Bytes encryption = Element(0x5035, UintElement(0x47E1, 5));
    encoding.insert(encoding.end(), encryption.begin(), encryption.end());
  } else {
    Bytes compression = UintElement(kIdContentCompAlgo, track.zlib ? 0 : 3);
    if (!track.zlib) {
      const Bytes settings =
          Element(kIdContentCompSettings, track.header_strip);
      compression.insert(compression.end(), settings.begin(), settings.end());
    }
    const Bytes wrapped = Element(kIdContentCompression, compression);
    encoding.insert(encoding.end(), wrapped.begin(), wrapped.end());
  }
  return Element(kIdContentEncodings, Element(kIdContentEncoding, encoding));
}

Bytes TrackEntry(const MkvTrackSpec& track) {
  Writer writer;
  writer.Add(UintElement(kIdTrackNumber, track.number));
  writer.Add(UintElement(0x73C5, track.number));
  writer.Add(UintElement(kIdTrackType, TrackType(track.codec)));
  writer.Add(StringElement(kIdCodecId, CodecIdOf(track.codec)));
  const Bytes private_data = CodecPrivate(track);
  if (!private_data.empty()) {
    writer.Add(Element(kIdCodecPrivate, private_data));
  }
  if (track.default_duration_ns.has_value()) {
    writer.Add(UintElement(kIdDefaultDuration, *track.default_duration_ns));
  }
  if (track.codec_delay_ns != 0) {
    writer.Add(UintElement(kIdCodecDelay, track.codec_delay_ns));
  }
  if (track.seek_pre_roll_ns != 0) {
    writer.Add(UintElement(kIdSeekPreRoll, track.seek_pre_roll_ns));
  }
  if (IsVideo(track.codec)) {
    Writer video;
    video.Add(UintElement(kIdPixelWidth, track.width));
    video.Add(UintElement(kIdPixelHeight, track.height));
    writer.Add(Element(kIdVideo, video.Data()));
  } else {
    Writer audio;
    audio.Add(FloatElement(kIdSamplingFrequency, 48000.0));
    audio.Add(UintElement(kIdChannels, track.channels));
    writer.Add(Element(kIdAudio, audio.Data()));
  }
  writer.Add(ContentEncodings(track));
  return Element(kIdTrackEntry, writer.Data());
}

Bytes SeekHeadElement(uint64_t cues_position) {
  Writer entry;
  entry.Add(Element(kIdSeekId, EbmlId(kIdCues)));
  entry.Add(UintFixed(kIdSeekPosition, cues_position, 8));
  return Element(kIdSeekHead, Element(kIdSeek, entry.Data()));
}

Bytes CuesElement(const MkvFileSpec& spec,
                  const std::vector<uint64_t>& cluster_positions) {
  Writer cues;
  for (size_t index = 0; index < spec.clusters.size(); ++index) {
    Writer positions;
    positions.Add(UintFixed(kIdCueTrack, spec.tracks.front().number, 1));
    positions.Add(
        UintFixed(kIdCueClusterPosition, cluster_positions[index], 8));
    Writer point;
    point.Add(UintFixed(kIdCueTime, spec.clusters[index].timestamp, 8));
    point.Add(Element(kIdCueTrackPositions, positions.Data()));
    cues.Add(Element(kIdCuePoint, point.Data()));
  }
  return Element(kIdCues, cues.Data());
}

}

Bytes EbmlId(uint32_t id) {
  Bytes bytes;
  const int length = (32 - std::countl_zero(id) + 7) / 8;
  for (int index = length - 1; index >= 0; --index) {
    bytes.push_back(static_cast<uint8_t>(id >> (8 * index)));
  }
  return bytes;
}

Bytes EbmlSize(uint64_t size) {
  for (size_t length = 1; length <= 8; ++length) {
    if (size < (uint64_t{1} << (7 * length)) - 1) {
      return VintOfLength(size, length);
    }
  }
  return {};
}

Bytes Element(uint32_t id, const Bytes& payload) {
  Bytes bytes = EbmlId(id);
  const Bytes size = EbmlSize(payload.size());
  bytes.insert(bytes.end(), size.begin(), size.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

Bytes UnknownSizeElement(uint32_t id, const Bytes& payload) {
  Bytes bytes = EbmlId(id);
  const Bytes size = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bytes.insert(bytes.end(), size.begin(), size.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

Bytes UintElement(uint32_t id, uint64_t value) {
  size_t length = 1;
  while (length < 8 && (value >> (8 * length)) != 0) {
    ++length;
  }
  return UintFixed(id, value, length);
}

Bytes SintElement(uint32_t id, int64_t value) {
  size_t length = 1;
  while (length < 8) {
    const int64_t limit = int64_t{1} << ((8 * length) - 1);
    if (value >= -limit && value < limit) {
      break;
    }
    ++length;
  }
  return UintFixed(id, static_cast<uint64_t>(value), length);
}

Bytes FloatElement(uint32_t id, double value) {
  return UintFixed(id, std::bit_cast<uint64_t>(value), 8);
}

Bytes StringElement(uint32_t id, std::string_view value) {
  return Element(id, Bytes(value.begin(), value.end()));
}

Bytes SimpleBlockElement(const MkvBlockSpec& block) {
  return Element(kIdSimpleBlock, BlockPayload(block, true));
}

Bytes BlockGroupElement(const MkvBlockSpec& block) {
  Writer group;
  group.Add(Element(kIdBlock, BlockPayload(block, false)));
  if (block.duration.has_value()) {
    group.Add(UintElement(kIdBlockDuration, *block.duration));
  }
  if (block.reference) {
    group.Add(SintElement(kIdReferenceBlock, -1));
  }
  if (block.discard_padding.has_value()) {
    group.Add(SintElement(kIdDiscardPadding, *block.discard_padding));
  }
  return Element(kIdBlockGroup, group.Data());
}

Bytes ClusterElement(const MkvClusterSpec& cluster) {
  Writer body;
  if (!cluster.omit_timestamp) {
    body.Add(UintElement(kIdClusterTimestamp, cluster.timestamp));
  }
  for (const MkvBlockSpec& block : cluster.blocks) {
    body.Add(block.group ? BlockGroupElement(block)
                         : SimpleBlockElement(block));
  }
  body.Add(cluster.trailing_raw);
  return cluster.unknown_size ? UnknownSizeElement(kIdCluster, body.Data())
                              : Element(kIdCluster, body.Data());
}

Bytes TracksElement(const std::vector<MkvTrackSpec>& tracks) {
  Writer writer;
  for (const MkvTrackSpec& track : tracks) {
    writer.Add(TrackEntry(track));
  }
  return Element(kIdTracks, writer.Data());
}

Bytes InfoElement(const MkvFileSpec& spec) {
  Writer writer;
  writer.Add(UintElement(kIdTimestampScale, spec.timestamp_scale));
  if (spec.duration.has_value()) {
    writer.Add(FloatElement(kIdDuration, *spec.duration));
  }
  return Element(kIdInfo, writer.Data());
}

Bytes OpusHead(uint16_t pre_skip, uint8_t channels) {
  Writer writer;
  writer.Text("OpusHead").U8(1).U8(channels);
  writer.U8(pre_skip & 0xFF).U8(pre_skip >> 8);
  writer.U8(0x80).U8(0xBB).U8(0).U8(0);
  writer.U16(0).U8(0);
  return writer.Data();
}

Bytes FrameBytes(uint64_t track, size_t index, size_t size) {
  return SampleBytes(static_cast<uint32_t>(track), index,
                     static_cast<uint32_t>(size));
}

BuiltMkv BuildMkv(const MkvFileSpec& spec) {
  Writer header;
  header.Add(UintElement(kIdEbmlReadVersion, 1));
  header.Add(UintElement(kIdEbmlMaxIdLength, 4));
  header.Add(UintElement(kIdEbmlMaxSizeLength, 8));
  header.Add(StringElement(kIdDocType, spec.doc_type));
  header.Add(UintElement(kIdDocTypeReadVersion, 2));
  const Bytes info = InfoElement(spec);
  const Bytes tracks = spec.omit_tracks ? Bytes() : TracksElement(spec.tracks);
  std::vector<Bytes> clusters;
  clusters.reserve(spec.clusters.size());
  for (const MkvClusterSpec& cluster : spec.clusters) {
    clusters.push_back(ClusterElement(cluster));
  }
  const size_t seek_head_size = spec.seek_head ? SeekHeadElement(0).size() : 0;
  const size_t front = seek_head_size + info.size() + tracks.size() +
                       spec.junk_before_clusters.size();
  uint64_t cues_size = 0;
  if (spec.cues) {
    cues_size =
        CuesElement(spec, std::vector<uint64_t>(spec.clusters.size())).size();
  }
  uint64_t position = front + (spec.cues && !spec.cues_at_end ? cues_size : 0);
  std::vector<uint64_t> cluster_positions;
  for (const Bytes& cluster : clusters) {
    cluster_positions.push_back(position);
    position += cluster.size();
  }
  const uint64_t cues_position =
      spec.cues_at_end ? position : static_cast<uint64_t>(front);
  Writer body;
  if (spec.seek_head) {
    body.Add(SeekHeadElement(spec.cues ? cues_position : 0));
  }
  if (spec.tracks_before_info) {
    body.Add(tracks);
    body.Add(info);
  } else {
    body.Add(info);
    body.Add(tracks);
  }
  body.Add(spec.junk_before_clusters);
  if (spec.cues && !spec.cues_at_end) {
    body.Add(CuesElement(spec, cluster_positions));
  }
  for (const Bytes& cluster : clusters) {
    body.Add(cluster);
  }
  if (spec.cues && spec.cues_at_end) {
    body.Add(CuesElement(spec, cluster_positions));
  }
  BuiltMkv built;
  if (!spec.omit_header) {
    built.bytes = Element(kIdEbml, header.Data());
  }
  const Bytes segment = spec.unknown_segment_size
                            ? UnknownSizeElement(kIdSegment, body.Data())
                            : Element(kIdSegment, body.Data());
  const size_t header_size = segment.size() - body.Data().size();
  built.segment_data_start = built.bytes.size() + header_size;
  built.bytes.insert(built.bytes.end(), segment.begin(), segment.end());
  for (const uint64_t cluster_position : cluster_positions) {
    built.cluster_offsets.push_back(built.segment_data_start +
                                    cluster_position);
  }
  built.first_cluster_offset =
      built.cluster_offsets.empty() ? 0 : built.cluster_offsets.front();
  built.cues_offset = built.segment_data_start + cues_position;
  return built;
}

}
