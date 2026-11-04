#include "umbra/manifest/segment_index.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "test_util.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/url_template.h"

namespace umbra::manifest {
namespace {

using penumbra::MediaTime;
using penumbra::Rational;
using penumbra::TimeDelta;
using test_data::Delta;
using test_data::MakeExplicitIndex;
using test_data::MakeSegment;
using test_data::Seconds;

constexpr TimeDelta kMicrosecond = TimeDelta::FromMicroseconds(1);

void ExpectBoundaries(const SegmentIndex& index, std::size_t position) {
  const MediaTime start = index.StartOf(position);
  const MediaTime end = index.EndOf(position);
  EXPECT_EQ(index.FindByTime(start), position);
  EXPECT_EQ(index.FindByTime(end - kMicrosecond), position);
  const std::optional<std::size_t> before =
      index.FindByTime(start - kMicrosecond);
  if (position == 0) {
    EXPECT_EQ(before, std::nullopt);
  } else if (index.EndOf(position - 1) == start) {
    EXPECT_EQ(before, position - 1);
  } else {
    EXPECT_EQ(before, std::nullopt);
  }
  const std::optional<std::size_t> after = index.FindByTime(end);
  if (position + 1 == index.size()) {
    EXPECT_EQ(after, std::nullopt);
  } else if (index.StartOf(position + 1) == end) {
    EXPECT_EQ(after, position + 1);
  } else {
    EXPECT_EQ(after, std::nullopt);
  }
}

TEST(ExplicitSegmentIndexTest, LookupIsExactAtEveryBoundary) {
  const auto index = MakeExplicitIndex(10, 8);
  ASSERT_NE(index, nullptr);
  EXPECT_EQ(index->size(), 8U);
  EXPECT_EQ(index->FirstSequenceNumber(), 10);
  EXPECT_EQ(index->LastSequenceNumber(), 17);
  for (std::size_t i = 0; i < index->size(); ++i) {
    ExpectBoundaries(*index, i);
  }
  EXPECT_EQ(index->FindByTime(MediaTime::Unknown()), std::nullopt);
  EXPECT_EQ(index->FindByTime(MediaTime::Infinite()), std::nullopt);
}

TEST(ExplicitSegmentIndexTest, LookupBySequenceCoversTheWholeWindow) {
  const auto index = MakeExplicitIndex(100, 5);
  ASSERT_NE(index, nullptr);
  EXPECT_EQ(index->FindBySequence(99), std::nullopt);
  EXPECT_EQ(index->FindBySequence(100), 0U);
  EXPECT_EQ(index->FindBySequence(104), 4U);
  EXPECT_EQ(index->FindBySequence(105), std::nullopt);
}

TEST(ExplicitSegmentIndexTest, ATimeInsideAHoleFindsNothing) {
  ExplicitSegmentIndexParameters parameters;
  parameters.segments = {MakeSegment(0, 0, 4), MakeSegment(1, 10, 4)};
  const auto index = ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok()) << index.status();
  EXPECT_EQ((*index)->FindByTime(Seconds(3)), 0U);
  EXPECT_EQ((*index)->FindByTime(Seconds(4)), std::nullopt);
  EXPECT_EQ((*index)->FindByTime(Seconds(9)), std::nullopt);
  EXPECT_EQ((*index)->FindByTime(Seconds(10)), 1U);
}

TEST(ExplicitSegmentIndexTest, EmptyIndexKeepsItsSequenceBase) {
  ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = 55;
  const auto index = ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok());
  EXPECT_TRUE((*index)->empty());
  EXPECT_EQ((*index)->FirstSequenceNumber(), 55);
  EXPECT_EQ((*index)->FindByTime(Seconds(0)), std::nullopt);
  EXPECT_TRUE((*index)->Start().is_unknown());
}

