# Licenses

Full license texts for third-party code distributed with Ex-Box. See the top-level
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for what each component is and which version.

The project's own code is **GPL-3.0-or-later**; its full text is [`../COPYING`](../COPYING).

## Image runtime (already present)

- `ncnn-LICENSE.txt` — ncnn (BSD 3-Clause, with its own third-party notices).
- `Real-ESRGAN-LICENSE.txt` — Real-ESRGAN (BSD 3-Clause).

## Dashboard app binary (PPSA99735) — licenses to include before a public release

The dashboard `eboot.bin` statically links the components in `THIRD_PARTY_NOTICES.md`. Each license
below must be present as the **exact upstream text** before the release is made public. Take each from
the component's own `LICENSE`/`COPYING` file in the PS5 payload SDK bundle the dashboard was built
against (that bundle is authoritative for the shipped versions); do not paraphrase. Status:

| License | Covers | File | Status |
|---|---|---|---|
| GPL-3.0-or-later | Ex-Box code; libps5platform + start-up/link support; libav2 shims | `../COPYING` | present |
| LGPL-2.1-or-later | GNU libiconv 1.17 | `LGPL-2.1.txt` | **to add** (copy from libiconv `COPYING.LIB`) |
| Apache-2.0 WITH LLVM-exception | libc++, libc++abi, libunwind, compiler-rt | `LLVM-LICENSE.txt` | **to add** (copy from the LLVM `LICENSE.TXT`) |
| MIT | HarfBuzz; Mesa/RADV (MIT parts) | `MIT-HarfBuzz.txt`, `Mesa-LICENSE.txt` | **to add** (copy each project's text; Mesa carries its own tree notices) |
| zlib | SDL2, SDL2_ttf 2.22.0, SDL2_image 2.8.2 | `Zlib-SDL.txt` | **to add** (copy from SDL's `LICENSE.txt`) |
| FreeType License (FTL) | FreeType 2.13.x | `FTL.txt` | **to add** (copy from FreeType `docs/FTL.TXT`; keep the credit line) |
| libpng License | libpng 1.6.43 | `libpng-LICENSE.txt` | **to add** (copy from libpng `LICENSE`) |
| IJG + BSD-3 + zlib | libjpeg-turbo 3.0.2 | `libjpeg-turbo-LICENSE.txt` | **to add** (copy from libjpeg-turbo `LICENSE.md`) |
| BSD-3-Clause | libwebp/libwebpdemux 1.4.0 | `libwebp-LICENSE.txt` | **to add** (copy from libwebp `COPYING`) |
| bzip2 License | bzip2 / libbzip2 | `bzip2-LICENSE.txt` | **to add** (copy from bzip2 `LICENSE`) |
| BSD-2-Clause | libsamplerate 0.2.2 | `libsamplerate-COPYING.txt` | **to add** (copy from libsamplerate `COPYING`) |

Audio decoders (dr_flac, dr_mp3, dr_wav, stb_vorbis) are covered by
[`../dashboard/vendor/audio/NOTICE-EXBOX.md`](../dashboard/vendor/audio/NOTICE-EXBOX.md).

Reason the texts are not generated here: version- and project-specific license files (copyright lines,
FreeType's credit clause, the IJG terms) must match exactly what shipped. The payload SDK tree the
dashboard links against already contains each upstream `LICENSE` file; copy those verbatim.
