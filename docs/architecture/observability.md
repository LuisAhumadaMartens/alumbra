# Logging and trace instrumentation

[ADR-0020](../adr/0020-observability-logging-tracing-qoe-metrics.md) gives Umbra its own small observability
facade and lets Penumbra install the output. This page documents the `umbra::observability` library that
implements the logging and tracing part (#50). Code comments are not used in Alumbra, so the rules live here.

The headers are in `umbra/observability/internal_include/umbra/observability/`. They are internal engine
headers and are **not installed**: the public statistics and overlay API arrives with #126, and no integrator
needs the macros before then. The core library `umbra::observability` depends only on `umbra::base` and the
Penumbra interface; `umbra::observability_settings` adds the settings binding.

## Logging

```cpp
ALUMBRA_LOG(kNet, kWarning)
        .Field("throughput_bps", throughput)
        .Field("buffer_ms", buffer)
    << "segment fetch is slow";
```

`ALUMBRA_LOG(category, level)` takes a `Category` and a `Level` enumerator name. It expands to an `if` statement
whose condition is the only code that always runs, so use it as a statement. The message and every `Field` value
are built only when the compile-time floor and the runtime filters admit the message, so their arguments are not
evaluated otherwise.

| Piece | Rule |
|-------|------|
| Categories | `net`, `manifest`, `abr`, `buffer`, `demux`, `decode`, `render`, `audio`, `text`, `ads`, `drm`, `settings`, `player` and `port`. Logs and traces share them. |
| Levels | `kVerbose`, `kDebug`, `kInfo`, `kWarning`, `kError`. |
| Fields | `Field(name, value)` takes a string, an integer, a floating-point number or a `bool`. Names are lower snake case with a unit suffix such as `_bps` or `_ms`; the facade replaces any other name with `invalid_field_name`. |
| Message | `operator<<` appends strings, integers, floating-point numbers and booleans. |
| `ALUMBRA_LOG_EVERY_N(category, level, n)` | Emits the first call and every `n`th call of that call site that passes the gates. |
| `ALUMBRA_LOG_EVERY_MS(category, level, interval_ms)` | Emits at most once per interval for that call site. |

### Build-time floor

The CMake cache variable `ALUMBRA_LOG_FLOOR` (`verbose`, `debug`, `info`, `warning`, `error` or `off`) sets the
macro `ALUMBRA_LOG_FLOOR_LEVEL` for every target that links `umbra::observability`. When it is empty, Debug
builds use `verbose` and other builds use `info`. A message below the floor is never evaluated, whatever the
runtime settings say, and changing the floor requires a rebuild. The runtime level can only raise the effective
severity above the floor.

### Runtime filters

The three settings of [ADR-0017](../adr/0017-settings-schema-and-configuration.md) drive the runtime gates:

| Setting | Effect |
|---------|--------|
| `observability.log.level` | Minimum severity that is emitted. |
| `observability.log.categories` | Categories that may log; an empty list admits every category. |
| `observability.trace.categories` | Categories that record trace events; an empty list disables tracing. |

`SettingsBinding::Create(store, runner)` applies the current values at once and then follows changes, which the
settings store delivers on the given sequenced task runner. The gates are relaxed atomics, so a change reaches
other threads without locking. Unknown category names are ignored.

### Sinks, order and redaction

`SetSink` installs a `Sink` that receives a `LogRecord`: category, level, message, typed fields, a global
sequence number and a small thread number. A sink must be thread safe. The facade calls it on the thread that
logged, so each thread sees its own records in program order; records of different threads interleave in any
order.

Before a record reaches the sink, the installed `UrlRedactor` (`SetRedactor`) redacts the message with
`RedactText` and each string field with `RedactField`. URLs shrink to an origin and a per-playback digest, and
values of credential, header and DRM or license fields never appear. See
[Status, detail and diagnostic privacy](errors.md). `RedactText` also hides the value of a `Name: value` header
line or `name=value` pair whose name is a credential or a configured token name.

Penumbra installs the output. `PortSink` formats a record as one line, `[net] message key=value`, and passes
it to a `penumbra::LogSink` with the matching severity; a rejection is counted, never retried. The desktop port
provides `penumbra::desktop::StderrLogSink`, which writes `I [net] message` lines to `std::cerr`. The `os_log`,
logcat and `console` sinks belong to the Apple, Android and web ports; they implement the same
`penumbra::LogSink` and need no change to this library. Alumbra uses neither spdlog nor Abseil logging.

## Tracing

| Macro | Meaning |
|-------|---------|
| `ALUMBRA_TRACE(category, name)` | Scope: records a begin event now and an end event when the enclosing block exits. |
| `ALUMBRA_TRACE_INSTANT(category, name)` | A point event. |
| `ALUMBRA_TRACE_COUNTER(category, name, value)` | A counter sample; `value` is evaluated only when the category records. |

`name` must be a string literal, which the macros enforce. Tracing is off until a category is enabled, so a
disabled call site costs one relaxed atomic load and a branch.

Events go to a bounded in-process ring buffer of 8192 slots. The newest events replace the oldest, writers take
no lock, and `DumpTrace()` returns a consistent snapshot in order at any time. `FormatTrace` renders a snapshot as
text lines `timestamp_ns thread phase category name [value]`, and `ClearTrace` empties the ring. Apple
`os_signpost`, web User Timing and native Perfetto output belong to later work (#264) and to the ports.

### Compiling tracing out

Setting `ALUMBRA_TRACE=OFF` in CMake defines `ALUMBRA_TRACE_ENABLED=0`. The trace macros then expand to nothing
that evaluates or names their arguments, so neither the argument expressions nor the category and scope strings
reach the binary. Two tests prove it: one counts evaluations of macro arguments in a translation unit built
without tracing, and `tools/testing/check_binary_strings.py` checks that a probe executable built without
tracing contains none of the category names or probe scope names, while the same probe built with tracing does.

## Metrics events

`metrics.h` defines the typed event interface that #126 feeds into the statistics snapshot: `PlaybackStart`,
`PlaybackStall`, `RenditionUpdate`, `FramesDropped` and `PlaybackFailed`, combined in the `MetricsEvent`
variant. `SetMetricsSink` installs a `MetricsSink`, and `ReportMetric` stamps an event and delivers it. The
statistics model itself is not part of #50.

## Benchmarks

Two Google Benchmark executables live in `test/umbra/observability/`. Both run a parse-like unit of work
(a byte-mixing loop over 16 KiB, and over 256 bytes as a stress case) with one trace scope, one counter and one
rate-limited verbose log per unit.

- `trace_disabled_benchmark` compares an uninstrumented baseline with the same unit instrumented but disabled at
  runtime. Run it with `--benchmark_repetitions=10` and compare the means against the reported standard
  deviation.
- `trace_enabled_benchmark` compares the instrumented unit with trace capture disabled, the reference, against
  the same unit with the `demux` category enabled. The overhead must stay at or below 3% of CPU time.

CI runs both briefly as smoke tests under the `benchmark` label. Baseline reporting and budget gates belong to
D-31 and #263.
