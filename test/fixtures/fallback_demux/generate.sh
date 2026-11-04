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
video_src="testsrc=size=96x64:rate=10:duration=1,format=yuv420p"
audio_src="sine=frequency=440:sample_rate=48000:duration=1,aformat=channel_layouts=stereo"
x264=(-c:v libx264 -preset veryfast -threads 1 -profile:v baseline -g 5 -bf 0 -x264-params no-scenecut=1)

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${x264[@]}" -c:a libmp3lame -b:a 64k "${bitexact[@]}" "$out/h264_mp3.avi"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$audio_src" \
  -c:a libopus -b:a 32k "${bitexact[@]}" "$out/opus.ogg"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" -f lavfi -i "$audio_src" \
  "${x264[@]}" -c:a aac -b:a 64k "${bitexact[@]}" "$out/h264_aac.flv"

"$ffmpeg" "${quiet[@]}" -f lavfi -i "$video_src" \
  -c:v mpeg4 -g 5 -bf 0 "${bitexact[@]}" -movflags +faststart \
  "$out/mpeg4_part2.mp4"

"$ffmpeg" "${quiet[@]}" \
  -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=0.5,aformat=channel_layouts=stereo" \
  -c:a aac -b:a 48k "${bitexact[@]}" \
  -encryption_scheme cenc-aes-ctr \
  -encryption_key 000102030405060708090a0b0c0d0e0f \
  -encryption_kid 101112131415161718191a1b1c1d1e1f \
  "$out/encrypted_cenc.mp4"
