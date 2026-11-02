#ifndef UMBRA_MANIFEST_SEGMENT_INDEX_H_
#define UMBRA_MANIFEST_SEGMENT_INDEX_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/url_template.h"

namespace umbra::manifest {

inline constexpr std::int64_t kMaxSegmentsPerIndex = 10'000'000;

class SegmentIndex {
 public:
  enum class Kind : std::uint8_t {
    kExplicit,
    kTemplate,
  };

  virtual ~SegmentIndex() = default;
  SegmentIndex(const SegmentIndex&) = delete;
  SegmentIndex& operator=(const SegmentIndex&) = delete;

  [[nodiscard]] virtual Kind IndexKind() const = 0;
  [[nodiscard]] virtual Segment At(std::size_t position) const = 0;
  [[nodiscard]] virtual penumbra::MediaTime StartOf(
      std::size_t position) const = 0;
  [[nodiscard]] virtual penumbra::MediaTime EndOf(
      std::size_t position) const = 0;
  [[nodiscard]] virtual bool Equals(const SegmentIndex& other) const = 0;
  [[nodiscard]] virtual std::size_t ApproximateBytes() const = 0;

  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0; }
  [[nodiscard]] std::int64_t FirstSequenceNumber() const {
    return first_sequence_number_;
  }
  [[nodiscard]] std::int64_t LastSequenceNumber() const;
  [[nodiscard]] penumbra::MediaTime Start() const;
  [[nodiscard]] penumbra::MediaTime End() const;
  [[nodiscard]] const std::vector<InitSection>& InitSections() const {
    return init_sections_;
  }
  [[nodiscard]] const std::vector<KeyReference>& KeyReferences() const {
    return key_references_;
  }

  [[nodiscard]] std::optional<std::size_t> FindByTime(
      penumbra::MediaTime time) const;
  [[nodiscard]] std::optional<std::size_t> FindBySequence(
      std::int64_t sequence_number) const;

 protected:
  SegmentIndex(std::size_t count, std::int64_t first_sequence_number,
               std::vector<InitSection> init_sections,
               std::vector<KeyReference> key_references)
      : count_(count),
        first_sequence_number_(first_sequence_number),
        init_sections_(std::move(init_sections)),
        key_references_(std::move(key_references)) {}

 private:
  std::size_t count_ = 0;
  std::int64_t first_sequence_number_ = 0;
  std::vector<InitSection> init_sections_;
  std::vector<KeyReference> key_references_;
};

struct ExplicitSegmentIndexParameters {
  std::vector<Segment> segments;
  std::int64_t first_sequence_number = 0;
  std::vector<InitSection> init_sections;
  std::vector<KeyReference> key_references;
};

class ExplicitSegmentIndex final : public SegmentIndex {
 public:
  static absl::StatusOr<std::shared_ptr<const ExplicitSegmentIndex>> Create(
      ExplicitSegmentIndexParameters parameters);

  [[nodiscard]] Kind IndexKind() const override { return Kind::kExplicit; }
  [[nodiscard]] Segment At(std::size_t position) const override;
  [[nodiscard]] penumbra::MediaTime StartOf(
      std::size_t position) const override;
  [[nodiscard]] penumbra::MediaTime EndOf(std::size_t position) const override;
  [[nodiscard]] bool Equals(const SegmentIndex& other) const override;
  [[nodiscard]] std::size_t ApproximateBytes() const override;

  [[nodiscard]] const std::vector<Segment>& Segments() const {
    return segments_;
  }

 private:
  explicit ExplicitSegmentIndex(ExplicitSegmentIndexParameters parameters);

  std::vector<Segment> segments_;
};

struct TimelineRun {
  std::int64_t start_ticks = 0;
  std::int64_t duration_ticks = 0;
  std::int64_t count = 1;

  friend bool operator==(const TimelineRun&, const TimelineRun&) = default;
};

