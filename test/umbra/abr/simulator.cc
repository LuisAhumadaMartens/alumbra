#include "simulator.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "umbra/abr/bandwidth_estimator.h"
#include "umbra/abr/policy.h"
#include "umbra/base/test_support/fake_tick_clock.h"

namespace umbra::abr::simulation {
namespace {

constexpr std::int64_t kSegmentDurationMs = 4000;
constexpr std::int64_t kTtfbMs = 100;
constexpr std::int64_t kDefaultBandwidthBps = 1000000;
constexpr std::uint64_t kMaximumSegments = 60;
constexpr std::int64_t kMaximumDownloadMs = 120000;

std::int64_t ParseInteger(std::string_view value) {
  std::int64_t result = 0;
  const char* const begin = value.data();
  const char* const end =
      std::next(begin, static_cast<std::ptrdiff_t>(value.size()));
  const auto parsed = std::from_chars(begin, end, result);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    throw std::runtime_error("trace contains an invalid integer");
  }
  return result;
}

double ParseNumber(std::string_view value) {
  double result = 0;
  const auto parsed = std::from_chars(value.begin(), value.end(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.end() ||
      !std::isfinite(result)) {
    throw std::runtime_error("baseline contains an invalid number");
  }
  return result;
}

double SustainableBitrate(const Trace& trace) {
  if (trace.points.size() == 1) {
    return static_cast<double>(trace.points.front().throughput_bps);
  }
  long double weighted_bitrate = 0;
  std::int64_t duration_ms = 0;
  for (std::size_t index = 0; index < trace.points.size(); ++index) {
    const std::int64_t interval_ms =
        index + 1 < trace.points.size()
            ? trace.points[index + 1].time_ms - trace.points[index].time_ms
            : trace.points[index].time_ms - trace.points[index - 1].time_ms;
    weighted_bitrate +=
        static_cast<long double>(trace.points[index].throughput_bps) *
        static_cast<long double>(interval_ms);
    duration_ms += interval_ms;
  }
  return static_cast<double>(weighted_bitrate /
                             static_cast<long double>(duration_ms));
}

std::int64_t ThroughputAt(const Trace& trace, std::int64_t time_ms) {
  const auto found =
      std::ranges::upper_bound(trace.points, time_ms, {}, &TracePoint::time_ms);
  return found == trace.points.begin() ? trace.points.front().throughput_bps
                                       : std::prev(found)->throughput_bps;
}

std::int64_t NextAvailableAt(const Trace& trace, std::int64_t time_ms) {
  const auto found =
      std::ranges::find_if(trace.points, [time_ms](const TracePoint& point) {
        return point.time_ms > time_ms && point.throughput_bps > 0;
      });
  return found == trace.points.end() ? time_ms + kMaximumDownloadMs
                                     : found->time_ms;
}

std::string JsonString(std::string_view value) {
  std::string result = "\"";
  for (const char character : value) {
    if (character == '\\' || character == '"') {
      result.push_back('\\');
    }
    if (character == '\n') {
      result += "\\n";
    } else if (character == '\r') {
      result += "\\r";
    } else {
      result.push_back(character);
    }
  }
  result.push_back('"');
  return result;
}

struct NetworkState {
  std::int64_t throughput_bps;
  bool stalled;
};

NetworkState ResumeAfterOutage(const Trace& trace, base::FakeTickClock& clock,
                               std::int64_t& wall_time_ms,
                               std::int64_t& buffer_ms,
                               std::uint64_t& rebuffer_count,
                               std::int64_t& rebuffer_ms,
                               long double& qoe_score) {
  const std::int64_t next_available = NextAvailableAt(trace, wall_time_ms);
  const std::int64_t outage_ms =
      std::min(kMaximumDownloadMs,
               std::max<std::int64_t>(0, next_available - wall_time_ms));
  const std::int64_t buffer_before_outage = buffer_ms;
  buffer_ms = std::max<std::int64_t>(0, buffer_ms - outage_ms);
  clock.Advance(absl::Milliseconds(outage_ms));
  wall_time_ms = trace.points.front().time_ms +
                 absl::ToInt64Milliseconds(clock.NowTicks().SinceOrigin());
  const std::int64_t outage_stall_ms =
      std::max<std::int64_t>(0, outage_ms - buffer_before_outage);
  rebuffer_ms += outage_stall_ms;
  qoe_score -= 4.3 * static_cast<double>(outage_stall_ms) / 1000.0;
  const bool stalled = outage_stall_ms > 0;
  if (stalled) {
    ++rebuffer_count;
  }
  return NetworkState{.throughput_bps = std::max<std::int64_t>(
                          1, ThroughputAt(trace, wall_time_ms)),
                      .stalled = stalled};
}

}

Trace ReadTrace(const std::filesystem::path& path) {
  const std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("could not open trace: " + path.string());
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string extension = path.extension().string();
  return ReadTraceText(path.stem().string(), contents.str(),
                       extension == ".mahimahi" || extension == ".mm");
}

namespace {

Trace ReadMahimahiTrace(const std::string& name, const std::string& contents) {
  Trace trace{.name = name, .points = {}};
  std::istringstream input(contents);
  std::string line;
  std::vector<std::int64_t> events;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    events.push_back(ParseInteger(line));
    if (events.back() < 0 || events.back() > 3600000) {
      throw std::runtime_error("Mahimahi timestamp is outside the limit");
    }
  }
  if (events.empty()) {
    throw std::runtime_error("Mahimahi trace is empty");
  }
  std::ranges::sort(events);
  const std::int64_t final_bucket = events.back() / 1000;
  std::vector<std::int64_t> packets(static_cast<std::size_t>(final_bucket + 1),
                                    0);
  for (const std::int64_t event : events) {
    ++packets[static_cast<std::size_t>(event / 1000)];
  }
  for (std::size_t index = 0; index < packets.size(); ++index) {
    trace.points.push_back(
        TracePoint{.time_ms = static_cast<std::int64_t>(index) * 1000,
                   .throughput_bps = packets[index] * 12000});
  }
  return trace;
}

Trace ReadCsvTrace(const std::string& name, const std::string& contents) {
  Trace trace{.name = name, .points = {}};
  std::istringstream input(contents);
  std::string line;
  if (!std::getline(input, line) || line != "time_ms,throughput_bps") {
    throw std::runtime_error("CSV trace header must be time_ms,throughput_bps");
  }
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const std::size_t separator = line.find(',');
    if (separator == std::string::npos ||
        line.find(',', separator + 1) != std::string::npos) {
      throw std::runtime_error("CSV trace row must contain two fields");
    }
    const std::int64_t time_ms = ParseInteger(line.substr(0, separator));
    const std::int64_t throughput_bps =
        ParseInteger(line.substr(separator + 1));
    if (time_ms < 0 || throughput_bps < 0 ||
        (!trace.points.empty() && time_ms <= trace.points.back().time_ms)) {
      throw std::runtime_error(
          "CSV trace values must be non-negative and ordered");
    }
    trace.points.push_back(
        TracePoint{.time_ms = time_ms, .throughput_bps = throughput_bps});
  }
  if (trace.points.empty()) {
    throw std::runtime_error("trace is empty");
  }
  return trace;
}

}

