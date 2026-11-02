#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT.mp4" >&2
  exit 2
fi

beep="if(lt(mod(n,48000),1600),0.5*sin(2*PI*1000*t),0)"

"${FFMPEG:-ffmpeg}" -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc=size=640x360:rate=1:duration=300,settb=1/30,setpts=N,fps=30,format=yuv420p,drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='eq(mod(n,30),0)'" \
  -f lavfi -i "aevalsrc=exprs='${beep}|${beep}':sample_rate=48000:duration=10:channel_layout=stereo" \
  -map 0:v -map 1:a \
  -c:v libx264 -threads 1 -profile:v high -level:v 3.0 -g 30 -bf 0 \
  -c:a aac -b:a 128k \
  -fflags +bitexact -flags:v +bitexact -flags:a +bitexact -map_metadata -1 -movflags +faststart \
  "$1"
