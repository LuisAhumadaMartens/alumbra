#!/usr/bin/env python3
import argparse
import json
import math
import pathlib
import statistics
import sys
import xml.sax.saxutils


SCHEMA = 1
DEFAULT_DRIFT_THRESHOLD_PPM = 50.0
DEFAULT_FLASH_THRESHOLD = 0.8
DEFAULT_BEEP_THRESHOLD = 0.1
PHASES = frozenset({"startup", "after_seek", "after_rate_change", "steady_state"})


class CaptureError(Exception):
    pass


def number(value, field):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CaptureError(f"{field} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise CaptureError(f"{field} must be finite")
    return result


def percentile(values, percent):
    ordered = sorted(values)
    if not ordered:
        raise CaptureError("at least one event is required")
    position = (len(ordered) - 1) * percent / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def linear_slope(points):
    if len(points) < 2:
        return None
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    x_mean = statistics.fmean(xs)
    y_mean = statistics.fmean(ys)
    denominator = math.fsum((value - x_mean) ** 2 for value in xs)
    if denominator == 0:
        return None
    return math.fsum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys)) / denominator


def signal_events(capture, flash_threshold, beep_threshold):
    frames = capture.get("frames")
    audio = capture.get("audio")
    if not isinstance(frames, list) or not isinstance(audio, dict):
        raise CaptureError("capture must contain events or frame and audio signals")
    windows = audio.get("windows")
    if not isinstance(windows, list):
        raise CaptureError("audio.windows must be an array")
    sample_rate = number(audio.get("sample_rate_hz"), "audio.sample_rate_hz")
    first_sample = number(audio.get("first_sample_position"), "audio.first_sample_position")
    first_time = number(audio.get("first_sample_capture_ms"), "audio.first_sample_capture_ms")
    if (sample_rate <= 0 or not sample_rate.is_integer() or first_sample < 0
            or not first_sample.is_integer()):
        raise CaptureError("audio sample rate and first sample position must be valid")
    video_onsets = []
    previous_bright = False
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict):
            raise CaptureError(f"frames[{index}] must be an object")
        present_time = number(frame.get("present_ms"), f"frames[{index}].present_ms")
        luma = number(frame.get("luma"), f"frames[{index}].luma")
        if not 0 <= luma <= 1:
            raise CaptureError(f"frames[{index}].luma must be between zero and one")
        bright = luma >= flash_threshold
        if bright and not previous_bright:
            video_onsets.append((
                number(frame.get("media_time_ms"), f"frames[{index}].media_time_ms"),
                present_time,
                frame.get("phase", "steady_state"),
            ))
        previous_bright = bright
    audio_onsets = []
    previous_loud = False
    previous_position = None
    for index, window in enumerate(windows):
        if not isinstance(window, dict):
            raise CaptureError(f"audio.windows[{index}] must be an object")
        position = number(window.get("sample_position"), f"audio.windows[{index}].sample_position")
        level = number(window.get("level"), f"audio.windows[{index}].level")
        if position < first_sample or not position.is_integer():
            raise CaptureError(f"audio.windows[{index}].sample_position must be a valid integer")
        if previous_position is not None and position <= previous_position:
            raise CaptureError("audio windows must have strictly increasing sample positions")
        if not 0 <= level <= 1:
            raise CaptureError(f"audio.windows[{index}].level must be between zero and one")
        loud = level >= beep_threshold
        if loud and not previous_loud:
            audio_onsets.append(first_time + (position - first_sample) * 1000.0 / sample_rate)
        previous_loud = loud
        previous_position = position
    if not video_onsets or len(video_onsets) != len(audio_onsets):
        raise CaptureError(
            f"detected {len(video_onsets)} flashes and {len(audio_onsets)} beeps; counts must match"
        )
    return [
        {
            "media_time_ms": video[0],
            "video_present_ms": video[1],
            "audio_audible_ms": sound,
            "phase": video[2],
        }
        for video, sound in zip(video_onsets, audio_onsets)
    ]


