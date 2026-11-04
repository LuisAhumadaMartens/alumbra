#ifndef TEST_UMBRA_ABR_SIMULATOR_H_
#define TEST_UMBRA_ABR_SIMULATOR_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace umbra::abr::simulation {

struct TracePoint {
  std::int64_t time_ms;
  std::int64_t throughput_bps;
};

struct Trace {
  std::string name;
  std::vector<TracePoint> points;
};

struct Metrics {
  std::uint64_t rebuffer_count;
  std::int64_t rebuffer_ms;
  std::int64_t startup_delay_ms;
  std::uint64_t switch_count;
  double time_weighted_bitrate_bps;
  double qoe_score;
  double rebuffer_ratio;
  double bitrate_efficiency;
  double switches_per_minute;
  double sustainable_bitrate_bps;
};

struct Result {
  std::string trace_name;
  std::string configuration_name;
  Metrics metrics;
};

struct Thresholds {
  double max_rebuffer_ratio;
  double min_bitrate_efficiency;
  double max_switches_per_minute;
};

struct Baseline {
  std::string trace_name;
  double rebuffer_ratio;
  double bitrate_efficiency;
  double switches_per_minute;
  Thresholds thresholds;
};

struct Evaluation {
  Result result;
  Baseline baseline;
  bool passed = false;
};

Trace ReadTrace(const std::filesystem::path& path);
Trace ReadTraceText(const std::string& name, const std::string& contents,
                    bool mahimahi);
Result Run(const Trace& trace,
           const std::string& configuration_name = "default");
std::string RenderJson(const Result& result);
std::string RenderMarkdown(const Result& result);
std::vector<Baseline> ReadBaseline(const std::filesystem::path& path);
Evaluation Evaluate(const Result& result, const Baseline& baseline);
std::string RenderEvaluationMarkdown(const Evaluation& evaluation);

}

#endif
