#!/usr/bin/env python3
import argparse
import pathlib
import subprocess
import sys


RATES = {
    "23.976": ("24000/1001", 24),
    "25": ("25/1", 25),
    "29.97": ("30000/1001", 30),
    "50": ("50/1", 50),
    "59.94": ("60000/1001", 60),
}
CONTAINERS = {
    "mp4": ("libx264", "aac", "mp4"),
    "ts": ("libx264", "aac", "mpegts"),
    "webm": ("libvpx-vp9", "libvorbis", "webm"),
}


def command(args):
    rate, event_frames = RATES[args.fps]
    video_codec, audio_codec, muxer = CONTAINERS[args.container]
    video_options = (
        ["-preset", "ultrafast"] if video_codec == "libx264"
        else ["-deadline", "realtime", "-cpu-used", "8"]
    )
    frame_period = event_frames / (float(rate.split("/")[0]) / float(rate.split("/")[1]))
    beep = f"if(lt(mod(t\\,{frame_period:.12g})\\,0.01)\\,0.5*sin(2*PI*1000*t)\\,0)"
    video = (
        f"testsrc=size=640x360:rate={rate}:duration={args.duration},"
        f"format=yuv420p,drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:"
        f"enable='eq(mod(n,{event_frames}),0)'"
    )
    audio = (
        f"aevalsrc=exprs='{beep}|{beep}':sample_rate={args.sample_rate}:"
        f"duration={args.duration}:channel_layout=stereo"
    )
    result = [
        args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", video,
        "-f", "lavfi", "-i", audio,
        "-map", "0:v", "-map", "1:a", "-t", str(args.duration),
        "-c:v", video_codec, *video_options,
        "-threads", "1", "-g", str(event_frames), "-bf", "0",
        "-c:a", audio_codec, "-ar", str(args.sample_rate), "-b:a", "128k",
        "-map_metadata", "-1", "-f", muxer, str(args.output),
    ]
    if args.container == "mp4":
        result[-1:-1] = ["-movflags", "+faststart"]
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate a flash/beep synchronisation clip")
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--container", choices=CONTAINERS, required=True)
    parser.add_argument("--fps", choices=RATES, required=True)
    parser.add_argument("--sample-rate", choices=(44100, 48000), type=int, required=True)
    parser.add_argument("--duration", type=int, default=600)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args(argv)
    if args.duration < 1:
        parser.error("duration must be positive")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    invocation = command(args)
    try:
        result = subprocess.run(invocation, check=False)
    except FileNotFoundError:
        print(f"{args.ffmpeg} is required to generate clips", file=sys.stderr)
        return 127
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