Trace ReadTraceText(const std::string& name, const std::string& contents,
                    bool mahimahi) {
  return mahimahi ? ReadMahimahiTrace(name, contents)
                  : ReadCsvTrace(name, contents);
}

Result Run(const Trace& trace, const std::string& configuration_name) {
  if (trace.points.empty()) {
    throw std::runtime_error("trace is empty");
  }
  const std::vector<Rendition> renditions = {{.id = 1,
                                              .bitrate_bps = 300000,
                                              .height = 360,
                                              .frame_rate_numerator = 30,
                                              .frame_rate_denominator = 1,
                                              .score = 0.0,
                                              .filter_id = ""},
                                             {.id = 2,
                                              .bitrate_bps = 700000,
                                              .height = 480,
                                              .frame_rate_numerator = 30,
                                              .frame_rate_denominator = 1,
                                              .score = 0.0,
                                              .filter_id = ""},
                                             {.id = 3,
                                              .bitrate_bps = 1500000,
                                              .height = 720,
                                              .frame_rate_numerator = 30,
                                              .frame_rate_denominator = 1,
                                              .score = 0.0,
                                              .filter_id = ""},
                                             {.id = 4,
                                              .bitrate_bps = 3000000,
                                              .height = 1080,
                                              .frame_rate_numerator = 30,
                                              .frame_rate_denominator = 1,
                                              .score = 0.0,
                                              .filter_id = ""}};
  BandwidthEstimatorConfig estimator_config;
  estimator_config.default_bandwidth_bps = kDefaultBandwidthBps;
  estimator_config.min_sample_bytes = 16384;
  AbrPolicyConfig policy_config;
  if (configuration_name == "throughput_conservative") {
    policy_config.throughput_safety_factor = 0.75;
    policy_config.startup_safety_factor = 0.65;
  } else if (configuration_name != "default") {
    throw std::runtime_error("unknown simulation configuration");
  }
  std::unique_ptr<BandwidthEstimator> estimator =
      CreateBandwidthEstimator(estimator_config);
  std::unique_ptr<AbrPolicy> policy = CreateAbrPolicy(policy_config);
  base::FakeTickClock clock;
  std::int64_t wall_time_ms = trace.points.front().time_ms;
  std::int64_t buffer_ms = 0;
  std::int64_t last_switch_ms = -1;
  std::uint64_t current_id = 0;
  std::uint64_t rebuffer_count = 0;
  std::int64_t rebuffer_ms = 0;
  std::int64_t startup_delay_ms = 0;
  std::uint64_t switch_count = 0;
  std::uint64_t segments_run = 0;
  long double bitrate_time_sum = 0;
  long double qoe_score = 0;
  const double sustainable_bitrate_bps = SustainableBitrate(trace);

  for (std::uint64_t index = 0; index < kMaximumSegments; ++index) {
    const BandwidthEstimate estimate = estimator->GetEstimate();
    const AbrSnapshot snapshot{.bandwidth_estimate_bps = estimate.bandwidth_bps,
                               .buffer_ms = buffer_ms,
                               .now_ms = wall_time_ms,
                               .last_switch_ms = last_switch_ms,
                               .segment_duration_ms = kSegmentDurationMs,
                               .current_rendition_id = current_id,
                               .candidates = renditions,
                               .filter_policy = {},
                               .rendition_capabilities = {},
                               .force_frame_rate_change = false};
    const AbrDecision decision = policy->Decide(snapshot);
    const auto selected =
        std::ranges::find_if(renditions, [&decision](const Rendition& item) {
          return item.id == decision.rendition_id;
        });
    if (selected == renditions.end()) {
      throw std::runtime_error("ABR policy returned an unknown rendition");
    }
    if (current_id != 0 && current_id != selected->id) {
      ++switch_count;
      last_switch_ms = wall_time_ms;
    }
    current_id = selected->id;
    const auto bytes = static_cast<std::uint64_t>(selected->bitrate_bps *
                                                  kSegmentDurationMs / 8000);
    std::int64_t throughput_bps = ThroughputAt(trace, wall_time_ms);
    bool stalled_during_outage = false;
    if (throughput_bps == 0) {
      const NetworkState network =
          ResumeAfterOutage(trace, clock, wall_time_ms, buffer_ms,
                            rebuffer_count, rebuffer_ms, qoe_score);
      throughput_bps = network.throughput_bps;
      stalled_during_outage = network.stalled;
    }
    const auto transfer_ms = static_cast<std::int64_t>(
        std::ceil(static_cast<long double>(bytes) * 8.0L * 1000.0L /
                  static_cast<long double>(throughput_bps)));
    const std::int64_t download_ms =
        std::min(kMaximumDownloadMs, kTtfbMs + transfer_ms);
    const std::int64_t drained_ms = std::min(buffer_ms, download_ms);
    const std::int64_t stalled_ms = download_ms - drained_ms;
    if (stalled_ms > 0 && !stalled_during_outage) {
      ++rebuffer_count;
    }
    if (stalled_ms > 0) {
      rebuffer_ms += stalled_ms;
    }
    buffer_ms = std::min<std::int64_t>(
        60000, buffer_ms - drained_ms + kSegmentDurationMs);
    clock.Advance(absl::Milliseconds(download_ms));
    wall_time_ms = trace.points.front().time_ms +
                   absl::ToInt64Milliseconds(clock.NowTicks().SinceOrigin());
    ++segments_run;
    estimator->AddSample(bytes, absl::Milliseconds(kTtfbMs),
                         absl::Milliseconds(download_ms - kTtfbMs));
    if (index == 0) {
      startup_delay_ms = wall_time_ms - trace.points.front().time_ms;
    }
    bitrate_time_sum += static_cast<long double>(selected->bitrate_bps) *
                        static_cast<long double>(kSegmentDurationMs);
    qoe_score +=
        std::log2(static_cast<double>(selected->bitrate_bps) / 300000.0) *
        static_cast<double>(kSegmentDurationMs) / 1000.0;
    qoe_score -= 4.3 * static_cast<double>(stalled_ms) / 1000.0;
    if (index > 0 && last_switch_ms == wall_time_ms - download_ms) {
      qoe_score -= 1.0;
    }
    if (wall_time_ms >= trace.points.back().time_ms + 60000) {
      break;
    }
  }
  const double played_ms = static_cast<double>(
      std::max<std::uint64_t>(1, segments_run) * kSegmentDurationMs);
  const double playback_minutes = played_ms / 60000.0;
  const double rebuffer_ratio = static_cast<double>(rebuffer_ms) /
                                (played_ms + static_cast<double>(rebuffer_ms));
  const double average_bitrate =
      static_cast<double>(bitrate_time_sum) / played_ms;
  return Result{
      .trace_name = trace.name,
      .configuration_name = configuration_name,
      .metrics = Metrics{
          .rebuffer_count = rebuffer_count,
          .rebuffer_ms = rebuffer_ms,
          .startup_delay_ms = startup_delay_ms,
          .switch_count = switch_count,
          .time_weighted_bitrate_bps = average_bitrate,
          .qoe_score = static_cast<double>(qoe_score),
          .rebuffer_ratio = rebuffer_ratio,
          .bitrate_efficiency = sustainable_bitrate_bps > 0
                                    ? average_bitrate / sustainable_bitrate_bps
                                    : 0,
          .switches_per_minute =
              static_cast<double>(switch_count) / playback_minutes,
          .sustainable_bitrate_bps = sustainable_bitrate_bps}};
}

