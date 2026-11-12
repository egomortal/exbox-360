# The RADV archive the dashboard links (`dependencies/dashboard/libvulkan_radeon.a`)

The dashboard links Mesa's RADV driver, built for the PS5 window system, with one changed file that
adds HDR scan-out and the presenter probe (`exbox_presenter_probe`). This folder holds that change so
the archive can be rebuilt from public source. Nothing here is a binary.

## Source

- **Mesa fork:** the PS5_Mesa tree used by the PS5_Vulkan project (Mihawk-99, MIT), at commit
  `1ba9874f478657ccf6da72ce55f0260164cc0bd0`.
- **Cross file:** `tooling/radv/ps5-cross.ini` from PS5_Vulkan (GPL-3.0), commit
  `f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed`.
- **Our change:** `wsi-hdr.diff` (113 lines) against `src/vulkan/wsi/wsi_common_videoout.c`, plus
  `scanout_policy.h`, which that file includes. The original file's MIT notice is kept; our
  additions are GPL-3.0-or-later.

## Build

1. Configure and build the unpatched archive with the native SDK:

   ```text
   meson setup <build> <PS5_Mesa> --cross-file <constants with sdk path> --cross-file ps5-cross.ini \
     -Dvulkan-drivers=amd -Dgallium-drivers= -Dplatforms= -Dradv-winsys=ps5 \
     -Dllvm=disabled -Damd-use-llvm=false -Dvideo-codecs= -Dbuildtype=debugoptimized -Db_ndebug=false \
     -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
     -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dzlib=enabled --force-fallback-for=zlib \
     -Dexpat=disabled -Dxmlconfig=disabled -Dshader-cache=enabled -Dbuild-tests=false -Dvulkan-layers= \
     -Dtools= -Dmesa-clc=system -Dradv-build-id=1ba9874f478657ccf6da72ce55f0260164cc0bd0
   ninja -C <build> src/amd/vulkan/libvulkan_radeon.a
   ```

   `-Dmesa-clc=system` needs Mesa's host tools `mesa_clc` and `vtn_bindgen2` on the path; they are
   built from the same tree (the Avatar Editor's tool setup already builds them).

2. Apply `wsi-hdr.diff`, put `scanout_policy.h` beside the patched file, recompile that one file
   with the flags Meson used for it (from the build folder's `compile_commands.json`), and replace
   the archive member `wsi_common_videoout.c.o`. Add `-ffile-prefix-map=<folder of the patched
   file>=mesa/src/vulkan/wsi` and a second map for your build tree, so the file's assert messages
   do not carry a path from your PC into the eboot.

## Reference hashes

| File | SHA-256 |
|---|---|
| Unpatched archive from step 1 | `1f53ac68a8f065c20a1f80342bcb18c069b89ceea1cd7c80c0c061fe010c2ca4` |
| Patched archive, first build (embeds a private build path; do not use for a release) | `2bb567dab75d52ae30f0ba157d701f60b368a2f1d451bdd358dc837b5e8a704e` |
| Patched archive, member recompiled with the path mapped (reference) | `222b4199b5d9a8955ce707413c687849e22c56d55a5950392e2099ca728a256a` |
| `wsi-hdr.diff` | `3f6ed6902aab01d929ed4758f78a331a4b04e2b947811cf67eddd2d4f912b1ca` |

## What was verified

Step 2 was re-run on 2026-11-12 from the public Mesa file plus `wsi-hdr.diff`: the recompiled member
has the same machine code and symbols as the member in the first patched archive, and only its
embedded file name differs. Step 1 (the full unpatched archive) has not been rebuilt from scratch
here; the reference archive reuses the other 703 members of the existing build.
