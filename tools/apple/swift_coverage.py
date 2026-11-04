import argparse
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAYER_HEADER = ROOT / "umbra/player/include/umbra/player/player.h"
EVENTS_HEADER = ROOT / "umbra/player/include/umbra/player/events.h"
ABI_HEADER = ROOT / "umbra/c_api/include/umbra/c_api/c_api.h"
SWIFT_HEADER = ROOT / "tools/apple/swiftpm-consumer/Sources/AlumbraConsumer/AlumbraPlayer.swift"
PLAYER_BINDINGS = {
    "Create": ("AlumbraPlayer.init", "Bound through alumbra_player_create."),
    "~Player": ("AlumbraPlayer.deinit", "Bound through alumbra_player_destroy."),
    "Player(const Player&)": (None, "Excluded because Swift classes use reference semantics and do not clone player handles."),
    "operator=(const Player&)": (None, "Excluded because Swift classes use reference semantics and do not assign player handles."),
    "src": ("AlumbraPlayer.source", "Bound through the C ABI source getter."),
    "set_src": ("AlumbraPlayer.source", "Bound through the C ABI source setter."),
    "Load": ("AlumbraPlayer.load", "Bound."),
    "CanPlayType": (None, "Excluded because the C ABI has no canPlayType entry point."),
    "network_state": ("AlumbraPlayer.networkState", "Bound."),
    "Play": ("AlumbraPlayer.play", "Bound."),
    "Pause": ("AlumbraPlayer.pause", "Bound."),
    "paused": (None, "Excluded because the C ABI has no paused entry point."),
    "ended": (None, "Excluded because the C ABI has no ended entry point."),
    "autoplay": (None, "Excluded because the C ABI has no autoplay entry point."),
    "set_autoplay": (None, "Excluded because the C ABI has no autoplay entry point."),
    "loop": (None, "Excluded because the C ABI has no loop entry point."),
    "set_loop": (None, "Excluded because the C ABI has no loop entry point."),
    "current_time": ("AlumbraPlayer.currentTime", "Bound."),
    "set_current_time": ("AlumbraPlayer.currentTime", "Bound."),
    "FastSeek": (None, "Excluded because the C ABI has no fastSeek entry point."),
    "seeking": ("AlumbraPlayer.isSeeking", "Bound."),
    "duration": (None, "Excluded because the C ABI has no duration entry point."),
    "buffered": (None, "Excluded because the C ABI has no TimeRanges entry point."),
    "seekable": (None, "Excluded because the C ABI has no TimeRanges entry point."),
    "ready_state": ("AlumbraPlayer.readyState", "Bound."),
    "preload": (None, "Excluded because the C ABI has no preload entry point."),
    "set_preload": (None, "Excluded because the C ABI has no preload entry point."),
    "playback_rate": ("AlumbraPlayer.playbackRate", "Bound."),
    "set_playback_rate": ("AlumbraPlayer.playbackRate", "Bound."),
    "default_playback_rate": (None, "Excluded because the C ABI has no default playback rate entry point."),
    "set_default_playback_rate": (None, "Excluded because the C ABI has no default playback rate entry point."),
    "preserves_pitch": (None, "Excluded because the C ABI has no preservesPitch entry point."),
    "set_preserves_pitch": (None, "Excluded because the C ABI has no preservesPitch entry point."),
    "volume": ("AlumbraPlayer.volume", "Bound."),
    "set_volume": ("AlumbraPlayer.volume", "Bound."),
    "muted": ("AlumbraPlayer.isMuted", "Bound."),
    "set_muted": ("AlumbraPlayer.isMuted", "Bound."),
    "error": ("AlumbraPlayer.mediaError", "Bound as a Swift error value."),
    "text_tracks": (None, "Excluded because the C ABI has no text-track handle model."),
    "AddTextTrack": (None, "Excluded because the C ABI has no text-track handle model."),
    "Statistics": (None, "Excluded because the C ABI has no statistics entry point."),
    "DispatchEvents": ("AlumbraPlayer.drainEvents", "Bound as main-actor event draining."),
    "AddEventListener": ("AlumbraPlayer.events", "Bound as a main-actor AsyncStream."),
}
EVENT_TYPES = [
    "LoadStartEvent", "ProgressEvent", "SuspendEvent", "AbortEvent", "ErrorEvent",
    "EmptiedEvent", "StalledEvent", "LoadedMetadataEvent", "LoadedDataEvent",
    "CanPlayEvent", "CanPlayThroughEvent", "PlayingEvent", "WaitingEvent",
    "SeekingEvent", "SeekedEvent", "EndedEvent", "DurationChangeEvent",
    "TimeUpdateEvent", "PlayEvent", "PauseEvent", "RateChangeEvent", "ResizeEvent",
    "VolumeChangeEvent", "WarningEvent",
]
EXTRA_PUBLIC_TYPES = {
    "PlayerOptions": "Excluded because the C ABI owns task-runner configuration and does not accept C++ callbacks.",
    "FastSeekSnap": "Excluded because fastSeek is not exposed by the current C ABI.",
    "NetworkState": "Bound as the numeric AlumbraPlayer.networkState value.",
    "ReadyState": "Bound as the numeric AlumbraPlayer.readyState value.",
    "Preload": "Excluded because preload is not exposed by the current C ABI.",
    "MediaErrorCode": "Bound as AlumbraMediaError.code.",
    "MediaError": "Bound as AlumbraMediaError; catalogue detail fields are absent from the current C ABI.",
    "PlayResultCode": "Excluded because play completion results are not exposed by the current C ABI.",
    "PlayResult": "Excluded because the current C ABI discards the C++ Play callback result.",
    "PlayCallback": "Excluded because callback completion values are not exposed by the current C ABI.",
    "ElementState": "Excluded because the C ABI event payload contains only sequence, current time and duration.",
    "EventType": "Bound as AlumbraEvent cases.",
    "Subscription": "Excluded because Swift AsyncStream owns event observation lifetime.",
    "CanPlayTypeResult": "Excluded because canPlayType is not exposed by the current C ABI.",
    "TimeRange": "Excluded because range accessors are not exposed by the current C ABI.",
    "TimeRanges": "Excluded because range accessors are not exposed by the current C ABI.",
    "TextTrackKind": "Excluded because text-track handles are not exposed by the current C ABI.",
    "TextTrackMode": "Excluded because text-track handles are not exposed by the current C ABI.",
    "TextTrackListEventType": "Excluded because text-track handles are not exposed by the current C ABI.",
    "StyledTextRun": "Excluded because text-track handles are not exposed by the current C ABI.",
    "TextCue": "Excluded because text-track handles are not exposed by the current C ABI.",
    "BitmapCue": "Excluded because text-track handles are not exposed by the current C ABI.",
    "TextTrack": "Excluded because text-track handles are not exposed by the current C ABI.",
    "TextTrackList": "Excluded because text-track handles are not exposed by the current C ABI.",
    "MetricStability": "Excluded because statistics are not exposed by the current C ABI.",
    "Statistic": "Excluded because statistics are not exposed by the current C ABI.",
    "UnstableDebugStatistics": "Excluded because statistics are not exposed by the current C ABI.",
    "StatisticsSnapshot": "Excluded because statistics are not exposed by the current C ABI.",
}


