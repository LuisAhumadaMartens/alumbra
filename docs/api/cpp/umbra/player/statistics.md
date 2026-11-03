\file umbra/player/statistics.h
Stable values returned by a Player's QoE statistics model.

\enum umbra::player::MetricStability
Stability label attached to every statistic. `kStable` is part of the supported snapshot contract; `kUnstable` marks diagnostic values whose meaning may change.

\var umbra::player::MetricStability::kStable
The metric has a stable documented contract.

\var umbra::player::MetricStability::kUnstable
The metric is diagnostic and may change without a stable API guarantee.

\struct umbra::player::Statistic
An optional metric value and its stability label. An empty value means that no applicable sample exists.

\struct umbra::player::StatisticsSnapshot
Event-derived statistics owned by one Player and shared by that player's API consumers and debug overlays. CTA-2066 fields use its public names and formulas. Other fields use the Alumbra-specific definitions below.

The statistics model is owned for the lifetime of its Player and is destroyed with that Player.

\struct umbra::player::UnstableDebugStatistics
Explicitly unstable diagnostics returned only when the owning Player caller opts in through Statistics(true).

\var umbra::player::UnstableDebugStatistics::events_received
Number of metric events accepted by the current model; unstable diagnostic value.

\var umbra::player::UnstableDebugStatistics::last_event_timestamp_ms
Timestamp of the latest accepted metric event in milliseconds; unstable diagnostic value.

\var umbra::player::StatisticsSnapshot::unstable_debug
Absent unless a caller explicitly requests the unstable debug extension.

\var umbra::player::StatisticsSnapshot::average_initial_startup_time_s
CTA-2066 Average Initial Startup Time: arithmetic mean of successful initial startup durations in seconds. A startup failure has no startup-time sample, and a seek or resumed playback does not count as a startup.

\var umbra::player::StatisticsSnapshot::startup_failures
`startup_failures` counts failures explicitly marked as occurring before the first playback start. It is not CTA-2066 Playback Failure Percentage.

\var umbra::player::StatisticsSnapshot::playback_failure_percentage
`playback_failure_percentage` is CTA-2066 Playback Failure Percentage: the percentage of completed playback sessions with at least one playback failure. Multiple failure events in one session count once.

\var umbra::player::StatisticsSnapshot::average_playback_stalled_count
`average_playback_stalled_count` is CTA-2066 Average Playback Stalled Count: the mean stall count across all playback sessions, represented as a fractional value when needed. A stall is a buffering interruption after startup; startup buffering and seeking are excluded.

\var umbra::player::StatisticsSnapshot::average_stalled_time_percentage
`average_stalled_time_percentage` is CTA-2066 Average Stalled Time Percentage: total stall duration divided by total watched time across playback sessions, expressed as a percentage. Startup buffering and seeking duration are excluded by the event producer.

\var umbra::player::StatisticsSnapshot::average_playback_bitrate_kbps
`average_playback_bitrate_kbps` is CTA-2066 Average Playback Bitrate in kbit/s: total `bitsPlayed` divided by total session media time.

\var umbra::player::StatisticsSnapshot::average_segment_bitrate_bps
`average_segment_bitrate_bps` is the Alumbra-specific unweighted arithmetic mean in bit/s of one bitrate sample per successfully completed media segment. Failed, retried and initialisation segments are excluded. This metric is not CTA-2066 Average Playback Bitrate.

\var umbra::player::StatisticsSnapshot::rendition_switch_count
`rendition_switch_count` counts rendition updates whose old and new bitrates differ. Repeated updates to the same bitrate are excluded; this is an Alumbra aggregate, not a CTA-2066 metric.

\var umbra::player::StatisticsSnapshot::dropped_frames
`dropped_frames` is the cumulative number of video frames reported dropped by the renderer. It is an Alumbra-specific count.

\var umbra::player::StatisticsSnapshot::live_latency_ms
`live_latency_ms` is the most recently reported live-edge-to-playhead distance in milliseconds. It is absent for non-live playback and is an Alumbra-specific metric.

\var umbra::player::Statistic::value
Optional metric value. An empty value means that no applicable sample exists.

\var umbra::player::Statistic::stability
Stability contract for the metric value.

