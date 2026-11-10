# Launch helper (companion daemon) — source goes here

The launch helper is the small background program on the PS5 that lets Ex-Box open the Avatar Editor
and Xbox 360 games itself and come back afterwards. Without it, Ex-Box still works; you just open
those from the PS5 home screen instead.

**This folder is intentionally empty of the helper's source.** The helper is privileged code (it can
launch other titles), so it is built on the user's own PC from source, never shipped as a ready-made
binary. Ex-Box Setup does not author, download or compile it; it only shows the build commands and
installs whatever the build produces.

## For the project owner: adding the source

Put the helper's source in this folder so the public download can build it:

```
launch-helper/
  Makefile            # builds build/nxe_dashboard_daemon.elf
  main.c
  <its other source files>
  COPYING             # the source's own licence (it is third-party GPL)
```

Ex-Box Setup recognises it when there is a `Makefile` here, beside `main.c`, that builds
`nxe_dashboard_daemon.elf`. Keep the original licence and credit files intact (they are also listed
in the repo's `THIRD_PARTY_NOTICES.md`). The source is public already; keeping it as source (not a
prebuilt binary) is deliberate, so anyone can read and build what they run.

## For the user: building it

1. Install the PS5 payload SDK once (the same toolchain the Avatar Editor build uses; see
   `avatar-editor/local-builder/README.md`).
2. In Ex-Box Setup, the Launch helper line shows the two commands and the folder. Run them:
   ```
   cd launch-helper
   make
   ```
3. Press **Look inside** again. Setup finds `build/nxe_dashboard_daemon.elf` and installs it, and
   writes its start-up entry.

See `docs/INSTALLER.md` for the whole picture.