std::vector<Baseline> ReadBaseline(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("could not open baseline: " + path.string());
  }
  std::string line;
  if (!std::getline(input, line) ||
      line !=
          "trace,rebuffer_ratio,bitrate_efficiency,switches_per_minute,"
          "max_rebuffer_ratio,min_bitrate_efficiency,"
          "max_switches_per_minute") {
    throw std::runtime_error("baseline header is invalid");
  }
  std::vector<Baseline> baselines;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const std::string_view line_view(line);
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
      const std::size_t separator = line.find(',', start);
      fields.emplace_back(line_view.substr(
          start,
          (separator == std::string::npos ? line.size() : separator) - start));
      if (separator == std::string::npos) {
        break;
      }
      start = separator + 1;
    }
    if (fields.size() != 7 || fields.front().empty()) {
      throw std::runtime_error("baseline row must contain seven fields");
    }
    Baseline baseline{.trace_name = std::string(fields[0]),
                      .rebuffer_ratio = ParseNumber(fields[1]),
                      .bitrate_efficiency = ParseNumber(fields[2]),
                      .switches_per_minute = ParseNumber(fields[3]),
                      .thresholds = Thresholds{
                          .max_rebuffer_ratio = ParseNumber(fields[4]),
                          .min_bitrate_efficiency = ParseNumber(fields[5]),
                          .max_switches_per_minute = ParseNumber(fields[6])}};
    if (baseline.rebuffer_ratio < 0 || baseline.bitrate_efficiency < 0 ||
        baseline.switches_per_minute < 0 ||
        baseline.thresholds.max_rebuffer_ratio < baseline.rebuffer_ratio ||
        baseline.thresholds.min_bitrate_efficiency < 0 ||
        baseline.thresholds.min_bitrate_efficiency >
            baseline.bitrate_efficiency ||
        baseline.thresholds.max_switches_per_minute <
            baseline.switches_per_minute) {
      throw std::runtime_error("baseline thresholds are inconsistent");
    }
    baselines.push_back(std::move(baseline));
  }
  if (baselines.empty()) {
    throw std::runtime_error("baseline contains no traces");
  }
  return baselines;
}

