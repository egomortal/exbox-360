# Building the native dashboard (PPSA99735) from source

`build_native.py` turns this repository into a signed `eboot.bin` for the Ex-Box 360 dashboard title.
It compiles every unit from source, links, checks the result and signs it. It does not download
anything, contact a console or install anything.

```bash
python3 dashboard/native/build_native.py --deps ~/exbox-dependencies --out ~/exbox-dashboard-build
```

Run it on Linux. On Windows use WSL (Ubuntu-24.04). A full build takes a few minutes. The output
folder must not exist yet; the eboot is written to `<out>/PPSA99735/eboot.bin` and the record of the
build to `<out>/build-manifest.json`.

## What the build checks

- Every source file compiles with warnings treated as errors.
- The import list is read from a first link, and a start-up checker for exactly those imports is
  generated and linked in. A build that imports a function known not to work in a title is refused.
- The segment layout must fit the dashboard limits (`../tools/check_native_layout.py`).
- The signed file must pass the title tool's own inspection.

## The dependencies folder

The build uses the same folder layout as the Avatar Editor builder
(`avatar-editor/local-builder/README.md`), plus one `dashboard/` folder. Entries can be links to
tools you already have installed.

```text
dependencies/
  native-sdk/
    bin/        prospero-clang, prospero-clang++, prospero-lld, llvm-nm
    target/lib/ libps5platform.a, libc++.a, libc++abi.a, libunwind.a and the system stub libraries
  payload-sdk/
    target/user/homebrew/   include/ and lib/ with SDL2, SDL2_ttf, SDL2_image, freetype, harfbuzz,
                            png, jpeg, webp, webpdemux, bz2, samplerate, iconv (static)
  llvm21/
    bin/clang               used only to find Clang's builtins library
  native-tools/
    ps5-native-tool         converts and signs the title
  dashboard/
    libvulkan_radeon.a      RADV built for the PS5 window system, with the HDR presenter change
    libav2_locale_accessor.a
    libav2_tempfile_compat.a
    stubs/libSceAgc.so
    stubs/libSceAgcDriver.so
```

None of these are in this repository, and none are downloaded by the build.

## What is still not closed

Being honest about what a stranger cannot do yet:

- **The tools are not provisioned for you.** Setup does not yet fetch and build the SDKs and
  libraries above from their public sources. That work is tracked with the installer.
- **`dashboard/libvulkan_radeon.a`: recipe written, rebuild not re-run.** [`radv/README.md`](radv/README.md)
  gives the Mesa revision, build options and our presenter patch. Rebuilding it from that recipe
  has not been repeated yet.
  The two small `libav2_*` archives and the two stub libraries come from the Avatar Editor's native
  platform layer and need the same treatment.
- **The eboot alone is not a title folder.** A title also needs `sce_sys/param.json`,
  `sce_sys/icon0.png` and `sce_module/libc.prx`.

## libc.prx is built, never shared

`sce_module/libc.prx` is the clean-room runtime shim from the PS5_Vulkan project (GPL-3.0). It is
built on your own PC from that project's source by `build_libc()` in
`avatar-editor/local-builder/companion_producer.py`, with pinned input and output hashes. The
dashboard and the Avatar Editor use the same file, so build it once and place it in both titles.
This repository does not ship it.

## Reproducibility

`dependencies.json` records the hashes of the dependency set used for the installed build. The
manifest lists any dependency that differs from it.

The eboot hash depends on where the repository is checked out, because compiler diagnostics embed
source paths. Built from this repository with the reference dependencies, the result has the same
408 imports and the same code size as the installed build, and a different hash. A matching hash
is only expected when the checkout path matches too; pass `--expect-eboot-sha256` if you want the
build to enforce one.

## Support sources

`support/` holds the title start-up code, the link recipe and linker scripts (GPL-3.0, adapted from
the PS5_Vulkan and ps5-native-app-boilerplate projects; notices are kept in each file), the import
wraps for functions a title cannot use, and the presenter headers the dashboard compiles against.
