#include "umbra/demux/ts_packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/stream_info.h"
#include "ts_builder.h"
#include "umbra/demux/caption_extractor.h"
#include "umbra/demux/ts_framing.h"

namespace umbra::demux::ts {
namespace {

using test::Bytes;

TEST(TsCrcTest, MatchesTheKnownMpegCheckValue) {
  const Bytes text = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(Crc32Mpeg(text), 0x0376E6E7U);
}

TEST(TsCrcTest, ASectionWithItsCrcAppendedChecksToZero) {
  test::TsWriter writer;
  const std::vector<Bytes> packets = writer.Pat({{1, 0x100}});
  ASSERT_EQ(packets.size(), 1U);
  const std::span<const uint8_t> section =
      std::span<const uint8_t>(packets[0]).subspan(5, 12 + 4);
  EXPECT_EQ(Crc32Mpeg(section), 0U);
}

TEST(TsPacketTest, ParsesHeaderAdaptationAndPcr) {
  test::TsWriter writer;
  test::PesOptions options;
  options.pts = 90000;
  options.random_access = true;
  options.discontinuity = true;
  options.pcr = (27000000ULL * 3) + 17;
  const std::vector<Bytes> packets = writer.Pes(0x101, Bytes(10, 1), options);
  ASSERT_EQ(packets.size(), 1U);
  const std::optional<PacketHeader> header = ParsePacket(packets[0]);
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->pid, 0x101);
  EXPECT_TRUE(header->payload_unit_start);
  EXPECT_TRUE(header->has_payload);
  EXPECT_TRUE(header->adaptation.random_access);
  EXPECT_TRUE(header->adaptation.discontinuity);
  ASSERT_TRUE(header->adaptation.pcr_27mhz.has_value());
  EXPECT_EQ(*header->adaptation.pcr_27mhz, (27000000ULL * 3) + 17);
  EXPECT_EQ(header->payload.size(), 24U);
}

TEST(TsPacketTest, RejectsBadSyncReservedControlAndOversizedAdaptation) {
  Bytes packet(188, 0xFF);
  EXPECT_FALSE(ParsePacket(packet).has_value());
  packet = {0x47, 0x01, 0x01, 0x00};
  packet.resize(188, 0);
  EXPECT_FALSE(ParsePacket(packet).has_value());
  packet[3] = 0x30;
  packet[4] = 200;
  EXPECT_FALSE(ParsePacket(packet).has_value());
  packet[3] = 0x30;
  packet[4] = 183;
  EXPECT_TRUE(ParsePacket(packet).has_value());
  EXPECT_FALSE(ParsePacket(Bytes(100, 0x47)).has_value());
}

TEST(TsPacketTest, ReadsTheTransportErrorAndScramblingFlags) {
  Bytes packet = {0x47, 0x81, 0x01, 0xD0};
  packet.resize(188, 0);
  const std::optional<PacketHeader> header = ParsePacket(packet);
  ASSERT_TRUE(header.has_value());
  EXPECT_TRUE(header->transport_error);
  EXPECT_EQ(header->scrambling, 3);
  EXPECT_EQ(header->payload.size(), 184U);
}

TEST(TsSectionTest, ReassemblesASectionAcrossPackets) {
  test::TsWriter writer;
  std::vector<test::EsSpec> streams;
  streams.reserve(60);
  for (uint16_t index = 0; index < 60; ++index) {
    streams.push_back({0x1B, static_cast<uint16_t>(0x100 + index), {}});
  }
  const std::vector<Bytes> packets = writer.Pmt(0x20, 1, 0x100, streams);
  ASSERT_GE(packets.size(), 2U);
  SectionAssembler assembler;
  std::vector<Section> sections;
  for (const Bytes& packet : packets) {
    const std::optional<PacketHeader> header = ParsePacket(packet);
    ASSERT_TRUE(header.has_value());
    assembler.Feed(header->payload, header->payload_unit_start, sections);
  }
  ASSERT_EQ(sections.size(), 1U);
  EXPECT_TRUE(sections[0].crc_ok);
  const std::optional<Pmt> pmt = ParsePmt(sections[0].bytes);
  ASSERT_TRUE(pmt.has_value());
  EXPECT_EQ(pmt->streams.size(), 60U);
  EXPECT_EQ(pmt->program_number, 1);
  EXPECT_EQ(pmt->pcr_pid, 0x100);
}

TEST(TsSectionTest, SplitsTwoSectionsInOnePacketAndFlagsABadCrc) {
  test::TsWriter writer;
  const Bytes first = writer.Pat({{1, 0x100}})[0];
  const Bytes second = writer.Pat({{2, 0x200}}, 1)[0];
  Bytes payload(first.begin() + 4, first.begin() + 4 + 1 + 16);
  payload.insert(payload.end(), second.begin() + 5, second.begin() + 5 + 16);
  payload.resize(184, 0xFF);
  SectionAssembler assembler;
  std::vector<Section> sections;
  assembler.Feed(payload, true, sections);
  ASSERT_EQ(sections.size(), 2U);
  EXPECT_TRUE(sections[0].crc_ok);
  EXPECT_TRUE(sections[1].crc_ok);
  EXPECT_EQ(ParsePat(sections[1].bytes)->programs[0].pid, 0x200);
  payload[10] ^= 0x01;
  sections.clear();
  assembler.Feed(payload, true, sections);
  ASSERT_GE(sections.size(), 1U);
  EXPECT_FALSE(sections[0].crc_ok);
}

TEST(TsSectionTest, DropsAContinuationWithoutAStartAndOversizedSections) {
  SectionAssembler assembler;
  std::vector<Section> sections;
  assembler.Feed(Bytes(184, 0x11), false, sections);
  EXPECT_TRUE(sections.empty());
  Bytes payload = {0x00, 0x00, 0xBF, 0xFF};
  payload.resize(184, 0x22);
  assembler.Feed(payload, true, sections);
  EXPECT_TRUE(sections.empty());
  EXPECT_EQ(assembler.OversizedSections(), 1U);
  assembler.Feed(Bytes{0xFF}, true, sections);
  EXPECT_TRUE(sections.empty());
}

TEST(TsTableTest, ReadsDescriptorsAndRejectsMalformedTables) {
  test::TsWriter writer;
  const Bytes descriptors = {0x6A, 0x01, 0x00, 0x0A, 0x04, 'e', 'n', 'g',
                             0x00, 0x05, 0x04, 'E',  'A',  'C', '3'};
  const std::vector<Bytes> packets =
      writer.Pmt(0x20, 1, 0x101, {{0x06, 0x101, descriptors}});
  SectionAssembler assembler;
  std::vector<Section> sections;
  const std::optional<PacketHeader> header = ParsePacket(packets[0]);
  assembler.Feed(header->payload, true, sections);
  ASSERT_EQ(sections.size(), 1U);
  const std::optional<Pmt> pmt = ParsePmt(sections[0].bytes);
  ASSERT_TRUE(pmt.has_value());
  ASSERT_EQ(pmt->streams.size(), 1U);
  EXPECT_TRUE(pmt->streams[0].descriptors.ac3);
  EXPECT_TRUE(pmt->streams[0].descriptors.eac3);
  EXPECT_EQ(pmt->streams[0].descriptors.language, "eng");
  Bytes broken = sections[0].bytes;
  broken[0] = 0x00;
  EXPECT_FALSE(ParsePmt(broken).has_value());
  broken = sections[0].bytes;
  broken.resize(broken.size() - 1);
  EXPECT_FALSE(ParsePmt(broken).has_value());
  EXPECT_FALSE(ParsePat(sections[0].bytes).has_value());
  EXPECT_FALSE(ParsePmt(Bytes{}).has_value());
}

TEST(TsPesTest, ParsesPtsAndDtsAndRejectsBadMarkers) {
  test::TsWriter writer;
  test::PesOptions options;
  options.pts = 0x1FFFFFFFFULL;
  options.dts = 0x12345;
  const std::vector<Bytes> packets = writer.Pes(0x101, Bytes(5, 9), options);
  const std::optional<PacketHeader> header = ParsePacket(packets[0]);
  ASSERT_TRUE(header.has_value());
  const std::optional<PesHeader> pes = ParsePesHeader(header->payload);
  ASSERT_TRUE(pes.has_value());
  EXPECT_EQ(pes->stream_id, 0xE0);
  EXPECT_EQ(*pes->pts, 0x1FFFFFFFFULL);
  EXPECT_EQ(*pes->dts, 0x12345U);
  EXPECT_EQ(pes->payload_offset, 19U);
  Bytes damaged(header->payload.begin(), header->payload.end());
  damaged[9] &= 0xFE;
  EXPECT_FALSE(ParsePesHeader(damaged).has_value());
  damaged = Bytes(header->payload.begin(), header->payload.end());
  damaged[7] = 0x40;
  EXPECT_FALSE(ParsePesHeader(damaged).has_value());
  damaged = Bytes(header->payload.begin(), header->payload.end());
  damaged[0] = 1;
  EXPECT_FALSE(ParsePesHeader(damaged).has_value());
  damaged = Bytes(header->payload.begin(), header->payload.end());
  damaged[8] = 255;
  EXPECT_FALSE(ParsePesHeader(damaged).has_value());
}

TEST(TsPesTest, StreamsWithoutAHeaderHaveNoTimestamps) {
  const Bytes padding = {0, 0, 1, 0xBE, 0, 3, 0xFF, 0xFF, 0xFF};
  const std::optional<PesHeader> pes = ParsePesHeader(padding);
  ASSERT_TRUE(pes.has_value());
  EXPECT_EQ(pes->payload_offset, 6U);
  EXPECT_FALSE(pes->pts.has_value());
  EXPECT_FALSE(StreamIdHasPesHeader(0xBF));
  EXPECT_TRUE(StreamIdHasPesHeader(0xFC));
}

TEST(TsUnwrapTest, FollowsTheNearestCandidateAcrossTheWrap) {
  TimestampUnwrapper unwrapper;
  const auto wrap = static_cast<int64_t>(kTimestampWrap);
  EXPECT_EQ(unwrapper.Unwrap(wrap - 7200), wrap - 7200);
  EXPECT_EQ(unwrapper.Unwrap(wrap - 3600), wrap - 3600);
  EXPECT_EQ(unwrapper.Unwrap(0), wrap);
  EXPECT_EQ(unwrapper.Unwrap(3600), wrap + 3600);
  EXPECT_EQ(unwrapper.Unwrap(wrap - 3600), wrap - 3600);
  EXPECT_EQ(unwrapper.Unwrap(wrap - 7200), wrap - 7200);
}

TEST(TsUnwrapTest, ContinuesPastSeveralWrapsAndSeedsFromAReference) {
  TimestampUnwrapper unwrapper;
  const auto wrap = static_cast<int64_t>(kTimestampWrap);
  int64_t expected = 100;
  for (int step = 0; step < 40; ++step) {
    EXPECT_EQ(
        unwrapper.Unwrap(static_cast<uint64_t>(expected) % kTimestampWrap),
        expected);
    expected += wrap / 3;
  }
  unwrapper.Seed((5 * wrap) + 10);
  EXPECT_EQ(unwrapper.Unwrap(20), (5 * wrap) + 20);
  unwrapper.Reset();
  EXPECT_EQ(unwrapper.Unwrap(20), 20);
}

TEST(TsFramingTest, SplitsAdtsFramesAndFlagsGarbage) {
  Bytes data = test::AdtsFrames(3, 50);
  AudioSplit split = SplitAudio(EsKind::kAdts, data);
  ASSERT_EQ(split.frames.size(), 3U);
  EXPECT_FALSE(split.lost_sync);
  EXPECT_EQ(split.frames[1].offset, 57U);
  EXPECT_EQ(split.frames[0].samples, 1024U);
  EXPECT_EQ(split.frames[0].sample_rate, 48000);
  data.insert(data.begin(), {1, 2, 3});
  split = SplitAudio(EsKind::kAdts, data);
  EXPECT_EQ(split.frames.size(), 3U);
  EXPECT_TRUE(split.lost_sync);
  data.resize(data.size() - 10);
  split = SplitAudio(EsKind::kAdts, data);
  EXPECT_EQ(split.frames.size(), 2U);
  EXPECT_TRUE(split.lost_sync);
}

TEST(TsFramingTest, SplitsAc3AndBuildsItsConfiguration) {
  const Bytes data = test::Ac3Frames(3);
  const AudioSplit split = SplitAudio(EsKind::kAc3, data);
  ASSERT_EQ(split.frames.size(), 3U);
  EXPECT_EQ(split.frames[2].offset, 640U);
  EXPECT_EQ(split.frames[0].size, 320U);
  const auto info = BuildAudioInfo(
      EsKind::kAc3, std::span<const uint8_t>(data).first(320), 7, "eng");
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info->codec, penumbra::CodecId::kAc3);
  EXPECT_EQ(info->language, "eng");
  EXPECT_EQ(info->codec_config.size(), 3U);
}

