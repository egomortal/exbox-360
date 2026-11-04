#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/direct-video
cxx=${CXX:-g++}
"$cxx" -std=c++17 -Wall -Wextra -Werror -O2 tools/direct_video_player.cpp modules/direct_video/omni/OmniVideoDecoder.cpp -o build/direct-video/player $(pkg-config --cflags --libs sdl2 libavformat libavcodec libavutil libswscale libswresample)