def read_capture(path, flash_threshold=DEFAULT_FLASH_THRESHOLD,
                 beep_threshold=DEFAULT_BEEP_THRESHOLD):
    try:
        capture = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CaptureError(f"cannot read capture JSON: {error}") from error
    if (not isinstance(capture, dict) or isinstance(capture.get("schema"), bool)
            or capture.get("schema") != SCHEMA):
        raise CaptureError(f"capture schema must be {SCHEMA}")
    events = capture.get("events")
    if events is None:
        events = signal_events(capture, flash_threshold, beep_threshold)
    if not isinstance(events, list) or not events:
        raise CaptureError("events must be a non-empty array")
    calibration = capture.get("calibration", {})
    if not isinstance(calibration, dict):
        raise CaptureError("calibration must be an object")
    calibration_offset = number(calibration.get("offset_ms", 0), "calibration.offset_ms")
    uncertainty = number(calibration.get("uncertainty_ms", 0), "calibration.uncertainty_ms")
    if uncertainty < 0:
        raise CaptureError("calibration.uncertainty_ms must not be negative")
    normalized = []
    previous_capture_time = None
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            raise CaptureError(f"events[{index}] must be an object")
        media_time = number(event.get("media_time_ms"), f"events[{index}].media_time_ms")
        video_time = number(event.get("video_present_ms"), f"events[{index}].video_present_ms")
        audio_time = number(event.get("audio_audible_ms"), f"events[{index}].audio_audible_ms")
        phase = event.get("phase", "steady_state")
        if not isinstance(phase, str) or phase not in PHASES:
            raise CaptureError(f"events[{index}].phase is not a supported phase")
        capture_time = (video_time + audio_time) / 2.0
        if previous_capture_time is not None and capture_time <= previous_capture_time:
            raise CaptureError("events must be ordered by increasing capture time")
        previous_capture_time = capture_time
        normalized.append({
            "media_time_ms": media_time,
            "capture_time_ms": capture_time,
            "video_present_ms": video_time,
            "audio_audible_ms": audio_time,
            "offset_ms": video_time - audio_time - calibration_offset,
            "phase": phase,
        })
    return capture, normalized, uncertainty


def summarise(events, uncertainty, drift_threshold_ppm):
    groups = {}
    for event in events:
        groups.setdefault(event["phase"], []).append(event)
    phase_summaries = {}
    hard_failure = False
    target_exceeded = False
    for phase, group in sorted(groups.items()):
        offsets = [event["offset_ms"] for event in group]
        absolute_offsets = [abs(value) for value in offsets]
        slope = linear_slope([
            (event["capture_time_ms"], event["offset_ms"]) for event in group
        ])
        slope_ms_per_minute = None if slope is None else slope * 60000.0
        slope_ppm = None if slope is None else slope * 1_000_000.0
        failed = any(value > 40.0 or value < -60.0 for value in offsets)
        target = phase != "steady_state" or all(value <= 20.0 for value in absolute_offsets)
        settle_index = next((
            index for index in range(len(group))
            if all(abs(event["offset_ms"]) <= 20.0 for event in group[index:])
        ), None)
        settle_time = None if settle_index is None or phase == "steady_state" else (
            group[settle_index]["capture_time_ms"] - group[0]["capture_time_ms"]
        )
        hard_failure = hard_failure or failed
        target_exceeded = target_exceeded or not target
        phase_summaries[phase] = {
            "count": len(group),
            "mean_offset_ms": statistics.fmean(offsets),
            "median_offset_ms": statistics.median(offsets),
            "maximum_offset_ms": max(offsets),
            "minimum_offset_ms": min(offsets),
            "p95_absolute_offset_ms": percentile(absolute_offsets, 95),
            "maximum_absolute_offset_ms": max(absolute_offsets),
            "drift_slope_ms_per_minute": slope_ms_per_minute,
            "drift_slope_ppm": slope_ppm,
            "drifting": slope_ppm is not None and abs(slope_ppm) > drift_threshold_ppm,
            "capture_uncertainty_ms": uncertainty,
            "target_exceeded": not target,
            "hard_failure": failed,
            "settled_within_20ms": (
                None if phase == "steady_state" else settle_index is not None
            ),
            "settle_time_ms": settle_time,
        }
    all_offsets = [event["offset_ms"] for event in events]
    total_slope = linear_slope([
        (event["capture_time_ms"], event["offset_ms"]) for event in events
    ])
    total_slope_ppm = None if total_slope is None else total_slope * 1_000_000.0
    return {
        "schema": SCHEMA,
        "event_count": len(events),
        "mean_offset_ms": statistics.fmean(all_offsets),
        "median_offset_ms": statistics.median(all_offsets),
        "maximum_offset_ms": max(all_offsets),
        "minimum_offset_ms": min(all_offsets),
        "p95_absolute_offset_ms": percentile([abs(value) for value in all_offsets], 95),
        "maximum_absolute_offset_ms": max(abs(value) for value in all_offsets),
        "drift_slope_ms_per_minute": None if total_slope is None else total_slope * 60000.0,
        "drift_slope_ppm": total_slope_ppm,
        "drifting": any(value["drifting"] for value in phase_summaries.values()),
        "drift_threshold_ppm": drift_threshold_ppm,
        "capture_uncertainty_ms": uncertainty,
        "target_exceeded": target_exceeded,
        "hard_failure": hard_failure,
        "phases": phase_summaries,
    }