struct TemplateSegmentIndexParameters {
  std::string media_template;
  std::optional<InitSection> init_section;
  std::string representation_id;
  std::int64_t bandwidth = 0;
  std::int64_t first_sequence_number = 1;
  penumbra::Rational time_base;
  std::vector<TimelineRun> runs;
  std::int64_t presentation_time_offset_ticks = 0;
  penumbra::MediaTime period_start;
  std::int64_t discontinuity_sequence = 0;
};

class TemplateSegmentIndex final : public SegmentIndex {
 public:
  static absl::StatusOr<std::shared_ptr<const TemplateSegmentIndex>> Create(
      TemplateSegmentIndexParameters parameters);

  [[nodiscard]] Kind IndexKind() const override { return Kind::kTemplate; }
  [[nodiscard]] Segment At(std::size_t position) const override;
  [[nodiscard]] penumbra::MediaTime StartOf(
      std::size_t position) const override;
  [[nodiscard]] penumbra::MediaTime EndOf(std::size_t position) const override;
  [[nodiscard]] bool Equals(const SegmentIndex& other) const override;
  [[nodiscard]] std::size_t ApproximateBytes() const override;

  [[nodiscard]] const std::vector<TimelineRun>& Runs() const { return runs_; }

 private:
  TemplateSegmentIndex(TemplateSegmentIndexParameters parameters,
                       UrlTemplate media_template,
                       std::vector<std::size_t> first_positions,
                       std::size_t count);

  [[nodiscard]] std::size_t RunOf(std::size_t position) const;
  [[nodiscard]] std::int64_t StartTicks(std::size_t position) const;
  [[nodiscard]] penumbra::MediaTime TicksToTime(std::int64_t ticks) const;

  UrlTemplate media_template_;
  std::optional<InitSection> init_section_;
  std::string representation_id_;
  std::int64_t bandwidth_ = 0;
  penumbra::Rational time_base_;
  std::vector<TimelineRun> runs_;
  std::vector<std::size_t> first_positions_;
  std::int64_t presentation_time_offset_ticks_ = 0;
  penumbra::MediaTime period_start_;
  std::int64_t discontinuity_sequence_ = 0;
};

struct IndexLocation {
  enum class Source : std::uint8_t {
    kHlsMediaPlaylist,
    kDashSegmentBase,
    kDashSegmentIndexBox,
  };

  Source source = Source::kHlsMediaPlaylist;
  std::string uri;
  std::optional<ByteRange> byte_range;

  friend bool operator==(const IndexLocation&, const IndexLocation&) = default;
};

class SegmentIndexProvider {
 public:
  SegmentIndexProvider() = default;

  static SegmentIndexProvider Resolved(
      std::shared_ptr<const SegmentIndex> index);
  static SegmentIndexProvider Deferred(IndexLocation location);

  [[nodiscard]] SegmentIndexProvider WithIndex(
      std::shared_ptr<const SegmentIndex> index) const;

  [[nodiscard]] bool is_resolved() const { return index_ != nullptr; }
  [[nodiscard]] const SegmentIndex* Index() const { return index_.get(); }
  [[nodiscard]] const std::shared_ptr<const SegmentIndex>& SharedIndex() const {
    return index_;
  }
  [[nodiscard]] const std::optional<IndexLocation>& Location() const {
    return location_;
  }

  friend bool operator==(const SegmentIndexProvider& a,
                         const SegmentIndexProvider& b);

 private:
  std::shared_ptr<const SegmentIndex> index_;
  std::optional<IndexLocation> location_;
};

[[nodiscard]] std::optional<std::int64_t> SequenceForStart(
    const SegmentIndex& previous, penumbra::MediaTime start);

[[nodiscard]] absl::StatusOr<std::shared_ptr<const SegmentIndex>>
MergeSegmentIndexes(const std::shared_ptr<const SegmentIndex>& previous,
                    const std::shared_ptr<const SegmentIndex>& next);

}

#endif