TEST(TsFramingTest, SplitsMpegAudioFrames) {
  Bytes frame = {0xFF, 0xFB, 0x90, 0x00};
  frame.resize(417, 0x11);
  Bytes data = frame;
  data.insert(data.end(), frame.begin(), frame.end());
  const AudioSplit split = SplitAudio(EsKind::kMpegAudio, data);
  ASSERT_EQ(split.frames.size(), 2U);
  EXPECT_EQ(split.frames[0].size, 417U);
  EXPECT_EQ(split.frames[0].samples, 1152U);
  EXPECT_EQ(split.frames[0].sample_rate, 44100);
  const auto info = BuildAudioInfo(EsKind::kMpegAudio, frame, 3, "");
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info->codec, penumbra::CodecId::kMp3);
}

TEST(TsFramingTest, ScansAnnexBForKeyFramesSeiAndParameterSets) {
  const Bytes key = test::H264AccessUnit(true, 10, test::H264CaptionSei());
  const VideoScan scan = ScanVideo(CaptionCodec::kH264, key);
  EXPECT_TRUE(scan.key_frame);
  EXPECT_TRUE(scan.has_sei);
  EXPECT_TRUE(scan.has_parameter_sets);
  const VideoScan plain =
      ScanVideo(CaptionCodec::kH264, test::H264AccessUnit(false, 10));
  EXPECT_FALSE(plain.key_frame);
  EXPECT_FALSE(plain.has_sei);
  EXPECT_FALSE(ScanVideo(CaptionCodec::kH264, Bytes{1, 2, 3}).key_frame);
  const Bytes hevc = {0, 0, 1, 0x26, 0x01, 0xAA};
  EXPECT_TRUE(ScanVideo(CaptionCodec::kHevc, hevc).key_frame);
}

TEST(TsFramingTest, BuildsVideoInformationFromParameterSets) {
  const Bytes key = test::H264AccessUnit(true, 10);
  const auto info = BuildVideoInfo(CaptionCodec::kH264, key, 0x101, "und");
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info->codec, penumbra::CodecId::kH264);
  EXPECT_EQ(info->codec_string, "avc1.4d400b");
  EXPECT_EQ(info->time_base, kTimeBase);
  const auto& video = std::get<penumbra::VideoInfo>(info->format);
  EXPECT_EQ(video.coded_width, 160);
  EXPECT_FALSE(BuildVideoInfo(CaptionCodec::kH264,
                              test::H264AccessUnit(false, 10), 1, "")
                   .ok());
}

}
}