Evaluation Evaluate(const Result& result, const Baseline& baseline) {
  if (result.trace_name != baseline.trace_name) {
    throw std::runtime_error("simulation result does not match baseline trace");
  }
  const Metrics& metrics = result.metrics;
  const bool passed =
      metrics.rebuffer_ratio <= baseline.thresholds.max_rebuffer_ratio &&
      metrics.bitrate_efficiency >=
          baseline.thresholds.min_bitrate_efficiency &&
      metrics.switches_per_minute <=
          baseline.thresholds.max_switches_per_minute;
  return Evaluation{.result = result, .baseline = baseline, .passed = passed};
}

std::string RenderEvaluationMarkdown(const Evaluation& evaluation) {
  const Metrics& metrics = evaluation.result.metrics;
  const Thresholds& thresholds = evaluation.baseline.thresholds;
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(6) << "| "
         << evaluation.result.trace_name << " | " << metrics.rebuffer_ratio
         << " | " << thresholds.max_rebuffer_ratio << " | "
         << metrics.bitrate_efficiency << " | "
         << thresholds.min_bitrate_efficiency << " | "
         << metrics.switches_per_minute << " | "
         << thresholds.max_switches_per_minute << " | "
         << (evaluation.passed ? "PASS" : "FAIL") << " |\n";
  return output.str();
}

