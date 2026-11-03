#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT_DIR" >&2
  exit 2
fi

out="$1"
ffmpeg_bin="${FFMPEG:-ffmpeg}"
mkdir -p "$out"

run() {
  "$ffmpeg_bin" -nostdin -hide_banner -loglevel error -y "$@"
}

video_source="testsrc2=size=160x96:rate=12:duration=2,format=yuv420p"
video_source_10="testsrc2=size=160x96:rate=12:duration=2,format=yuv420p10le"
audio_source="sine=frequency=440:sample_rate=48000:duration=1,aformat=channel_layouts=stereo"
bitexact="-fflags +bitexact -flags:v +bitexact -flags:a +bitexact -map_metadata -1"

run -f lavfi -i "$video_source" -map 0:v \
  -c:v libx264 -threads 1 -profile:v high -bf 2 -g 12 -x264-params "ref=3:no-scenecut=1" \
  $bitexact -movflags +faststart "$out/h264_high_bframes.mp4"
run -f lavfi -i "$video_source,format=yuv422p" -map 0:v \
  -c:v libx264 -threads 1 -profile:v high422 -g 12 -x264-params "no-scenecut=1" \
  $bitexact -movflags +faststart "$out/h264_high422.mp4"
run -f lavfi -i "$video_source" -map 0:v \
  -c:v libx264 -threads 1 -profile:v baseline -g 12 -x264-params "no-scenecut=1" \
  $bitexact -f h264 "$out/h264_baseline.h264"
run -f lavfi -i "$video_source" -map 0:v \
  -c:v libx265 -preset fast -x265-params "bframes=2:keyint=12:min-keyint=12:scenecut=0:pools=1:frame-threads=1:log-level=none" \
  -tag:v hvc1 $bitexact -movflags +faststart "$out/hevc_main_bframes.mp4"
run -f lavfi -i "$video_source_10" -map 0:v \
  -c:v libx265 -preset fast -profile:v main10 -x265-params "bframes=2:keyint=12:min-keyint=12:scenecut=0:pools=1:frame-threads=1:log-level=none" \
  -tag:v hvc1 $bitexact -movflags +faststart "$out/hevc_main10_bframes.mp4"
run -f lavfi -i "$video_source" -map 0:v \
  -c:v libvpx-vp9 -threads 1 -g 12 -b:v 0 -crf 33 -deadline good -cpu-used 4 \
  $bitexact "$out/vp9_profile0.webm"
run -f lavfi -i "$video_source_10" -map 0:v \
  -c:v libvpx-vp9 -threads 1 -profile:v 2 -g 12 -b:v 0 -crf 33 -deadline good -cpu-used 4 \
  $bitexact "$out/vp9_profile2.webm"
run -f lavfi -i "$video_source" -f lavfi -i "$audio_source" -map 0:v -map 1:a \
  -c:v libvpx -threads 1 -g 12 -b:v 0 -crf 30 -deadline good -cpu-used 4 \
  -c:a libvorbis -q:a 4 $bitexact "$out/vp8_vorbis.webm"

run -f lavfi -i "$audio_source" -c:a aac -b:a 96k $bitexact "$out/aac_lc.m4a"
run -f lavfi -i "$audio_source" -c:a libopus -b:a 64k $bitexact "$out/opus.ogg"
run -f lavfi -i "$audio_source" -c:a libmp3lame -b:a 128k $bitexact "$out/mp3.mp3"
run -f lavfi -i "$audio_source" -c:a ac3 -b:a 192k $bitexact "$out/ac3.ac3"
run -f lavfi -i "$audio_source" -c:a eac3 -b:a 192k $bitexact "$out/eac3.eac3"
run -f lavfi -i "$audio_source" -c:a flac $bitexact "$out/flac.flac"

for clip in h264_high_bframes.mp4 h264_baseline.h264 hevc_main_bframes.mp4 \
    hevc_main10_bframes.mp4 vp9_profile0.webm vp9_profile2.webm vp8_vorbis.webm; do
  "$ffmpeg_bin" -nostdin -hide_banner -loglevel error -y -threads 1 -i "$out/$clip" \
    -map 0:v:0 -f framemd5 "$out/${clip}.framemd5"
done

run -f lavfi -i "$video_source" -map 0:v \
  -c:v libaom-av1 -threads 1 -cpu-used 8 -b:v 0 -crf 40 -g 12 -row-mt 0 \
  $bitexact -movflags +faststart "$out/av1_main.mp4"
run -i "$out/av1_main.mp4" -map 0:v -c:v copy -f ivf "$out/av1_main.ivf"
dav1d -q --threads 1 -i "$out/av1_main.ivf" --muxer md5 -o "$out/av1_main.mp4.dav1d.md5"
rm -f "$out/av1_main.ivf"
run -f lavfi -i "$video_source,format=yuv444p" -map 0:v \
  -c:v libaom-av1 -threads 1 -cpu-used 8 -b:v 0 -crf 40 -g 12 -row-mt 0 \
  $bitexact -movflags +faststart "$out/av1_yuv444.mp4"