def svg_plot(events, output):
    width = 900
    height = 420
    margin = 54
    xs = [event["capture_time_ms"] / 1000.0 for event in events]
    ys = [event["offset_ms"] for event in events]
    x_low = min(xs)
    x_high = max(xs)
    if x_low == x_high:
        x_high = x_low + 1.0
    y_low = min(-60.0, min(ys))
    y_high = max(40.0, max(ys))
    if y_low == y_high:
        y_high = y_low + 1.0
    x = lambda value: margin + (value - x_low) * (width - 2 * margin) / (x_high - x_low)
    y = lambda value: height - margin - (value - y_low) * (height - 2 * margin) / (y_high - y_low)
    points = " ".join(f"{x(a):.2f},{y(b):.2f}" for a, b in zip(xs, ys))
    escaped = xml.sax.saxutils.escape
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width / 2}" y="24" text-anchor="middle" font-family="sans-serif" font-size="16">A/V offset: picture presentation minus audible sound</text>',
    ]
    for threshold in (-60.0, -20.0, 20.0, 40.0):
        color = "#b00020" if threshold in (-60.0, 40.0) else "#d18b00"
        lines.append(f'<line x1="{margin}" y1="{y(threshold):.2f}" x2="{width - margin}" y2="{y(threshold):.2f}" stroke="{color}" stroke-dasharray="5 5"/>')
    lines.extend([
        f'<polyline points="{points}" fill="none" stroke="#155eef" stroke-width="2"/>',
        f'<text x="{width / 2}" y="{height - 12}" text-anchor="middle" font-family="sans-serif" font-size="13">Capture time (s)</text>',
        f'<text x="16" y="{height / 2}" text-anchor="middle" transform="rotate(-90 16 {height / 2})" font-family="sans-serif" font-size="13">Offset (ms)</text>',
        f'<text x="{margin}" y="{height - margin + 22}" font-family="sans-serif" font-size="12">{escaped(f"{x_low:g}")}</text>',
        f'<text x="{width - margin}" y="{height - margin + 22}" text-anchor="end" font-family="sans-serif" font-size="12">{escaped(f"{x_high:g}")}</text>',
        '</svg>',
    ])
    pathlib.Path(output).write_text("\n".join(lines) + "\n", encoding="utf-8")


def analyse(args):
    capture, events, uncertainty = read_capture(
        args.capture, args.flash_threshold, args.beep_threshold
    )
    report = summarise(events, uncertainty, args.drift_threshold_ppm)
    metadata = capture.get("metadata", {})
    route = metadata.get("route", "") if isinstance(metadata, dict) else ""
    if isinstance(route, dict):
        route = f"{route.get('name', '')} {route.get('kind', '')}"
    report["gating_exempt"] = "bluetooth" in str(route).casefold()
    report["gating_failure"] = not report["gating_exempt"] and (
        report["hard_failure"] or report["target_exceeded"]
    )
    report["metadata"] = metadata
    report["calibration"] = capture.get("calibration", {})
    destination = pathlib.Path(args.json)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.plot:
        plot = pathlib.Path(args.plot)
        plot.parent.mkdir(parents=True, exist_ok=True)
        svg_plot(events, plot)
    print(json.dumps(report, sort_keys=True))
    return 1 if args.enforce and report["gating_failure"] else 0


def parser():
    result = argparse.ArgumentParser(description="Measure A/V timing from a capture JSON file")
    commands = result.add_subparsers(dest="command", required=True)
    command = commands.add_parser("analyse")
    command.add_argument("capture", type=pathlib.Path)
    command.add_argument("--json", required=True, type=pathlib.Path)
    command.add_argument("--plot", type=pathlib.Path)
    command.add_argument("--drift-threshold-ppm", type=float, default=DEFAULT_DRIFT_THRESHOLD_PPM)
    command.add_argument("--flash-threshold", type=float, default=DEFAULT_FLASH_THRESHOLD)
    command.add_argument("--beep-threshold", type=float, default=DEFAULT_BEEP_THRESHOLD)
    command.add_argument("--enforce", action="store_true")
    command.set_defaults(handler=analyse)
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    if (args.drift_threshold_ppm < 0 or not math.isfinite(args.drift_threshold_ppm)
            or not 0 <= args.flash_threshold <= 1
            or not 0 <= args.beep_threshold <= 1):
        print("thresholds must be finite and within their supported range", file=sys.stderr)
        return 2
    try:
        return args.handler(args)
    except CaptureError as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
