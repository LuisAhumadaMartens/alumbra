#ifndef UMBRA_PLAYER_SOURCE_ROUTER_H_
#define UMBRA_PLAYER_SOURCE_ROUTER_H_

#include <functional>
#include <memory>
#include <string_view>

#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/task_runner.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/source/source_resolver.h"

namespace umbra::player::internal {

using SourceResolverCallback =
    std::function<std::unique_ptr<pipeline::SampleSource>(std::string_view)>;

SourceResolverCallback CreateSourceResolver(
    std::shared_ptr<const source::SourceResolver> resolver,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<penumbra::HttpTransport> transport,
    std::shared_ptr<const base::UrlRedactor> redactor);

}

#endif
