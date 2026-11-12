# Licenses

Full license texts for third-party code distributed with Ex-Box. See the top-level
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for what each component is and which version.

The project's own code is **GPL-3.0-or-later**; its full text is [`../COPYING`](../COPYING).

## Image runtime (already present)

- `ncnn-LICENSE.txt` — ncnn (BSD 3-Clause, with its own third-party notices).
- `Real-ESRGAN-LICENSE.txt` — Real-ESRGAN (BSD 3-Clause).

## Dashboard app binary (PPSA99735)

The dashboard `eboot.bin` statically links the components in `THIRD_PARTY_NOTICES.md`. Each license
text below is the exact upstream file for the version that shipped.

| License | Covers | File | Source of the text |
|---|---|---|---|
| GPL-3.0-or-later | Ex-Box code; libps5platform + start-up/link support; libav2 shims | `../COPYING` | project |
| LGPL-2.1-or-later | GNU libiconv 1.17 | `LGPL-2.1.txt` | FSF text, as shipped in Ubuntu `common-licenses` |
| Apache-2.0 WITH LLVM-exception | libc++, libc++abi, libunwind, compiler-rt | `LLVM-LICENSE.txt` | llvm-project `LICENSE.TXT` |
| MIT ("Old MIT") | HarfBuzz 9.0.0 | `MIT-HarfBuzz.txt` | harfbuzz 9.0.0 `COPYING` |
| MIT and others | Mesa / RADV (PS5_Mesa fork, 1ba9874) | `Mesa-LICENSE.txt` | that tree's `docs/license.rst` |
| zlib | SDL2 2.30.12 | `Zlib-SDL.txt` | SDL release-2.30.12 `LICENSE.txt` (matches the SDK bundle copy) |
| zlib | SDL2_ttf 2.22.0 | `Zlib-SDL_ttf.txt` | SDL_ttf release-2.22.0 `LICENSE.txt` |
| zlib | SDL2_image 2.8.2 | `Zlib-SDL_image.txt` | SDL_image release-2.8.2 `LICENSE.txt` |
| FreeType License (FTL) | FreeType 2.13.2 | `FTL.txt` | freetype VER-2-13-2 `docs/FTL.TXT` |
| libpng License | libpng 1.6.43 | `libpng-LICENSE.txt` | libpng v1.6.43 `LICENSE` |
| IJG + BSD-3 + zlib | libjpeg-turbo 3.0.2 | `libjpeg-turbo-LICENSE.txt` | libjpeg-turbo 3.0.2 `LICENSE.md` |
| BSD-3-Clause | libwebp / libwebpdemux 1.4.0 | `libwebp-LICENSE.txt` | libwebp v1.4.0 `COPYING` |
| bzip2 License | bzip2 1.0.8 | `bzip2-LICENSE.txt` | bzip2-1.0.8 `LICENSE` |
| BSD-2-Clause | libsamplerate 0.2.2 | `libsamplerate-COPYING.txt` | libsamplerate 0.2.2 `COPYING` |

Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.

Audio decoders (dr_flac, dr_mp3, dr_wav, stb_vorbis) are covered by
[`../dashboard/vendor/audio/NOTICE-EXBOX.md`](../dashboard/vendor/audio/NOTICE-EXBOX.md).

Reason the texts are not generated here: version- and project-specific license files (copyright lines,
FreeType's credit clause, the IJG terms) must match exactly what shipped. The payload SDK tree the
dashboard links against already contains each upstream `LICENSE` file; copy those verbatim.