def declarations(header):
    content = header.read_text(encoding="utf-8")
    if header == PLAYER_HEADER:
        content = content.split(" public:", 1)[-1].split(" private:", 1)[0]
    return set(re.findall(r"\b(?:void|bool|double|size_t|std::\w+(?:<[^;{}]+>)?|[A-Z]\w+)\s+(\w+)\s*\(", content))


def report():
    player_declarations = declarations(PLAYER_HEADER)
    missing = sorted(player_declarations - set(PLAYER_BINDINGS) - {"Create", "Player"})
    if missing:
        raise RuntimeError("Unclassified Player declarations: " + ", ".join(missing))
    event_declarations = set(re.findall(r"struct (\w+Event)\s*\{", EVENTS_HEADER.read_text(encoding="utf-8")))
    if event_declarations != set(EVENT_TYPES):
        raise RuntimeError("Event type inventory differs from events.h")
    abi = ABI_HEADER.read_text(encoding="utf-8")
    swift = SWIFT_HEADER.read_text(encoding="utf-8")
    abi_events = set(re.findall(r"ALUMBRA_EVENT_\w+", abi))
    swift_events = set(re.findall(r"ALUMBRA_EVENT_\w+", swift))
    if abi_events != swift_events:
        raise RuntimeError("C ABI and Swift event inventories differ")
    if "umbra::" in swift or "std::" in swift or '#include "umbra/' in swift:
        raise RuntimeError("Swift SDK declarations must not expose C++ types")
    lines = [
        "# Swift binding coverage",
        "",
        "Generated by `tools/apple/swift_coverage.py` from the installed C++ player headers.",
        "",
        "## Player declarations",
        "",
        "| C++ declaration | Swift surface | Status | Reason |",
        "|---|---|---|---|",
    ]
    for name in sorted(PLAYER_BINDINGS):
        surface, reason = PLAYER_BINDINGS[name]
        status = "Bound" if surface else "Excluded"
        surface_text = surface if surface else "—"
        lines.append(f"| `Player::{name}` | {surface_text} | {status} | {reason} |")
    lines.extend(["", "## Events", "", "| C++ event | Swift surface | Status |", "|---|---|---|"])
    for event in EVENT_TYPES:
        lines.append(f"| `{event}` | `AlumbraEvent` case | Bound |")
    lines.extend(["", "## Public types", "", "| C++ type | Status | Reason |", "|---|---|---|"])
    for name, reason in sorted(EXTRA_PUBLIC_TYPES.items()):
        status = "Bound" if reason.startswith("Bound") else "Excluded"
        lines.append(f"| `{name}` | {status} | {reason} |")
    lines.extend([
        "",
        "Event payload members other than sequence, current time and duration are excluded because the current C",
        "event struct does not carry them. This includes `ErrorEvent.error` and `WarningEvent.detail`.",
    ])
    lines.extend([
        "",
        "## Settings and Penumbra registration",
        "",
        "Settings get and set are bound through `alumbra_settings_get` and `alumbra_settings_set`.",
        "Penumbra registration is excluded: the current C ABI creates the reference player with an unavailable",
        "factory, and no Apple port registration entry point exists in the dependency branch. The Swift wrapper",
        "exposes player creation and reports unsupported operations through `AlumbraError.unsupported`.",
        "",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    arguments = parser.parse_args()
    result = report()
    if arguments.check:
        if arguments.check.read_text(encoding="utf-8") != result:
            raise SystemExit("Swift binding coverage report is stale")
    elif arguments.output:
        arguments.output.write_text(result, encoding="utf-8")
    else:
        sys.stdout.write(result)


if __name__ == "__main__":
    main()
