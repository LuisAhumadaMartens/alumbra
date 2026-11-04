#include "umbra/demux/mp4_box.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/port_types.h"
#include "test_support.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/demux/mp4_sample_entry.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux::test {
namespace {

constexpr uint32_t kMoov = FourCc('m', 'o', 'o', 'v');

TEST(Mp4BoxTest, ParsesCompactLargeAndUuidHeaders) {
  const Bytes compact = Box("free", Bytes(5, 0));
  const absl::StatusOr<BoxHeader> a = ParseBoxHeader(compact);
  ASSERT_TRUE(a.ok());
  EXPECT_EQ(a->type, FourCc('f', 'r', 'e', 'e'));
  EXPECT_EQ(a->total_size, 13U);
  EXPECT_EQ(a->header_size, 8U);

  const Bytes large = LargeBox("mdat", Bytes(5, 0));
  EXPECT_EQ(BoxHeaderBytes(large), 16U);
  const absl::StatusOr<BoxHeader> b = ParseBoxHeader(large);
  ASSERT_TRUE(b.ok());
  EXPECT_EQ(b->total_size, 21U);
  EXPECT_EQ(b->header_size, 16U);

  Writer uuid;
  uuid.U32(40).Text("uuid").Zeros(16).Zeros(16);
  EXPECT_EQ(BoxHeaderBytes(uuid.Data()), 24U);
  const absl::StatusOr<BoxHeader> c = ParseBoxHeader(uuid.Data());
  ASSERT_TRUE(c.ok());
  EXPECT_EQ(c->header_size, 24U);
}

TEST(Mp4BoxTest, SizeToEndOfFileIsReportedAsZero) {
  Writer writer;
  writer.U32(0).Text("mdat");
  const absl::StatusOr<BoxHeader> header = ParseBoxHeader(writer.Data());
  ASSERT_TRUE(header.ok());
  EXPECT_EQ(header->total_size, 0U);
}

TEST(Mp4BoxTest, RejectsSizesSmallerThanTheHeader) {
  for (const uint32_t size : {2U, 7U}) {
    Writer writer;
    writer.U32(size).Text("free");
    EXPECT_EQ(ParseBoxHeader(writer.Data()).status().code(),
              absl::StatusCode::kDataLoss)
        << size;
  }
  Writer large;
  large.U32(1).Text("mdat").U64(15);
  EXPECT_EQ(ParseBoxHeader(large.Data()).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(Mp4BoxTest, RejectsTruncatedHeaders) {
  const Bytes large = LargeBox("mdat", Bytes());
  EXPECT_EQ(
      ParseBoxHeader(std::span<const uint8_t>(large).first(10)).status().code(),
      absl::StatusCode::kDataLoss);
}

TEST(Mp4BoxTest, AnnouncesTx3gAndWvttAsTextStreams) {
  SampleEntryContext context;
  context.track_id = 7;
  context.time_base = penumbra::Rational::Of(1, 1000);
  context.language = "en";
  for (const auto& [fourcc, codec] :
       {std::pair{FourCc('t', 'x', '3', 'g'), "tx3g"},
        std::pair{FourCc('w', 'v', 't', 't'), "wvtt"}}) {
    const auto entry = ParseSampleEntry(fourcc, {}, context);
    ASSERT_TRUE(entry.ok()) << entry.status();
    ASSERT_TRUE(entry->stream.has_value());
    EXPECT_EQ(entry->stream->track_id, 7U);
    EXPECT_EQ(entry->stream->type(), penumbra::StreamType::kText);
    EXPECT_EQ(entry->stream->codec_string, codec);
    EXPECT_EQ(entry->stream->language, "en");
  }
}

TEST(Mp4BoxTest, SplitsChildrenAndChecksTheirSizes) {
  const Bytes parent = Concat({Box("free", Bytes(3, 1)), Box("skip", Bytes())});
  const auto children = SplitChildren(parent, 1);
  ASSERT_TRUE(children.ok());
  ASSERT_EQ(children->size(), 2U);
  EXPECT_EQ((*children)[0].payload.size(), 3U);
  EXPECT_EQ((*children)[1].payload.size(), 0U);

  Bytes oversized = Box("free", Bytes(3, 1));
  oversized[3] = 0x40;
  EXPECT_EQ(SplitChildren(oversized, 1).status().code(),
            absl::StatusCode::kDataLoss);

  Bytes trailing = Box("free", Bytes());
  trailing.push_back(0);
  EXPECT_EQ(SplitChildren(trailing, 1).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(Mp4BoxTest, RejectsExcessiveNesting) {
  EXPECT_TRUE(SplitChildren(Box("free", Bytes()), kMaxBoxDepth).ok());
  EXPECT_EQ(
      SplitChildren(Box("free", Bytes()), kMaxBoxDepth + 1).status().code(),
      absl::StatusCode::kDataLoss);
}

TEST(Mp4BoxTest, KnowsTheTopLevelBoxTypes) {
  EXPECT_TRUE(IsKnownTopLevelBox(kMoov));
  EXPECT_TRUE(IsKnownTopLevelBox(FourCc('f', 't', 'y', 'p')));
  EXPECT_FALSE(IsKnownTopLevelBox(FourCc('R', 'I', 'F', 'F')));
}

TEST(Mp4EsdsTest, ParsesTheObjectTypeAndDecoderSpecificInfo) {
  Writer descriptor;
  descriptor.U32(0).U8(0x03).U8(25).U16(1).U8(0);
  descriptor.U8(0x04).U8(17).U8(0x40).U8(0x15).Zeros(3).U32(0).U32(0);
  descriptor.U8(0x05).U8(2).U8(0x12).U8(0x10);
  const absl::StatusOr<EsdsInfo> info = ParseEsds(descriptor.Data());
  ASSERT_TRUE(info.ok()) << info.status();
  EXPECT_EQ(info->object_type_indication, 0x40);
  EXPECT_EQ(info->decoder_specific_info, (Bytes{0x12, 0x10}));
}

TEST(Mp4EsdsTest, ParsesLongFormDescriptorLengths) {
  Writer descriptor;
  descriptor.U32(0).U8(0x03).U8(0x80).U8(0x80).U8(0x80).U8(25).U16(1).U8(0);
  descriptor.U8(0x04).U8(0x80).U8(17).U8(0x6B).U8(0x15).Zeros(3).U32(0).U32(0);
  const absl::StatusOr<EsdsInfo> info = ParseEsds(descriptor.Data());
  ASSERT_TRUE(info.ok()) << info.status();
  EXPECT_EQ(info->object_type_indication, 0x6B);
  EXPECT_TRUE(info->decoder_specific_info.empty());
}

TEST(Mp4EsdsTest, RejectsMalformedDescriptors) {
  EXPECT_FALSE(ParseEsds(Bytes{0, 0, 0, 0, 0x09, 0x00}).ok());
  EXPECT_FALSE(
      ParseEsds(Bytes{0, 0, 0, 0, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 1}).ok());
  EXPECT_FALSE(ParseEsds(Bytes{0, 0, 0}).ok());
}

size_t Locate(const Bytes& bytes, std::string_view type) {
  const auto found = std::ranges::search(bytes, type).begin();
  EXPECT_NE(found, bytes.end()) << type;
  return static_cast<size_t>(found - bytes.begin()) - 4;
}

void PutU32(Bytes& bytes, size_t offset, uint32_t value) {
  for (int index = 0; index < 4; ++index) {
    bytes[offset + static_cast<size_t>(index)] =
        static_cast<uint8_t>(value >> (24 - 8 * index));
  }
}

absl::Status Demux(const Bytes& bytes, Mp4DemuxerOptions options = {}) {
  RecordingOutput output;
  Mp4Demuxer demuxer(output, options);
  return DriveBytes(demuxer, bytes, 97).status;
}

Bytes ValidFile(bool moov_at_end = false) {
  ProgressiveSpec spec;
  spec.moov_at_end = moov_at_end;
  TrackSpec track;
  track.id = 1;
  track.timescale = 44100;
  track.codec = TestCodec::kAac;
  track.samples = std::vector<SampleSpec>(6, MakeSample(40, 1024, 0, true));
  spec.tracks = {track};
  return BuildProgressive(spec).Flatten();
}

TEST(Mp4MalformedTest, ValidFileIsAccepted) {
  EXPECT_TRUE(Demux(ValidFile()).ok());
  EXPECT_TRUE(Demux(ValidFile(true)).ok());
}

TEST(Mp4MalformedTest, RejectsFilesThatDoNotStartWithABox) {
  Bytes bytes = ValidFile();
  bytes[4] = 'R';
  bytes[5] = 'I';
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(Demux(Bytes()).code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(Demux(Bytes(100, 0xAA)).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsATruncatedMoov) {
  const Bytes bytes = ValidFile();
  const size_t moov = Locate(bytes, "moov");
  const Bytes cut(bytes.begin(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(moov + 50));
  EXPECT_EQ(Demux(cut).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsABoxSizeLargerThanTheFile) {
  Bytes bytes = ValidFile();
  PutU32(bytes, Locate(bytes, "moov"), 0x7FFFFFFF);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsBoxesBeyondTheBufferingLimit) {
  Bytes bytes = Ftyp();
  Writer large;
  large.U32(1).Text("moov").U64(uint64_t{1} << 40);
  bytes.insert(bytes.end(), large.Data().begin(), large.Data().end());
  bytes.insert(bytes.end(), 100, 0);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsBoxSizesThatOverflowTheFileOffset) {
  Bytes bytes = Ftyp();
  Writer large;
  large.U32(1).Text("free").U64(UINT64_MAX - 4);
  bytes.insert(bytes.end(), large.Data().begin(), large.Data().end());
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsABoxSmallerThanItsHeader) {
  Bytes bytes = Ftyp();
  Writer small;
  small.U32(3).Text("free");
  bytes.insert(bytes.end(), small.Data().begin(), small.Data().end());
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsAChildLargerThanItsParent) {
  Bytes bytes = ValidFile();
  PutU32(bytes, Locate(bytes, "trak"), 0x00FFFFFF);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsExcessiveBoxNesting) {
  Bytes nested = Box("free", Bytes());
  for (int level = 0; level < 40; ++level) {
    nested = Box("trak", nested);
  }
  const Bytes bytes = Concat({Ftyp(), Box("moov", nested)});
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsSampleCountsBeyondTheLimit) {
  Bytes bytes = ValidFile();
  const size_t stsz = Locate(bytes, "stsz");
  PutU32(bytes, stsz + 12, 1);
  PutU32(bytes, stsz + 16, 0xFFFFFFFF);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsSizeTablesLargerThanTheirBox) {
  Bytes bytes = ValidFile();
  PutU32(bytes, Locate(bytes, "stsz") + 16, 0x00FFFFFF);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsTimeTablesThatCoverTooFewSamples) {
  Bytes bytes = ValidFile();
  PutU32(bytes, Locate(bytes, "stts") + 16, 2);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsAnEmptyChunkMap) {
  Bytes bytes = ValidFile();
  PutU32(bytes, Locate(bytes, "stsc") + 12, 0);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsChunkOffsetsThatOverflow) {
  ProgressiveSpec spec;
  TrackSpec track;
  track.id = 1;
  track.timescale = 44100;
  track.co64 = true;
  track.samples = std::vector<SampleSpec>(3, MakeSample(40, 1024, 0, true));
  spec.tracks = {track};
  Bytes bytes = BuildProgressive(spec).Flatten();
  const size_t co64 = Locate(bytes, "co64");
  for (size_t index = 0; index < 8; ++index) {
    bytes[co64 + 16 + index] = 0xFF;
  }
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsAZeroTimescale) {
  Bytes bytes = ValidFile();
  PutU32(bytes, Locate(bytes, "mdhd") + 20, 0);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, RejectsSamplesPastTheEndOfTheFile) {
  Bytes bytes = ValidFile();
  bytes.resize(bytes.size() - 20);
  EXPECT_EQ(Demux(bytes).code(), absl::StatusCode::kDataLoss);
}

TEST(Mp4MalformedTest, EveryTruncationAndCorruptionIsHandled) {
  ProgressiveSpec spec;
  TrackSpec video;
  video.id = 1;
  video.timescale = 30000;
  video.codec = TestCodec::kAvc;
  video.use_ctts = true;
  video.samples = std::vector<SampleSpec>(4, MakeSample(20, 1001, 0, false));
  video.samples[0].sync = true;
  video.edits = {MakeEdit(100, 1001)};
  spec.tracks = {video};
  const Bytes bytes = BuildProgressive(spec).Flatten();
  for (size_t length = 0; length < bytes.size(); length += 2) {
    const Bytes prefix(bytes.begin(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(length));
    const absl::Status status = Demux(prefix);
    EXPECT_TRUE(status.ok() || status.code() == absl::StatusCode::kDataLoss)
        << length << " " << status;
  }
  for (size_t position = 0; position < bytes.size(); ++position) {
    for (const uint8_t value : {uint8_t{0}, uint8_t{0xFF}, uint8_t{0x80}}) {
      Bytes corrupt = bytes;
      corrupt[position] = value;
      const absl::Status status = Demux(corrupt);
      EXPECT_TRUE(status.ok() || status.code() == absl::StatusCode::kDataLoss ||
                  status.code() == absl::StatusCode::kUnimplemented)
          << position << " " << status;
    }
  }
}

}
}
