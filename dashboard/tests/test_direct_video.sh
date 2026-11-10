#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=build/direct-video/tests
mkdir -p "$out"
exec > >(tee "$out/results.log") 2>&1
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined tests/test_direct_video.cpp modules/direct_video/omni/OmniVideoDecoder.cpp -o "$out/test" $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample)
bash tools/build_direct_video_host.sh
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined tests/test_video_transport.cpp modules/direct_video/omni/OmniVideoDecoder.cpp -o "$out/test-transport" $(pkg-config --cflags --libs sdl2 libavformat libavcodec libavutil libswscale libswresample)
ffmpeg -v error -f lavfi -i testsrc2=size=320x180:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 2 -c:v libx264 -bf 2 -c:a aac -y "$out/av.mp4"
ffmpeg -v error -i "$out/av.mp4" -an -c:v copy -y "$out/silent.mp4"
ffmpeg -v error -i "$out/av.mp4" -c:v libvpx-vp9 -c:a libopus -y "$out/av.webm"
SDL_AUDIODRIVER=dummy "$out/test-transport" "$out/av.mp4"
for clip in av.mp4 silent.mp4 av.webm; do
 ASAN_OPTIONS=detect_leaks=1 "$out/test" "$out/$clip" "$out/$clip" pixels
 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 10 build/direct-video/player "$out/$clip"
done
ffmpeg -v error -i "$out/av.mp4" -map 0:v:0 -pix_fmt rgba -sws_flags bilinear -f rawvideo -y "$out/reference.rgba"
cmp "$out/av.mp4.rgba" "$out/reference.rgba"
ffmpeg -v error -i "$out/av.mp4" -map 0:a:0 -ar 48000 -ac 2 -f s16le -y "$out/reference.pcm"
cmp "$out/av.mp4.pcm" "$out/reference.pcm"
ffmpeg -v error -f lavfi -i testsrc2=size=3840x2160:rate=60 -t 0.5 -c:v libx264 -preset ultrafast -y "$out/4k60.mp4"
ASAN_OPTIONS=detect_leaks=1 "$out/test" "$out/4k60.mp4" "$out/4k60" packets
printf 'not a video' > "$out/invalid.mp4"
set +e
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 10 build/direct-video/player "$out/invalid.mp4"
rc=$?
set -e
[[ "$rc" == 1 ]]
echo 'PASS: direct file decoding, exact RGBA/PCM, timestamps/seeks/reopen, bounded headless playback, malformed input, 4K packet intake.'
