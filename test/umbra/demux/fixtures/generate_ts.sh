#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT_DIRECTORY" >&2
  exit 2
fi

out="$1"
mkdir -p "$out"
ffmpeg="${FFMPEG:-ffmpeg}"
quiet=(-nostdin -hide_banner -loglevel error -y)
bitexact=(-fflags +bitexact -flags:v +bitexact -flags:a +bitexact -map_metadata -1)
video_src="testsrc=size=160x90:rate=25:duration=2,format=yuv420p"
audio_src="sine=frequency=440:sample_rate=48000:duration=2"
x264=(-c:v libx264 -preset veryfast -threads 1 -profile:v main -g 25 -bf 2)

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${x264[@]}" -c:a aac -b:a 64k "${bitexact[@]}" -f mpegts "$out/h264_aac.ts"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  -c:v libx265 -x265-params log-level=none:frame-threads=1:keyint=25:bframes=2 \
  -tag:v hvc1 -c:a ac3 -b:a 96k "${bitexact[@]}" -f mpegts "$out/hevc_ac3.ts"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$audio_src" -f lavfi -i "$audio_src" \
  -map 0:a -map 1:a -c:a:0 mp2 -b:a:0 128k -c:a:1 eac3 -b:a:1 128k \
  "${bitexact[@]}" -f mpegts "$out/mp2_eac3.ts"
