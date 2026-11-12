# Image runtime notices

The original dashboard/setup code is GPL-3.0-or-later. The optional image runtime uses:

- ncnn, Tencent, BSD 3-Clause, pinned commit 56775de50990ab7f16627efdcf5529b49541206f. Full upstream notices, including third-party notices, are retained in licenses/ncnn-LICENSE.txt. https://github.com/Tencent/ncnn
- Real-ESRGAN, Xintao Wang, BSD 3-Clause; unchanged generic realesr-general-x4v3 v0.2.5.0 weights. Full license in licenses/Real-ESRGAN-LICENSE.txt. https://github.com/xinntao/Real-ESRGAN
- The user-installed Python runtime uses Pillow (HPND license) and NumPy (BSD 3-Clause); retain their distribution notices when redistributing those dependencies. Their packages are not included here.

Source archives, weights, compiled adapters and user images are not part of the code-only source export. No console artwork is bundled or used to train or tune the model.

## Dashboard app binary (PPSA99735) distributed in the release

The Ex-Box 360 dashboard app shipped as the GitHub release asset `ExBox360-PPSA99735.zip` is our own
code, GPL-3.0-or-later. The **corresponding source is this repository at `main`, commit `e6474f0` or
later** — not the dashboard sources alone. Everything Ex-Box wrote or changed for this binary is in the
repo at that commit: the dashboard source, the start-up / link support sources
(`dashboard/native/support/`), the two code headers under `dashboard/assets/`, and the RADV presenter
patch with its Mesa revision and build options (`dashboard/native/radv/`). The release notes name this
commit.

Its `eboot.bin` statically links the third-party components below. Versions are those shipped by the
PS5 payload SDK bundle the dashboard was built against (taken from that bundle's pkg-config files and
build notes); confirm each against its upstream before relying on it. Full license texts live under
[`licenses/`](licenses/) — see [`licenses/README.md`](licenses/README.md) for the mapping and status.

- **Mesa 3D / RADV Vulkan driver** — the PS5_Mesa fork (Mihawk-99) at commit
  `1ba9874f478657ccf6da72ce55f0260164cc0bd0`, MIT License, plus Ex-Box's `wsi-hdr.diff` (our additions
  GPL-3.0-or-later). Mesa carries its own third-party notices in its tree.
  https://gitlab.freedesktop.org/mesa/mesa
- **SDL2** — zlib License (version per `SDL_version.h` in the payload SDK). **SDL2_ttf** 2.22.0 and
  **SDL2_image** 2.8.2 — zlib License. https://libsdl.org
- **FreeType** (2.13.x series) — The FreeType License (FTL); keep its credit line. https://freetype.org
- **HarfBuzz** — MIT (Old MIT) License. https://harfbuzz.github.io
- **libpng** 1.6.43 — PNG Reference Library License (libpng). http://www.libpng.org
- **libjpeg-turbo** 3.0.2 — IJG, BSD 3-Clause and zlib licenses. https://libjpeg-turbo.org
- **libwebp / libwebpdemux** 1.4.0 — BSD 3-Clause License. https://chromium.googlesource.com/webm/libwebp
- **bzip2 / libbzip2** — the bzip2 License (BSD-style). https://sourceware.org/bzip2
- **libsamplerate** 0.2.2 — BSD 2-Clause License. https://libsndfile.github.io/libsamplerate
- **GNU libiconv** 1.17 — LGPL-2.1-or-later. Static linking into this GPL-3.0 program is permitted
  (LGPL-2.1 section 3 allows using the library under the GPL); the LGPL-2.1 and GPL texts are provided
  under `licenses/`, and the corresponding source and relink information are available from the
  project. https://www.gnu.org/software/libiconv
- **LLVM libc++, libc++abi, libunwind, and the Clang builtins (compiler-rt)** — Apache-2.0 WITH
  LLVM-exception. https://llvm.org
- **PS5 payload SDK platform library (`libps5platform.a`) and the start-up / link recipe and linker
  scripts** — from the ps5-payload-sdk fork, PS5_Vulkan (Mihawk-99) and ps5-native-app-boilerplate
  (BlackBearReloaded); GPL-3.0-or-later per the headers in `dashboard/native/support/`. Confirm the SDK
  fork's own LICENSE for the platform library. https://github.com/ps5-payload-dev
- **dr_flac, dr_mp3, dr_wav, stb_vorbis** — covered by `dashboard/vendor/audio/NOTICE-EXBOX.md`.
- **`libav2_locale_accessor.a`, `libav2_tempfile_compat.a`** — small Ex-Box platform shims from the
  Avatar Editor's native layer (GPL-3.0-or-later, our own code); their source is in the Avatar Editor
  builder's authored tree (`avatar-editor/local-builder/authored/`).
