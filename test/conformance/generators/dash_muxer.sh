#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  exit 2
fi

output="$1"
name="${output##*/}"
use_template=1
use_timeline=0
segment_duration=1

if [ "$name" = "segment-timeline.mpd" ]; then
  use_timeline=1
  segment_duration=2.1
elif [ "$name" = "segment-list.mpd" ]; then
  use_template=0
fi

"${FFMPEG:-ffmpeg}" -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc=size=160x90:rate=10:duration=2,format=yuv420p" \
  -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=2" \
  -map 0:v -map 1:a \
  -c:v libx264 -threads 1 -profile:v baseline -level:v 3.0 -g 10 -bf 0 \
  -x264-params scenecut=0 -c:a aac -b:a 64k \
  -fflags +bitexact -flags:v +bitexact -map_metadata -1 \
  -f dash -seg_duration "$segment_duration" -use_template "$use_template" \
  -use_timeline "$use_timeline" "$output"

if [ "$name" = "segment-list.mpd" ]; then
  python3 - "$output" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
text = text.replace('urn:mpeg:dash:profile:isoff-live:2011',
                    'urn:mpeg:dash:profile:isoff-on-demand:2011')
path.write_text(text, encoding="utf-8")
PY
fi
