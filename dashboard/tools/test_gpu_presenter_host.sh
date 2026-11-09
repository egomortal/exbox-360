#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=private/gpu-presenter-host-20261102
mkdir -p "$out"
exec > >(tee "$out/test.log") 2>&1
headers=private/av2/sdk-host-source/thirdparty/vulkan-headers/include
compiler=private/av2/toolchain/build-glslang/StandAlone/glslangValidator
reference=../../outputs/builds/dl2-candidate-50/src/engine/media
"$compiler" -V modules/gpu_presenter/video.comp -o "$out/video.spv"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -I"$headers" -I"$reference" tests/test_gpu_presenter.cpp -Wl,-l:libvulkan.so.1 -o "$out/test-presenter"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
"$out/test-presenter" "$out/video.spv"
