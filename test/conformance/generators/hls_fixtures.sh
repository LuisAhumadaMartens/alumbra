#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  exit 2
fi

output="$1"
directory="$(dirname "$output")"
name="$(basename "$output" .m3u8)"
segment="$directory/$name-%03d.ts"
input="testsrc=size=320x180:rate=24:duration=8,format=yuv420p"
audio="sine=frequency=440:sample_rate=48000:duration=8"
mode="$name"
flags=()
extra=()

if [ "$mode" = "hls-live-simulator" ]; then
  extra=(-hls_list_size 3 -hls_flags delete_segments)
elif [ "$mode" = "hls-byte-range" ]; then
  extra=(-hls_flags single_file)
  segment="$directory/$name.ts"
elif [ "$mode" = "hls-aes128" ]; then
  printf '0123456789abcdef' > "$directory/key.bin"
  printf 'key.bin\n%s\n' "$directory/key.bin" > "$directory/key-info"
  extra=(-hls_key_info_file "$directory/key-info")
elif [ "$mode" = "hls-packed-audio" ]; then
  "${FFMPEG:-ffmpeg}" -nostdin -hide_banner -loglevel error -y \
    -f lavfi -i "$audio" -vn -c:a aac -b:a 96k -f adts "$directory/$name.aac"
  printf '#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:8\n#EXTINF:8,\n%s.aac\n#EXT-X-ENDLIST\n' "$name" > "$output"
  exit 0
fi

"${FFMPEG:-ffmpeg}" -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "$input" -f lavfi -i "$audio" \
  -map 0:v -map 1:a -c:v libx264 -profile:v baseline -level:v 3.0 \
  -threads 1 -g 48 -bf 0 -c:a aac -b:a 96k -hls_time 2 \
  -hls_list_size 0 -hls_segment_filename "$segment" "${extra[@]}" "$output"

if [ "$mode" = "hls-discontinuities" ]; then
  awk 'BEGIN { count = 0 } /^#EXTINF:/ { count++; if (count == 2) print "#EXT-X-DISCONTINUITY" } { print }' "$output" > "$output.tmp"
  mv "$output.tmp" "$output"
elif [ "$mode" = "hls-gaps" ]; then
  awk 'BEGIN { inserted = 0 } /^#EXTINF:/ && !inserted { print "#EXT-X-GAP"; inserted = 1 } { print }' "$output" > "$output.tmp"
  mv "$output.tmp" "$output"
fi
