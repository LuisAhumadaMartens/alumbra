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
audio_src="sine=frequency=440:sample_rate=44100:duration=2"
x264=(-c:v libx264 -preset veryfast -threads 1 -profile:v main -g 25 -bf 2)
aac=(-c:a aac -b:a 64k)

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${x264[@]}" "${aac[@]}" "${bitexact[@]}" "$out/progressive_moov_end.mp4"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${x264[@]}" "${aac[@]}" "${bitexact[@]}" -movflags +faststart \
  "$out/progressive_faststart.mp4"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${x264[@]}" "${aac[@]}" "${bitexact[@]}" \
  -movflags +frag_keyframe+empty_moov+default_base_moof+global_sidx \
  "$out/fragmented_sidx.mp4"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$audio_src" "${aac[@]}" "${bitexact[@]}" \
  -movflags +faststart "$out/audio_priming.m4a"

"$ffmpeg" "${quiet[@]}" \
  -f lavfi -i "testsrc=size=64x64:rate=10:duration=1,format=yuv420p" \
  -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" \
  -map 0:v -map 0:v -map 0:v -map 1:a -map 1:a -map 1:a \
  -c:v:0 libx265 -x265-params log-level=none:frame-threads=1 -tag:v:0 hvc1 \
  -c:v:1 libvpx-vp9 -deadline realtime -cpu-used 8 -threads 1 \
  -c:v:2 libaom-av1 -cpu-used 8 -threads 1 -row-mt 0 \
  -c:a:0 libopus -b:a:0 64k \
  -c:a:1 ac3 -b:a:1 96k \
  -c:a:2 eac3 -b:a:2 96k \
  "${bitexact[@]}" -strict -2 "$out/multi_codec.mp4"
