#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT_DIRECTORY" >&2
  exit 2
fi

here="$(cd "$(dirname "$0")" && pwd)"
out="$1"
mkdir -p "$out"
ffmpeg="${FFMPEG:-ffmpeg}"
quiet=(-nostdin -hide_banner -loglevel error -y)
bitexact=(-fflags +bitexact -flags:v +bitexact -flags:a +bitexact -map_metadata -1)
video_src="testsrc=size=160x90:rate=25:duration=2,format=yuv420p"
audio_src="sine=frequency=440:sample_rate=48000:duration=2"
vp9=(-c:v libvpx-vp9 -deadline realtime -cpu-used 8 -threads 1 -g 25)
opus=(-c:a libopus -b:a 64k)

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${vp9[@]}" "${opus[@]}" "${bitexact[@]}" "$out/webm_vp9_opus.webm"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  -c:v libvpx -deadline realtime -cpu-used 8 -threads 1 -g 25 \
  -c:a libvorbis -q:a 4 "${bitexact[@]}" "$out/webm_vp8_vorbis.webm"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${vp9[@]}" "${opus[@]}" "${bitexact[@]}" -live 1 "$out/webm_live_known.webm"

python3 "$here/make_unknown_size.py" "$out/webm_live_known.webm" \
  "$out/webm_unknown_size.webm"
rm "$out/webm_live_known.webm"

"$ffmpeg" "${quiet[@]}" \
  -f lavfi -i "testsrc=size=64x64:rate=10:duration=1,format=yuv420p" \
  -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" \
  -map 0:v -map 0:v -map 0:v -map 0:v -map 1:a -map 1:a -map 1:a -map 1:a \
  -map 1:a -map 1:a \
  -c:v:0 libx264 -preset veryfast -threads 1 -profile:v:0 main -bf 2 -g 5 \
  -c:v:1 libx265 -x265-params log-level=none:frame-threads=1 \
  -c:v:2 libvpx-vp9 -deadline realtime -cpu-used 8 -threads 1 \
  -c:v:3 libaom-av1 -cpu-used 8 -threads 1 -row-mt 0 \
  -c:a:0 aac -b:a:0 64k \
  -c:a:1 flac \
  -c:a:2 libmp3lame -b:a:2 64k \
  -c:a:3 ac3 -b:a:3 96k \
  -c:a:4 eac3 -b:a:4 96k \
  -c:a:5 libopus -b:a:5 64k \
  "${bitexact[@]}" -strict -2 "$out/matroska_multi_codec.mkv"