TEST(ExplicitSegmentIndexTest, RejectsBrokenShapes) {
  const auto create = [](auto edit) {
    ExplicitSegmentIndexParameters parameters;
    parameters.segments = {MakeSegment(0, 0, 4), MakeSegment(1, 4, 4),
                           MakeSegment(2, 8, 4)};
    parameters.init_sections.push_back(
        test_data::MakeInit("https://x.test/init"));
    edit(parameters);
    return ExplicitSegmentIndex::Create(std::move(parameters));
  };
  EXPECT_TRUE(create([](auto&) {}).ok());
  EXPECT_FALSE(create([](auto& p) { p.segments[1].sequence_number = 5; }).ok());
  EXPECT_FALSE(create([](auto& p) { p.segments[1].start = Seconds(3); }).ok());
  EXPECT_FALSE(create([](auto& p) { p.segments[1].duration = Delta(0); }).ok());
  EXPECT_FALSE(
      create([](auto& p) { p.segments[1].start = MediaTime::Unknown(); }).ok());
  EXPECT_FALSE(create([](auto& p) { p.segments[1].uri.clear(); }).ok());
  EXPECT_TRUE(create([](auto& p) {
                p.segments[1].uri.clear();
                p.segments[1].gap = true;
              }).ok());
  EXPECT_FALSE(create([](auto& p) { p.segments[1].init_index = 1; }).ok());
  EXPECT_TRUE(create([](auto& p) { p.segments[1].init_index = 0; }).ok());
  EXPECT_FALSE(create([](auto& p) { p.segments[1].key_index = 0; }).ok());
  EXPECT_FALSE(create([](auto& p) {
                 p.segments[1].byte_range = ByteRange{-1, 4};
               }).ok());
  EXPECT_FALSE(create([](auto& p) {
                 p.segments[1].byte_range = ByteRange{INT64_MAX, 4};
               }).ok());
  EXPECT_FALSE(create([](auto& p) {
                 p.segments[2].discontinuity_sequence = 1;
                 p.segments[1].discontinuity_sequence = 2;
               }).ok());
  EXPECT_FALSE(create([](auto& p) { p.init_sections[0].uri.clear(); }).ok());
}

