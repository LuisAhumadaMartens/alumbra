#include "umbra/source/source_type.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace umbra::source {
namespace {

SniffHints Hints(std::string extension = {}, std::string content_type = {}) {
  return {.extension = std::move(extension),
          .content_type = std::move(content_type)};
}

std::vector<uint8_t> Bytes(std::string_view text) {
  return {text.begin(), text.end()};
}

SniffResult Sniff(const std::vector<uint8_t>& bytes, bool end_of_source = true,
                  const SniffHints& hints = {}) {
  return SniffSourceType(bytes, end_of_source, hints);
}

void ExpectDecided(const SniffResult& result, SourceType type,
                   TypeSignal signal) {
  ASSERT_EQ(result.outcome, SniffOutcome::kDecided);
  EXPECT_EQ(result.type, type);
  EXPECT_EQ(result.signal, signal);
}

std::vector<uint8_t> TransportStream(size_t packets) {
  std::vector<uint8_t> bytes(packets * 188, 0);
  for (size_t i = 0; i < packets; ++i) {
    bytes[i * 188] = 0x47;
  }
  return bytes;
}

std::vector<uint8_t> Mp4(std::string_view brand = "ftyp") {
  std::vector<uint8_t> bytes = {0, 0, 0, 24};
  bytes.insert(bytes.end(), brand.begin(), brand.end());
  bytes.resize(24, 0);
  return bytes;
}

TEST(SourceTypeTest, NamesRoundTrip) {
  for (const SourceType type :
       {SourceType::kHls, SourceType::kDash, SourceType::kMp4,
        SourceType::kMatroska, SourceType::kMpegTs, SourceType::kFlv}) {
    EXPECT_EQ(ParseSourceType(SourceTypeName(type)), type);
  }
  EXPECT_EQ(ParseSourceType("auto"), std::nullopt);
  EXPECT_EQ(ParseSourceType("HLS"), std::nullopt);
  EXPECT_TRUE(IsAdaptive(SourceType::kHls));
  EXPECT_TRUE(IsAdaptive(SourceType::kDash));
  EXPECT_FALSE(IsAdaptive(SourceType::kMp4));
  EXPECT_EQ(TypeSignalName(TypeSignal::kContentType), "content_type");
  EXPECT_EQ(TypeSignalName(TypeSignal::kExplicitType), "explicit_type");
}

TEST(SniffTest, RecognisesEverySignature) {
  ExpectDecided(Sniff(Bytes("#EXTM3U\n#EXT-X-VERSION:3\n")), SourceType::kHls,
                TypeSignal::kSignature);
  ExpectDecided(Sniff(Bytes("\xEF\xBB\xBF  \n#EXTM3U\n")), SourceType::kHls,
                TypeSignal::kSignature);
  ExpectDecided(Sniff(Bytes("<?xml version=\"1.0\"?>\n<MPD xmlns=\"x\">")),
                SourceType::kDash, TypeSignal::kSignature);
  ExpectDecided(Sniff(Mp4()), SourceType::kMp4, TypeSignal::kSignature);
  ExpectDecided(Sniff(Mp4("styp")), SourceType::kMp4, TypeSignal::kSignature);
  ExpectDecided(Sniff(Mp4("moof")), SourceType::kMp4, TypeSignal::kSignature);
  ExpectDecided(Sniff({0x1A, 0x45, 0xDF, 0xA3, 0x9F}), SourceType::kMatroska,
                TypeSignal::kSignature);
  ExpectDecided(Sniff(Bytes("FLV\x01\x05")), SourceType::kFlv,
                TypeSignal::kSignature);
  ExpectDecided(Sniff(TransportStream(3)), SourceType::kMpegTs,
                TypeSignal::kSignature);
}

TEST(SniffTest, SkipsXmlPrologueBeforeTheRootElement) {
  ExpectDecided(
      Sniff(Bytes("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- a > b -->\n"
                  "<!DOCTYPE MPD [ <!ENTITY e \"x\"> ]>\n<dash:MPD>")),
      SourceType::kDash, TypeSignal::kSignature);
}

TEST(SniffTest, NonMpdXmlIsRejectedEvenWithAMatchingHint) {
  const auto result =
      Sniff(Bytes("<html><body>error</body></html>"), true,
            {.extension = "mpd", .content_type = "application/dash+xml"});
  EXPECT_EQ(result.outcome, SniffOutcome::kUndecided);
}

TEST(SniffTest, SignaturesBeatExtensionAndContentType) {
  ExpectDecided(Sniff(Mp4(), true,
                      {.extension = "m3u8",
                       .content_type = "application/vnd.apple.mpegurl"}),
                SourceType::kMp4, TypeSignal::kSignature);
  ExpectDecided(Sniff(Bytes("#EXTM3U\n"), true,
                      {.extension = "mp4", .content_type = "video/mp4"}),
                SourceType::kHls, TypeSignal::kSignature);
}

TEST(SniffTest, ExtensionThenContentTypeBreakTiesWhenBytesLeaveTheTypeOpen) {
  const auto text = Bytes("some playlist-like text\n");
  ExpectDecided(Sniff(text, true, Hints("M3U8")), SourceType::kHls,
                TypeSignal::kExtension);
  ExpectDecided(Sniff(text, true, Hints({}, "Application/DASH+xml; q=1")),
                SourceType::kDash, TypeSignal::kContentType);
  ExpectDecided(Sniff(text, true,
                      {.extension = "mpd",
                       .content_type = "application/vnd.apple.mpegurl"}),
                SourceType::kDash, TypeSignal::kExtension);
  const std::vector<uint8_t> binary = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0};
  ExpectDecided(Sniff(binary, true, Hints("webm")), SourceType::kMatroska,
                TypeSignal::kExtension);
  ExpectDecided(Sniff(binary, true, Hints({}, "video/mp2t")),
                SourceType::kMpegTs, TypeSignal::kContentType);
}

