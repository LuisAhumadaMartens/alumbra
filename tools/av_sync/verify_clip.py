#!/usr/bin/env python3
import argparse
import fractions
import json
import pathlib
import subprocess
import sys


EXPECTED = {
    "mp4": ({"mov", "mp4", "m4a", "3gp", "3g2", "mj2"}, "h264", "aac"),
    "ts": ({"mpegts"}, "h264", "aac"),
    "webm": ({"matroska", "webm"}, "vp9", "vorbis"),
}
RATES = {
    "23.976": "24000/1001",
    "25": "25/1",
    "29.97": "30000/1001",
    "50": "50/1",
    "59.94": "60000/1001",
}


class ClipError(Exception):
    pass


def media_info(path, ffprobe="ffprobe"):
    result = subprocess.run(
        [ffprobe, "-v", "error", "-show_format", "-show_streams", "-of", "json",
         str(path)],
        capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise ClipError(f"ffprobe failed: {result.stderr.strip()}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ClipError(f"ffprobe returned invalid JSON: {error}") from error


def verify(info, container, fps, sample_rate):
    if container not in EXPECTED or fps not in RATES:
        raise ClipError("unsupported container or frame rate")
    expected_format, expected_video, expected_audio = EXPECTED[container]
    formats = info.get("format", {}).get("format_name", "").split(",")
    if not expected_format.intersection(formats):
        raise ClipError(f"expected {container} container, found {formats}")
    streams = info.get("streams", [])
    videos = [stream for stream in streams if stream.get("codec_type") == "video"]
    audios = [stream for stream in streams if stream.get("codec_type") == "audio"]
    if len(videos) != 1 or len(audios) != 1:
        raise ClipError("clip must contain exactly one video and one audio stream")
    video = videos[0]
    audio = audios[0]
    if video.get("codec_name") != expected_video:
        raise ClipError(f"expected {expected_video}, found {video.get('codec_name')}")
    if audio.get("codec_name") != expected_audio:
        raise ClipError(f"expected {expected_audio}, found {audio.get('codec_name')}")
    expected_rate = fractions.Fraction(RATES[fps])
    frame_rates = {
        fractions.Fraction(value)
        for value in (video.get("r_frame_rate"), video.get("avg_frame_rate"))
        if value and value != "0/0"
    }
    if expected_rate not in frame_rates:
        raise ClipError(f"expected {fps} fps, found {sorted(map(str, frame_rates))}")
    try:
        actual_sample_rate = int(audio["sample_rate"])
    except (KeyError, TypeError, ValueError) as error:
        raise ClipError("audio stream has no valid sample rate") from error
    if actual_sample_rate != sample_rate:
        raise ClipError(f"expected {sample_rate} Hz, found {actual_sample_rate} Hz")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Verify a generated A/V sync clip")
    parser.add_argument("clip", type=pathlib.Path)
    parser.add_argument("--container", choices=EXPECTED, required=True)
    parser.add_argument("--fps", choices=RATES, required=True)
    parser.add_argument("--sample-rate", choices=(44100, 48000), type=int, required=True)
    parser.add_argument("--ffprobe", default="ffprobe")
    args = parser.parse_args(argv)
    try:
        verify(media_info(args.clip, args.ffprobe), args.container, args.fps,
               args.sample_rate)
    except (ClipError, FileNotFoundError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
