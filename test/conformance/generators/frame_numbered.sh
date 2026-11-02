#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT.mp4" >&2
  exit 2
fi

"${FFMPEG:-ffmpeg}" -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc=size=640x360:rate=1:duration=300,settb=1/30,setpts=N,fps=30,format=yuv420p" \
  -map 0:v \
  -c:v libx264 -threads 1 -profile:v high -level:v 3.0 -g 30 -bf 0 \
  -fflags +bitexact -flags:v +bitexact -map_metadata -1 -movflags +faststart \
  "$1"