std::string RenderJson(const Result& result) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(3)
         << "{\"trace\":" << JsonString(result.trace_name)
         << ",\"configuration\":" << JsonString(result.configuration_name)
         << ",\"rebuffer_count\":" << result.metrics.rebuffer_count
         << ",\"rebuffer_ms\":" << result.metrics.rebuffer_ms
         << ",\"startup_delay_ms\":" << result.metrics.startup_delay_ms
         << ",\"switch_count\":" << result.metrics.switch_count
         << ",\"time_weighted_bitrate_bps\":"
         << result.metrics.time_weighted_bitrate_bps
         << ",\"qoe_score\":" << result.metrics.qoe_score
         << ",\"rebuffer_ratio\":" << result.metrics.rebuffer_ratio
         << ",\"bitrate_efficiency\":" << result.metrics.bitrate_efficiency
         << ",\"switches_per_minute\":" << result.metrics.switches_per_minute
         << ",\"sustainable_bitrate_bps\":"
         << result.metrics.sustainable_bitrate_bps << "}\n";
  return output.str();
}

std::string RenderMarkdown(const Result& result) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output
      << std::fixed << std::setprecision(3)
      << "| Trace | Configuration | Rebuffers | Rebuffer ms | Rebuffer ratio | "
         "Startup ms | Switches | Switches/minute | Time weighted bitrate bps "
         "| Sustainable bitrate bps | Bitrate efficiency | QoE score |\n"
      << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n"
      << "| " << result.trace_name << " | " << result.configuration_name
      << " | " << result.metrics.rebuffer_count << " | "
      << result.metrics.rebuffer_ms << " | " << result.metrics.rebuffer_ratio
      << " | " << result.metrics.startup_delay_ms << " | "
      << result.metrics.switch_count << " | "
      << result.metrics.switches_per_minute << " | "
      << result.metrics.time_weighted_bitrate_bps << " | "
      << result.metrics.sustainable_bitrate_bps << " | "
      << result.metrics.bitrate_efficiency << " | " << result.metrics.qoe_score
      << " |\n";
  return output.str();
}

}
