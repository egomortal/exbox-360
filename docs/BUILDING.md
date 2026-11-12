# Building Ex-Box 360

This repository carries the dashboard source for the build installed on the owner's console
(r23, plus the r24 Theater Party changes). No console packages, original or derived artwork,
generated guest code or AvatarEditorRecomp sources are included.

## PC test build and tests

From `dashboard/`:

```bash
make host build/test_dash build/test_library
```

This builds the desktop validation build and the model and library tests. The tests need no SDK.

## Native dashboard (PPSA99735)

The native build works from this repository, given a dependencies folder:

```bash
python3 dashboard/native/build_native.py --deps <dependencies folder> --out <fresh output folder>
```

It compiles every unit from source, links, checks imports and layout, and signs the eboot. See
[`dashboard/native/README.md`](../dashboard/native/README.md) for the folder layout, the checks and
what is still open. `make native` in `dashboard/` prints the same pointer.

Checked on 2026-11-10: a full from-source build of this tree with the reference dependencies
produced a signed eboot with 408 imports and a passing layout. It was not run on a console.

## What is still open

- **Tool provisioning.** The SDKs and libraries in the dependencies folder are not fetched or built
  for you yet. The installer is taking this on for both titles.
- **RADV archive recipe.** Written in `dashboard/native/radv/` (Mesa revision, build options and our
  presenter patch). Rebuilding the archive from that recipe has not been repeated yet. The small
  platform archives still have no written recipe in this repository.
- **Title folder.** `sce_sys/param.json` and `sce_sys/icon0.png` are produced at install time by
  Setup; they are not built here.

## Decided: libc.prx is built locally

`sce_module/libc.prx` is never redistributed. It is the PS5_Vulkan clean-room runtime shim
(GPL-3.0), built on the user's PC by the Avatar Editor builder's `build_libc()` with pinned hashes,
and the same output is used for both the dashboard and the Avatar Editor.

## Avatar Editor (PPSA99734)

Built on the user's PC from their own original files with `avatar-editor/local-builder/`. See its
`README.md`. It uses the same dependencies folder layout.
