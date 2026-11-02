#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT.m4a" >&2
  exit 2
fi

"${FFMPEG:-ffmpeg}" -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "aevalsrc=exprs=0.1*sin(2*PI*1000*t)|0.1*sin(2*PI*2000*t):sample_rate=48000:duration=10:channel_layout=stereo" \
  -map 0:a \
  -c:a aac -b:a 128k \
  -fflags +bitexact -flags:a +bitexact -map_metadata -1 -movflags +faststart \
  "$1"
