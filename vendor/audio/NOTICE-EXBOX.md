# Audio decoders used by Ex-Box 360's Music Library

Copied unchanged on 2026-10-19 from the projects' `master` branches:

| File | Project | Version in file | Licence |
| --- | --- | --- | --- |
| `dr_mp3.h` | https://github.com/mackron/dr_libs | 0.7.4 (dev) | public domain (Unlicense) or MIT No Attribution; includes minimp3 (CC0) |
| `dr_flac.h` | https://github.com/mackron/dr_libs | 0.13.4 (dev) | public domain (Unlicense) or MIT No Attribution |
| `dr_wav.h` | https://github.com/mackron/dr_libs | 0.14.6 (dev) | public domain (Unlicense) or MIT No Attribution |
| `stb_vorbis.c` | https://github.com/nothings/stb | 1.22 | public domain or MIT |

`SHA256SUMS` lists the files as copied. `bigapp/music.hpp` compiles them into
the one translation unit that includes it (`main.cpp`, or a test).
