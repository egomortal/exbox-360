#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/direct-video
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -DNXE_DIRECT_VIDEO -I../native-app/src modules/editor_dashboard/bridge.cpp modules/avatar_editor/native/return_ticket.cpp modules/avatar_editor/native/ticket_file.cpp main.cpp dash_model.cpp ps5_library.cpp ../native-overlay/nxe_bridge.cpp modules/direct_video/omni/OmniVideoDecoder.cpp -o build/direct-video/exbox-host $(pkg-config --cflags --libs sdl2 SDL2_ttf SDL2_image libavformat libavcodec libavutil libswscale libswresample)