TEST(SniffTest, TieBreakersMustAgreeWithTheKindOfBytes) {
  EXPECT_EQ(Sniff(Bytes("plain text, not a container\n"), true,
                  {.extension = "mp4", .content_type = "video/mp4"})
                .outcome,
            SniffOutcome::kUndecided);
  const std::vector<uint8_t> binary = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0};
  EXPECT_EQ(Sniff(binary, true, Hints("m3u8")).outcome,
            SniffOutcome::kUndecided);
}

TEST(SniffTest, UnknownBytesWithoutHintsAreUndecided) {
  EXPECT_EQ(Sniff(Bytes("hello world, nothing to see here")).outcome,
            SniffOutcome::kUndecided);
  EXPECT_EQ(Sniff({}, true, Hints("mp4")).outcome, SniffOutcome::kUndecided);
  EXPECT_EQ(Sniff(Bytes("#EXTM3")).outcome, SniffOutcome::kUndecided);
}

TEST(SniffTest, AskForMoreBytesUntilASignatureCanBeJudged) {
  auto result = Sniff({}, false);
  EXPECT_EQ(result.outcome, SniffOutcome::kNeedMoreBytes);
  EXPECT_EQ(result.wanted_bytes, kSniffBytes);
  result = Sniff(Bytes("#EXTM"), false);
  EXPECT_EQ(result.outcome, SniffOutcome::kNeedMoreBytes);
  result = Sniff(Bytes("\xEF\xBB"), false);
  EXPECT_EQ(result.outcome, SniffOutcome::kNeedMoreBytes);
  auto partial = TransportStream(2);
  partial.resize(300);
  result = Sniff(partial, false);
  EXPECT_EQ(result.outcome, SniffOutcome::kNeedMoreBytes);
  EXPECT_EQ(result.wanted_bytes, 377U);
  EXPECT_EQ(Sniff(partial, true).outcome, SniffOutcome::kDecided);
}

TEST(SniffTest, TransportStreamNeedsConsistentSyncBytes) {
  auto broken = TransportStream(3);
  broken[376] = 0;
  EXPECT_EQ(Sniff(broken).outcome, SniffOutcome::kUndecided);
  EXPECT_EQ(Sniff(std::vector<uint8_t>(100, 0x47)).outcome,
            SniffOutcome::kUndecided);
}

TEST(SniffTest, GrowsTheBudgetOnlyWhileAPrologueHidesTheRoot) {
  std::string prologue = "<?xml version=\"1.0\"?>\n";
  while (prologue.size() < kSniffBytes) {
    prologue += "<!-- padding padding padding padding -->\n";
  }
  auto bytes = Bytes(prologue);
  bytes.resize(kSniffBytes);
  const auto first = Sniff(bytes, false);
  ASSERT_EQ(first.outcome, SniffOutcome::kNeedMoreBytes);
  EXPECT_EQ(first.wanted_bytes, 2 * kSniffBytes);
  EXPECT_GT(first.wanted_bytes, bytes.size());

  const std::string longer = prologue + "<MPD>";
  EXPECT_EQ(Sniff(Bytes(longer), true).type, SourceType::kDash);

  std::string endless = "<?xml?>";
  while (endless.size() < kMaxSniffBytes) {
    endless += "<!-- padding padding padding padding -->\n";
  }
  endless.resize(kMaxSniffBytes);
  const auto capped = Sniff(Bytes(endless), false);
  EXPECT_EQ(capped.outcome, SniffOutcome::kUndecided);

  auto almost = Bytes(endless);
  almost.resize(kMaxSniffBytes - 1);
  const auto last_step = Sniff(almost, false);
  ASSERT_EQ(last_step.outcome, SniffOutcome::kNeedMoreBytes);
  EXPECT_EQ(last_step.wanted_bytes, kMaxSniffBytes);
}

TEST(SniffTest, WhitespaceOnlyInputKeepsTheBudgetGrowing) {
  const std::vector<uint8_t> blanks(kSniffBytes, ' ');
  const auto result = Sniff(blanks, false);
  EXPECT_EQ(result.outcome, SniffOutcome::kNeedMoreBytes);
  EXPECT_EQ(Sniff(blanks, true).outcome, SniffOutcome::kUndecided);
}

TEST(SniffTest, TextThatIsNotXmlOrHlsIsLeftToTieBreakers) {
  ExpectDecided(Sniff(Bytes("#EXT-X-NOT-FIRST\n"), true, Hints("m3u8")),
                SourceType::kHls, TypeSignal::kExtension);
  EXPECT_EQ(Sniff(Bytes("<")).outcome, SniffOutcome::kUndecided);
  EXPECT_EQ(Sniff(Bytes("< not xml")).outcome, SniffOutcome::kUndecided);
}

}
}
