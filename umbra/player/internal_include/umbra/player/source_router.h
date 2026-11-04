#ifndef UMBRA_PLAYER_SOURCE_ROUTER_H_
#define UMBRA_PLAYER_SOURCE_ROUTER_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/task_runner.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/text_track.h"

namespace umbra::base {
class UrlRedactor;
}

namespace umbra::source {
class SourceResolver;
}

namespace umbra::player::internal {

struct TextTrackRoutingOptions {
  size_t cue_memory_budget_bytes = 0;
  size_t max_payload_bytes = 0;
  std::string fallback_encoding;
  bool auto_detect_encoding = false;
  bool auto_discover_siblings = false;
};

using TextPayloadCallback = std::function<void(absl::StatusOr<std::string>)>;
using TextTrackUrlLoader =
    std::function<void(std::string, size_t, TextPayloadCallback)>;
using SourceResolverCallback =
    std::function<std::unique_ptr<pipeline::SampleSource>(
        std::string_view, TextTrackList*, TextTrackRoutingOptions)>;

SourceResolverCallback CreateSourceResolver(
    std::shared_ptr<const source::SourceResolver> resolver,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<penumbra::HttpTransport> transport,
    std::shared_ptr<const base::UrlRedactor> redactor,
    TextTrackUrlLoader text_track_loader = {});
TextTrackUrlLoader CreateTextTrackUrlLoader(
    std::shared_ptr<const source::SourceResolver> resolver,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<const base::UrlRedactor> redactor);

}

#endif
