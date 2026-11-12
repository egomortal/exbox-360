# Image runtime notices

The original dashboard/setup code is GPL-3.0-or-later. The optional image runtime uses:

- ncnn, Tencent, BSD 3-Clause, pinned commit 56775de50990ab7f16627efdcf5529b49541206f. Full upstream notices, including third-party notices, are retained in licenses/ncnn-LICENSE.txt. https://github.com/Tencent/ncnn
- Real-ESRGAN, Xintao Wang, BSD 3-Clause; unchanged generic realesr-general-x4v3 v0.2.5.0 weights. Full license in licenses/Real-ESRGAN-LICENSE.txt. https://github.com/xinntao/Real-ESRGAN
- The user-installed Python runtime uses Pillow (HPND license) and NumPy (BSD 3-Clause); retain their distribution notices when redistributing those dependencies. Their packages are not included here.

Source archives, weights, compiled adapters and user images are not part of the code-only source export. No console artwork is bundled or used to train or tune the model.

## Dashboard app binary (PPSA99735) distributed in the release

The Ex-Box 360 dashboard app shipped as the GitHub release asset `ExBox360-PPSA99735.zip` is our own
code (GPL-3.0-or-later); the corresponding source is this repository at the release's tagged commit.
Its `eboot.bin` statically links the following third-party libraries. Each is distributed under its
own license; where a license requires it, the corresponding source is available from the project
linked.

- Mesa 3D, incl. the RADV Vulkan driver — MIT License (some components under other permissive
  licenses). https://www.mesa3d.org
- SDL2 (Simple DirectMedia Layer) — zlib License. https://libsdl.org
- SDL2_ttf — zlib License. https://github.com/libsdl-org/SDL_ttf
- SDL2_image — zlib License. https://github.com/libsdl-org/SDL_image
- FreeType — The FreeType License (BSD-style, with credit clause); dual-licensed with GPLv2 at the
  user's option, used here under the FreeType License. https://freetype.org
- HarfBuzz — MIT (Old MIT) License. https://harfbuzz.github.io
- libpng — PNG Reference Library License (libpng License). http://www.libpng.org/pub/png/libpng.html
- libjpeg-turbo — BSD-style (IJG, BSD 3-Clause and zlib licenses). https://libjpeg-turbo.org
- libwebp — BSD 3-Clause License. https://chromium.googlesource.com/webm/libwebp
- bzip2 / libbzip2 — BSD-style (bzip2 License). https://sourceware.org/bzip2
- libsamplerate — BSD 2-Clause License. https://libsndfile.github.io/libsamplerate
- GNU libiconv — LGPL-2.1-or-later. Static linking: the corresponding source and the information
  needed to relink are available from the project. https://www.gnu.org/software/libiconv
- LLVM libc++ and libunwind — Apache-2.0 WITH LLVM-exception. https://llvm.org
- PS5 payload SDK platform library (ps5-payload-dev) — under its upstream license. https://github.com/ps5-payload-dev/sdk

The release notes name the exact dashboard source commit. Exact library versions and full license
texts are those of the pinned PS5 payload SDK build the dashboard links against; add any missing full
license text under `licenses/` before a public release if required by that library's terms.
