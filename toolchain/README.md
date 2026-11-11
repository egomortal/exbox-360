# Build tools (set up by Ex-Box Setup)

`provision.py` builds the `dependencies` folder that `avatar-editor/local-builder/builder.py` needs
(and that the dashboard's native build uses), inside WSL Ubuntu 24.04, without admin rights.

```sh
python3 toolchain/provision.py                        # tools go to ~/.local/share/exbox-toolchain/dependencies
python3 toolchain/provision.py --rexglue-source ~/src/rexglue-sdk --xex-tool-source ~/src/XexTool-RE
python3 toolchain/provision.py --check                # what is already set up
python3 -B toolchain/test_provision.py                # its own checks (no network)
```

Ex-Box Setup runs it for you when it needs to build the Avatar Editor or `libc.prx`.

- **Pins:** `toolchain.json`. Every item is an official upstream at an exact commit or a
  SHA-256-checked release file; every fetched source is checked file by file against the editor
  builder's pins. Nothing is rehosted by Ex-Box.
- **Admin step:** if Ubuntu packages are missing it stops with exit code 3 and prints the exact
  `apt` commands (including the pinned LLVM 21.1.8 build from apt.llvm.org). It never runs `sudo`.
- **Your own two folders:** ReXGlue (`rexglue-sdk` 0.10.0, commit `c94f5ebd`, with submodules) and
  XexTool-RE (commit `c421d377`) contain the Xbox 360 decryption key in their code, so they are never
  downloaded. Without them everything else is still set up and it exits with code 4.
- **SDK:** official payload SDK v0.44 + pacbrew-repo v0.41.1 + the PS5 fork's headers and wrappers,
  exactly as the editor builder's pins were made. The last step checks the 48 files the editor links against.
- **Clone ReXGlue inside Ubuntu**, not on Windows: a Windows checkout turns its links into empty files.
- **Resumable:** finished steps are kept in `<deps>/.provision/state.json`; logs are in
  `<deps>/.provision/logs/`.
- Never downloaded: games, BIOS, keys, Microsoft game files, console firmware.

Output for Setup: `PROGRESS <n> <total> <step> <text>` lines, then one `RESULT <json>` line.
