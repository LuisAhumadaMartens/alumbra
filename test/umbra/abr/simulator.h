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
};

struct Result {
  std::string trace_name;
  std::string configuration_name;
  Metrics metrics;
};

Trace ReadTrace(const std::filesystem::path& path);
Trace ReadTraceText(const std::string& name, const std::string& contents,
                    bool mahimahi);
Result Run(const Trace& trace,
           const std::string& configuration_name = "default");
std::string RenderJson(const Result& result);
std::string RenderMarkdown(const Result& result);

}

#endif