TEST(ExplicitSegmentIndexTest, ErrorsUseTheManifestInvalidCatalogueEntry) {
  ExplicitSegmentIndexParameters parameters;
  parameters.segments = {MakeSegment(0, 0, 4), MakeSegment(2, 4, 4)};
  const auto index = ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_FALSE(index.ok());
  EXPECT_EQ(index.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(index.status().message().find("ALUMBRA-2001"), std::string::npos);
}

TemplateSegmentIndexParameters UniformTemplate(std::int64_t count) {
  TemplateSegmentIndexParameters parameters;
  parameters.media_template =
      "https://cdn.test/$RepresentationID$/$Number$.m4s";
  parameters.init_section =
      test_data::MakeInit("https://cdn.test/video/init.mp4");
  parameters.representation_id = "video";
  parameters.first_sequence_number = 1;
  parameters.time_base = Rational::Of(1, 1000);
  TimelineRun run;
  run.start_ticks = 0;
  run.duration_ticks = 4000;
  run.count = count;
  parameters.runs.push_back(run);
  return parameters;
}

TEST(TemplateSegmentIndexTest, GeneratesSegmentsLazily) {
  const auto index = TemplateSegmentIndex::Create(UniformTemplate(100));
  ASSERT_TRUE(index.ok()) << index.status();
  const Segment first = (*index)->At(0);
  EXPECT_EQ(first.sequence_number, 1);
  EXPECT_EQ(first.start, Seconds(0));
  EXPECT_EQ(first.duration, Delta(4));
  EXPECT_EQ(first.uri, "https://cdn.test/video/1.m4s");
  EXPECT_EQ(first.init_index, 0U);
  const Segment last = (*index)->At(99);
  EXPECT_EQ(last.sequence_number, 100);
  EXPECT_EQ(last.start, Seconds(396));
  EXPECT_EQ(last.uri, "https://cdn.test/video/100.m4s");
  ASSERT_EQ((*index)->InitSections().size(), 1U);
}

TEST(TemplateSegmentIndexTest, LookupIsExactAtBoundariesAndTheEnd) {
  const auto index = TemplateSegmentIndex::Create(UniformTemplate(50));
  ASSERT_TRUE(index.ok());
  for (std::size_t i = 0; i < (*index)->size(); ++i) {
    ExpectBoundaries(**index, i);
  }
  EXPECT_EQ((*index)->FindByTime((*index)->End()), std::nullopt);
}

TEST(TemplateSegmentIndexTest, ARepresentationOf100000SegmentsIsConstantSize) {
  const auto small = TemplateSegmentIndex::Create(UniformTemplate(10));
  const auto large = TemplateSegmentIndex::Create(UniformTemplate(100'000));
  ASSERT_TRUE(small.ok());
  ASSERT_TRUE(large.ok());
  EXPECT_EQ((*large)->size(), 100'000U);
  EXPECT_EQ((*large)->Runs().size(), 1U);
  EXPECT_EQ((*large)->ApproximateBytes(), (*small)->ApproximateBytes());
  EXPECT_LT((*large)->ApproximateBytes(), 1024U);
  EXPECT_EQ((*large)->At(99'999).uri, "https://cdn.test/video/100000.m4s");
  EXPECT_EQ((*large)->FindByTime(Seconds(399'999)), 99'999U);

  ExplicitSegmentIndexParameters explicit_parameters;
  for (std::int64_t i = 0; i < 100'000; ++i) {
    explicit_parameters.segments.push_back(MakeSegment(i, i * 4, 4));
  }
  const auto listed =
      ExplicitSegmentIndex::Create(std::move(explicit_parameters));
  ASSERT_TRUE(listed.ok());
  EXPECT_GT((*listed)->ApproximateBytes(), 100 * (*large)->ApproximateBytes());
}

TEST(TemplateSegmentIndexTest,
     TimeAddressingUsesMediaTicksNotPresentationTime) {
  TemplateSegmentIndexParameters parameters;
  parameters.media_template = "seg-$Time$.m4s";
  parameters.first_sequence_number = 7;
  parameters.time_base = Rational::Of(1, 90'000);
  parameters.presentation_time_offset_ticks = 900'000;
  parameters.period_start = Seconds(30);
  TimelineRun run;
  run.start_ticks = 900'000;
  run.duration_ticks = 360'000;
  run.count = 3;
  parameters.runs.push_back(run);
  const auto index = TemplateSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok()) << index.status();
  EXPECT_EQ((*index)->StartOf(0), Seconds(30));
  EXPECT_EQ((*index)->EndOf(2), Seconds(42));
  EXPECT_EQ((*index)->At(1).uri, "seg-1260000.m4s");
  EXPECT_EQ((*index)->At(1).sequence_number, 8);
  EXPECT_FALSE((*index)->At(0).init_index.has_value());
}

TEST(TemplateSegmentIndexTest, TimelineGapsAreHolesInLookup) {
  TemplateSegmentIndexParameters parameters = UniformTemplate(2);
  TimelineRun later;
  later.start_ticks = 20'000;
  later.duration_ticks = 4000;
  later.count = 2;
  parameters.runs.push_back(later);
  const auto index = TemplateSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok()) << index.status();
  EXPECT_EQ((*index)->size(), 4U);
  EXPECT_EQ((*index)->FindByTime(Seconds(7)), 1U);
  EXPECT_EQ((*index)->FindByTime(Seconds(8)), std::nullopt);
  EXPECT_EQ((*index)->FindByTime(Seconds(19)), std::nullopt);
  EXPECT_EQ((*index)->FindByTime(Seconds(20)), 2U);
  EXPECT_EQ((*index)->At(3).uri, "https://cdn.test/video/4.m4s");
}

TEST(TemplateSegmentIndexTest, RejectsBrokenParameters) {
  const auto create = [](auto edit) {
    TemplateSegmentIndexParameters parameters = UniformTemplate(4);
    edit(parameters);
    return TemplateSegmentIndex::Create(std::move(parameters));
  };
  EXPECT_TRUE(create([](auto&) {}).ok());
  EXPECT_FALSE(create([](auto& p) { p.media_template = "seg.m4s"; }).ok());
  EXPECT_FALSE(create([](auto& p) { p.time_base = Rational(); }).ok());
  EXPECT_FALSE(create([](auto& p) { p.runs[0].count = 0; }).ok());
  EXPECT_FALSE(create([](auto& p) { p.runs[0].duration_ticks = 0; }).ok());
  EXPECT_FALSE(create([](auto& p) { p.runs[0].start_ticks = -1; }).ok());
  EXPECT_FALSE(
      create([](auto& p) { p.runs[0].count = kMaxSegmentsPerIndex + 1; }).ok());
  EXPECT_FALSE(create([](auto& p) {
                 p.runs[0].duration_ticks = INT64_MAX / 2;
                 p.runs[0].count = 4;
               }).ok());
  EXPECT_FALSE(create([](auto& p) {
                 TimelineRun overlapping;
                 overlapping.start_ticks = 1000;
                 overlapping.duration_ticks = 1000;
                 p.runs.push_back(overlapping);
               }).ok());
  EXPECT_FALSE(create([](auto& p) {
                 p.time_base = Rational::Of(1, 100'000'000);
                 p.runs[0].duration_ticks = 5;
               }).ok());
  EXPECT_FALSE(
      create([](auto& p) { p.first_sequence_number = INT64_MAX; }).ok());
  EXPECT_FALSE(
      create([](auto& p) { p.period_start = MediaTime::Infinite(); }).ok());
  EXPECT_FALSE(
      create([](auto& p) { p.presentation_time_offset_ticks = -1; }).ok());
}

TEST(UrlTemplateTest, ExpandsIdentifiersWidthsAndEscapedDollars) {
  struct Case {
    std::string_view input;
    std::string expected;
  };
  const std::vector<Case> cases = {
      {"$RepresentationID$-$Number$", "video-42"},
      {"$Number$", "42"},
      {"$Bandwidth$-$Time$", "1500000-90000"},
      {"$Time$", "90000"},
      {"$Number%05d$", "00042"},
      {"$Time%08d$", "00090000"},
      {"$Bandwidth%010d$-$Number$", "0001500000-42"},
      {"$$$Number$", "$42"},
      {"$$/$RepresentationID$/$Number%03d$", "$/video/042"},
  };
  TemplateValues values;
  values.number = 42;
  values.time = 90'000;
  values.bandwidth = 1'500'000;
  values.representation_id = "video";
  for (const Case& test_case : cases) {
    const auto parsed = UrlTemplate::Parse(test_case.input);
    ASSERT_TRUE(parsed.ok()) << test_case.input;
    EXPECT_EQ(parsed->Expand(values), test_case.expected) << test_case.input;
  }
}

TEST(TemplateSegmentIndexTest, MatchesAnExplicitExpansionForRandomTimelines) {
  test_data::Rng random(1234);
  for (int round = 0; round < 200; ++round) {
    TemplateSegmentIndexParameters parameters;
    parameters.media_template = "s$Number$-$Time$";
    parameters.first_sequence_number =
        static_cast<std::int64_t>(random() % 1000);
    parameters.time_base = Rational::Of(1, 90'000);
    parameters.period_start =
        Seconds(static_cast<std::int64_t>(random() % 100));
    parameters.presentation_time_offset_ticks =
        static_cast<std::int64_t>(random() % 50'000);
    std::int64_t cursor = parameters.presentation_time_offset_ticks;
    const int runs = 1 + static_cast<int>(random() % 5);
    for (int i = 0; i < runs; ++i) {
      TimelineRun run;
      run.start_ticks =
          cursor + (static_cast<std::int64_t>(random() % 3) * 1000);
      run.duration_ticks = 1000 + static_cast<std::int64_t>(random() % 400'000);
      run.count = 1 + static_cast<std::int64_t>(random() % 20);
      cursor = run.start_ticks + (run.duration_ticks * run.count);
      parameters.runs.push_back(run);
    }
    const auto index = TemplateSegmentIndex::Create(parameters);
    ASSERT_TRUE(index.ok()) << index.status();
    std::vector<std::int64_t> starts;
    for (const TimelineRun& run : parameters.runs) {
      for (std::int64_t i = 0; i < run.count; ++i) {
        starts.push_back(run.start_ticks + (i * run.duration_ticks));
      }
    }
    ASSERT_EQ((*index)->size(), starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
      const Segment segment = (*index)->At(i);
      EXPECT_EQ(segment.sequence_number, parameters.first_sequence_number +
                                             static_cast<std::int64_t>(i));
      EXPECT_EQ(segment.uri, "s" + std::to_string(segment.sequence_number) +
                                 "-" + std::to_string(starts[i]));
      EXPECT_GT(segment.duration.InMicroseconds(), 0);
      ExpectBoundaries(**index, i);
    }
  }
}

TEST(SegmentIndexProviderTest, ResolvedAndDeferredFormsAreDistinct) {
  const SegmentIndexProvider empty;
  EXPECT_FALSE(empty.is_resolved());
  IndexLocation location;
  location.uri = "https://x.test/media.m3u8";
  const SegmentIndexProvider deferred =
      SegmentIndexProvider::Deferred(location);
  EXPECT_FALSE(deferred.is_resolved());
  ASSERT_TRUE(deferred.Location().has_value());
  const auto index = MakeExplicitIndex(0, 3);
  const SegmentIndexProvider resolved = deferred.WithIndex(index);
  EXPECT_TRUE(resolved.is_resolved());
  EXPECT_EQ(resolved.Location(), deferred.Location());
  EXPECT_NE(resolved, deferred);
  EXPECT_EQ(resolved, deferred.WithIndex(MakeExplicitIndex(0, 3)));
  EXPECT_NE(resolved, deferred.WithIndex(MakeExplicitIndex(0, 4)));
}

TEST(SequenceForStartTest, FindsTheSequenceOfAnExactStart) {
  const auto index = MakeExplicitIndex(40, 5);
  EXPECT_EQ(SequenceForStart(*index, Seconds(168)), 42);
  EXPECT_EQ(SequenceForStart(*index, Seconds(169)), std::nullopt);
  EXPECT_EQ(SequenceForStart(*index, Seconds(1000)), std::nullopt);
}

std::shared_ptr<const SegmentIndex> Window(std::int64_t first,
                                           std::size_t count) {
  return MakeExplicitIndex(first, count);
}

TEST(MergeSegmentIndexesTest, SlidingWindowKeepsTheIdentityOfOverlap) {
  const auto previous = Window(10, 6);
  const auto next = Window(13, 6);
  const auto merged = MergeSegmentIndexes(previous, next);
  ASSERT_TRUE(merged.ok()) << merged.status();
  EXPECT_EQ((*merged)->FirstSequenceNumber(), 13);
  EXPECT_EQ((*merged)->LastSequenceNumber(), 18);
  for (std::int64_t sequence = 13; sequence <= 18; ++sequence) {
    const auto position = (*merged)->FindBySequence(sequence);
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ((*merged)->At(*position).sequence_number, sequence);
  }
}

TEST(MergeSegmentIndexesTest, AnUnchangedPlaylistKeepsThePreviousObject) {
  const auto previous = Window(10, 6);
  const auto merged = MergeSegmentIndexes(previous, Window(10, 6));
  ASSERT_TRUE(merged.ok());
  EXPECT_EQ(merged->get(), previous.get());
}

TEST(MergeSegmentIndexesTest, AGrowingWindowAppendsNewSegments) {
  const auto merged = MergeSegmentIndexes(Window(0, 4), Window(0, 7));
  ASSERT_TRUE(merged.ok());
  EXPECT_EQ((*merged)->size(), 7U);
}

TEST(MergeSegmentIndexesTest, ADisjointWindowReplacesThePreviousOne) {
  const auto next = Window(50, 3);
  const auto merged = MergeSegmentIndexes(Window(10, 3), next);
  ASSERT_TRUE(merged.ok());
  EXPECT_EQ(merged->get(), next.get());
}

TEST(MergeSegmentIndexesTest, RefusesToRenumberOrMoveBackwards) {
  EXPECT_FALSE(MergeSegmentIndexes(Window(10, 6), Window(9, 6)).ok());

  ExplicitSegmentIndexParameters shifted;
  for (std::int64_t i = 0; i < 6; ++i) {
    shifted.segments.push_back(MakeSegment(10 + i, ((10 + i) * 4) + 1, 4));
  }
  const auto renumbered = ExplicitSegmentIndex::Create(std::move(shifted));
  ASSERT_TRUE(renumbered.ok());
  EXPECT_FALSE(MergeSegmentIndexes(Window(10, 6), *renumbered).ok());

  ExplicitSegmentIndexParameters other_uri;
  for (std::int64_t i = 0; i < 6; ++i) {
    other_uri.segments.push_back(MakeSegment(10 + i, (10 + i) * 4, 4));
    other_uri.segments.back().uri = "https://example.test/other.m4s";
  }
  const auto moved = ExplicitSegmentIndex::Create(std::move(other_uri));
  ASSERT_TRUE(moved.ok());
  EXPECT_FALSE(MergeSegmentIndexes(Window(10, 6), *moved).ok());

  EXPECT_FALSE(MergeSegmentIndexes(Window(10, 6), Window(10, 3)).ok());
}

TEST(MergeSegmentIndexesTest, TablesAreRemappedAndDoNotAccumulate) {
  const auto make = [](std::int64_t first, std::size_t count,
                       const std::string& init) {
    ExplicitSegmentIndexParameters parameters;
    parameters.init_sections.push_back(test_data::MakeInit(init));
    for (std::size_t i = 0; i < count; ++i) {
      const auto n = static_cast<std::int64_t>(i);
      parameters.segments.push_back(MakeSegment(first + n, (first + n) * 4, 4));
      parameters.segments.back().init_index = 0;
    }
    return ExplicitSegmentIndex::Create(std::move(parameters));
  };
  const auto previous = make(0, 4, "https://x.test/init-a.mp4");
  const auto next = make(2, 4, "https://x.test/init-a.mp4");
  ASSERT_TRUE(previous.ok());
  ASSERT_TRUE(next.ok());
  auto merged = MergeSegmentIndexes(*previous, *next);
  ASSERT_TRUE(merged.ok()) << merged.status();
  EXPECT_EQ((*merged)->InitSections().size(), 1U);

  const auto later = make(4, 4, "https://x.test/init-b.mp4");
  ASSERT_TRUE(later.ok());
  merged = MergeSegmentIndexes(*merged, *later);
  ASSERT_TRUE(merged.ok()) << merged.status();
  EXPECT_EQ((*merged)->size(), 4U);
  EXPECT_EQ((*merged)->InitSections().size(), 2U);
  EXPECT_EQ((*merged)->At(0).init_index, 0U);
  EXPECT_EQ((*merged)->At(3).init_index, 1U);

  const auto last = make(6, 4, "https://x.test/init-b.mp4");
  ASSERT_TRUE(last.ok());
  merged = MergeSegmentIndexes(*merged, *last);
  ASSERT_TRUE(merged.ok()) << merged.status();
  EXPECT_EQ((*merged)->InitSections().size(), 1U);
  EXPECT_EQ((*merged)->InitSections()[0].uri, "https://x.test/init-b.mp4");
  for (std::size_t i = 0; i < (*merged)->size(); ++i) {
    EXPECT_EQ((*merged)->At(i).init_index, 0U);
  }
}

TEST(MergeSegmentIndexesTest, RandomSlidingWindowsNeverRenumber) {
  test_data::Rng random(99);
  std::shared_ptr<const SegmentIndex> current = Window(0, 6);
  for (int step = 0; step < 300; ++step) {
    const std::int64_t first = current->FirstSequenceNumber() +
                               static_cast<std::int64_t>(random() % 3);
    const std::int64_t last =
        current->LastSequenceNumber() + static_cast<std::int64_t>(random() % 4);
    const auto next = Window(first, static_cast<std::size_t>(last - first + 1));
    const auto merged = MergeSegmentIndexes(current, next);
    ASSERT_TRUE(merged.ok()) << merged.status();
    for (std::int64_t sequence = (*merged)->FirstSequenceNumber();
         sequence <= (*merged)->LastSequenceNumber(); ++sequence) {
      const auto position = (*merged)->FindBySequence(sequence);
      ASSERT_TRUE(position.has_value());
      const Segment segment = (*merged)->At(*position);
      EXPECT_EQ(segment.sequence_number, sequence);
      EXPECT_EQ(segment.start, Seconds(sequence * 4));
      const auto old_position = current->FindBySequence(sequence);
      if (old_position.has_value()) {
        EXPECT_EQ(current->At(*old_position).uri, segment.uri);
      }
    }
    current = *merged;
  }
}

TEST(MergeSegmentIndexesTest,
     TemplateRefreshChecksOverlapAndAdoptsTheNewIndex) {
  const auto previous = TemplateSegmentIndex::Create(UniformTemplate(10));
  TemplateSegmentIndexParameters slid = UniformTemplate(10);
  slid.first_sequence_number = 4;
  slid.runs[0].start_ticks = 12'000;
  const auto next = TemplateSegmentIndex::Create(slid);
  ASSERT_TRUE(previous.ok());
  ASSERT_TRUE(next.ok());
  const std::shared_ptr<const SegmentIndex> before = *previous;
  const std::shared_ptr<const SegmentIndex> after = *next;
  const auto merged = MergeSegmentIndexes(before, after);
  ASSERT_TRUE(merged.ok()) << merged.status();
  EXPECT_EQ(merged->get(), after.get());

  TemplateSegmentIndexParameters renumbered = slid;
  renumbered.first_sequence_number = 5;
  const auto bad = TemplateSegmentIndex::Create(renumbered);
  ASSERT_TRUE(bad.ok());
  EXPECT_FALSE(MergeSegmentIndexes(before, *bad).ok());
}

}
}
