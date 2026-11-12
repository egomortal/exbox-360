# Ex-Box Setup (installer X18 v2)

One folder, one button. Double-click **`ExBox-Setup.exe`** (from the GitHub release; no Python needed),
or `dashboard/ExBox Setup.cmd` in a checkout. A window opens in your browser:

1. **Choose folder** - the folder that holds your Xbox 360 files. Setup only reads it.
2. **Where to install** - *Practice run* (a folder on this PC, no PS5 contacted) or *My PS5*.
3. **What was found** - every file or group Setup looks for, whether it was found, what it unlocks,
   and for missing things what you lose and where that kind of file comes from.
4. **What you'll get** - the summary, plus "New since last time" on later runs.
5. **Install everything** - does every step it can, in order, with progress and a plain result.
   Missing optional pieces are skipped, never errors.

Command line (same engine):

```sh
python -m installer scan FOLDER
python -m installer install FOLDER --practice        # full install into a practice folder
python -m installer install FOLDER --console IP      # real PS5; asks first
python -m installer doctor --practice | --console IP
python -m installer restore --practice | --console IP
python -B tests/test_installer.py                    # 21 checks, no console, no network
```

Run these from `dashboard/`. Python 3.11+ only; no extra packages (Xbox-style icons need Pillow,
otherwise the Ex-Box default icons are used and Setup says so).

## The one-file app (`ExBox-Setup.exe`)

Packaged the way Omnibloom Setup is (owner decision 2026-11-11: copy its installer approach; its PS5
install logic already matched Ex-Box's, so what was copied is its packaging):

- `dashboard/run_setup.py` is the app's entry point; `dashboard/exbox-setup.spec` builds one `.exe`
  with PyInstaller. Only git-tracked files go in: Setup, `dashboard/tools`, `dashboard/installer-icons`,
  `toolchain/`, `avatar-editor/local-builder/`, `launch-helper/` and `dashboard/setup.py`, with the
  repository layout, so Setup finds them exactly as in a checkout.
- Differences from Omnibloom's: the app keeps a small console window (Setup's window is a browser
  page; the console is how you close it), Tk is left out (Windows uses the system folder picker,
  and PyInstaller 6.16 cannot bundle Python 3.14's Tk 9), and in the app the art step runs on WSL's
  `python3`, which Setup needs anyway.
- `dashboard/build-setup-exe.ps1` builds it on Windows: own venv, Setup's tests, build, then
  `dashboard/tests/frozen_check.py` runs the built app (version, scan, a practice install into a
  temporary folder, the window's server) and prints the SHA-256. First build: 20.3 MB, check PASS.
- `.github/workflows/exbox-setup.yml` does the same on GitHub for each published release and attaches
  `ExBox-Setup.exe` and its `.sha256` to the release.
- Not yet run from the app: the WSL steps (tool setup, editor build, art preparation) and a real PS5
  install. They are the same code as from a checkout, but read their files from the app's unpacked
  folder.

## Add files from a link

A convenience for using your own files from a link, for example your own cloud storage, instead of
(or as well as) a folder on this PC. Requested by the owner, 2026-11-11.

- In section 1 of the window, paste a link, tick **"I have the right to use these files."** and press
  **Add files from this link**. Without the tick nothing is downloaded. On the command line:
  `--link URL --i-have-the-right` on `scan` or `install` (repeatable; the folder becomes optional).
- What the link points to is downloaded into a temporary folder and scanned exactly like a folder, so
  the checklist is the same: only files that pass the normal checks are used, and everything else is
  ignored. Several links can be added, and links work together with a folder. Files from links show
  in the checklist as `from link 1/...`.
- Accepted: an https link to a single file or a ZIP archive (including the link behind GitHub's
  **Download ZIP** button), a GitHub repository page (Setup fetches its default-branch archive), a
  branch or tag page (`.../tree/<name>`, that archive) or a release page (that release's files).
  Nothing else is treated specially.
- **A ZIP already on this PC needs no link:** pick it with **Choose ZIP file…** (or type its path), or
  leave it in the chosen folder. Any ZIP the checklist does not use as it is gets unpacked with the
  same safe extraction and scanned; files show as `inside <name>.zip/...`. ZIPs inside a ZIP are not
  unpacked again, and an unsafe or damaged ZIP is listed under the problems while the rest still works.
- **Progress and Cancel:** the window shows the size downloaded so far, and the total when the server
  reports it. **Cancel** stops the download and removes the partial file.
- **Plain-English failures**, each with the same fallback ("download it in your browser instead, then
  choose the downloaded file or folder in Setup"):
  - a link that needs a sign-in, is private or is gone (401, 403, 404): says so; Setup never asks for
    or stores passwords or tokens;
  - Git LFS placeholders (small pointer files instead of the real files): "this download contains
    placeholders, not the real files"; they are also named in the scan, wherever they are;
  - too large: names the cap that was hit (8 GB per link or 32 GB per session); timed out: says no
    data arrived for 60 seconds.
- Setup never suggests a link and shows no example; where files come from is the user's choice.
- **The link is never kept.** It is not written to `install-record.json`, the PC-side record, logs,
  settings or error messages, and the window clears the box after a download. Records and logs say
  only "from a link", with sizes and SHA-256 fingerprints.
- Safety (`installer/linksource.py`): https only, with no http, ftp, file or local-network addresses,
  checked again after every redirect; at most 8 GB per download and 32 GB per session, with a
  message suggesting a local folder for bigger files; 60-second timeouts. Nothing downloaded is run or
  imported. ZIP files are unpacked only if no entry has an absolute path, a `..` part or a link, and
  no entry is oversize or compressed more than 200 to 1. The temporary folder is deleted when Setup
  closes, when you press **Remove files from links**, and straight away if a download fails.
- Not supported yet: archives other than ZIP (`.7z`, `.rar`, `.tar`) are kept as plain files, so
  their contents are not scanned. The command line shows no download progress (Ctrl+C cancels). The address check happens before connecting, so a name whose
  address changes between the check and the connection is not caught (DNS rebinding).

## What Setup looks for

| Checklist line | Found when the folder has | Installs to |
|---|---|---|
| Ex-Box 360 dashboard app (needed) | `PPSA99735/eboot.bin`, or the copy shipped in `installer/payload/` | `/data/homebrew/PPSA99735` |
| Dashboard art, one line per look | original dashboard files (recognised by fingerprint), a `dashboard-resources` tree, system-update ZIPs, or art already prepared by `setup.py` | `/data/homebrew/ExBox360/...` |
| Shared artwork | `shrdres.xzp` or prepared shared art | same |
| Fonts | `xenonclatin.xtt`, or files in a `fonts` folder | `.../private_art/fonts` |
| Avatar kit | a `.nxak` kit | `.../private_art/avatar/kit360.nxak` |
| Avatar Editor | a finished build (`PPSA99734/` or the builder's ZIP), **or** the three originals (Setup then sets up the build tools itself, including `rexglue-sdk` and `XexTool-RE`, and builds headless with `avatar-editor/local-builder/builder.py`), **or** the three originals plus a ready `dependencies` tools folder | `/data/homebrew/PPSA99734` |
| Trailers | `manifest.tsv` + `packed/` from the trailer tool | `/data/homebrew/ExBox360/trailers` |
| Music | mp3 / flac / ogg / wav | `/data/nxe/music` |
| PS5X360 (optional, ticked) | nothing needed: fetched from the official release. `PPSA50011.zip` in the folder skips the download | `/data/homebrew/PPSA50011` |
| Your Xbox 360 games | `.iso`, GOD, `default.xex` - **counted only, never copied or fetched** | - |
| Loading art | `splash_360.png` (pinned) or `loading.png` | Xbox-style icons |
| Launch helper | the helper's **source** (Setup shows the two build commands; you run them), or an `exbox360-daemon.elf` / `build/nxe_dashboard_daemon.elf` you already built | `/data/pldmgr/payloads/ExBox360-Daemon` + one bare-name autoload line |

## Safety rules the code enforces

- **Allow-list.** Writes are refused outside our three title folders, `/data/homebrew/ExBox360`,
  `/data/nxe`, our payload folder, `autoload.txt`, and the name/icon files of our two titles
  (`installer/targets.py`).
- **Backup first.** A new file is uploaded beside the old one and checked; only then is the old file
  moved to `/data/nxe/install-backups/<run>/original/...`. Small files edited in place (autoload,
  names, icons, app database) are also copied to the PC. **Restore** puts back what was replaced and
  removes what was added.
- **Install memory.** `/data/nxe/install-record.json` and a copy on the PC hold file names, sizes,
  SHA-256 and counts only - never the files, never PC paths. A later run installs only what changed.
- **PS5X360** is third-party (BrinooTk, MIT). One pinned file from the official GitHub release,
  size + SHA-256 checked, installed unchanged with its `LICENSE`; never rehosted or modified. To
  update the pin, change `VERSION`, `SIZE` and `SHA256` together in `installer/ps5x360.py`.
- **Autoload** gets our helper as a bare file name (Payload Manager skips absolute paths). Other
  lines and line endings are untouched.
- Names and icons (`tools/branding.py`) run as the last step.

## Not built yet

- A real PS5 install has not been run. The FTP path exists and follows the known console quirks
  (missing-file size, transformed SELFs checked by size) but is untested on hardware.
- Console preflight is light: it checks the PS5 answers and `/data/homebrew` exists. It does not yet
  detect ShadowMountPlus or confirm titles are registered, and FTP gives no free-space figure.
- Preparing art from *raw* originals calls the existing `setup.py` pipeline; that path is wired but
  only the already-prepared path has been exercised with real files.
- The self-provisioned build tools (`toolchain/provision.py`, below) are written and unit-tested, but
  have **not yet been run end to end**: that run downloads third-party sources and runs their build
  scripts, which needs the owner's go-ahead in this session. In a practice run the tool setup and the
  editor build are described, not started (`--build-editor` starts them).
- Raw videos are found but not converted (run `import_trailers.py` first). Dashboard loading-screen
  images from `splash_360.png` are not generated here.
- Repair is "press Install again" (it re-sends only what Doctor found wrong); there is no separate button.
- Not packaged as a single Windows download; it runs from the repository with Python.
- No release has been cut yet, so the dashboard app's release pin is empty (see below).

## Build tools set up by Setup (`toolchain/`)

Building the Avatar Editor (and, later, the dashboard's native build on `main`) needs a `dependencies`
folder: the PS5 SDKs, LLVM 21, shader tools, Mesa's shader compilers, ReXGlue and so on. Setup now
builds that folder itself, inside WSL, with no admin rights:

- `toolchain/toolchain.json` pins every item to its **official upstream** at an exact commit or a
  SHA-256-checked release file (GitHub, apt.llvm.org, zlib.net, PyPI with `--require-hashes`).
  Nothing is rehosted by Ex-Box. Every fetched source is then checked file by file against the
  editor builder's own pins (`external-source-manifest.json`, `recipes/*/manifest.json`).
- `toolchain/provision.py` runs the steps in order with progress lines, remembers finished steps in
  `<deps>/.provision/state.json` (safe to stop and resume), and lays out the folder the builder
  expects. Default location: `~/.local/share/exbox-toolchain/dependencies` inside WSL.
- **One-time admin step.** It never uses `sudo`. If Ubuntu packages are missing it stops and prints
  the exact commands, with why: the Ubuntu 24.04 build packages (including `python3-pil`, whose exact
  Pillow 10.2.0 build the editor's images are checked against) and the pinned LLVM 21.1.8 packages
  from apt.llvm.org (`1:21.1.8~++20251221032922+2078da43e25a-1~exp1~20251221153059.70`, still served).
- **SDK**: rebuilt the way the owner's validated SDK was made (`bigapp/private/av2/toolchain/
  prepare-native-sdk.sh`): the official payload SDK **v0.44** release, the official **pacbrew-repo
  v0.41.1** bundle over it (its `libc++abi.a`/`libunwind.a` replace the release's), the PS5 fork's
  headers and compiler wrappers at `b83202be`, and three quoting fixes so the wrappers work in folders
  with spaces. Compared file by file with the owner's SDK: 6,474 of 6,479 shared files identical (the
  rest are five helper-script links that point at `/opt`). The same SDK serves `native-sdk` and
  `payload-sdk`. A last step checks all 48 files the editor links against by SHA-256.
- **Converter** (`ps5-native-tool`): built like the owner's (`clang++-21 -O2 ... -lz`, Ubuntu's zlib);
  byte-identical to the pin (`48bb5f0b...`).

### Test run on a clean WSL user (2026-11-10)

Provisioning reached `ready` (17 steps). The editor builder then passed all 11 stages: compile, link,
verify, companions, package. Every packaged companion matches `validation.json` exactly, including
`sce_module/libc.prx` (`e6ff45d1...`) and both splash images made from the user's original.

**The eboot is now the same in every folder (owner decision, 2026-11-11).** The program used to embed
its build folder's path (122 absolute paths from `__FILE__`, plus the shader pack's developer fallback
folder `AE_SOURCE_DIR`), so each folder gave a different eboot and the old recorded `ef199349...` could
only be matched in the owner's original folder. The builder now maps the build folder to `/exbox/run`
and the dependencies folder to `/exbox/deps` in every compile (`native_graph.py`, and the PCH in
`workflow.py`). Two full builds in different folders gave the same eboot,
**`21dbf785...`**, now recorded in `validation.json` and `BUILD-STATUS.md`, with no build-folder path
inside. That fallback folder never exists on the console, so behaviour is unchanged. A builder test
compiles the same source in two folders and checks the objects are identical.

Also found: a ReXGlue folder cloned or copied on **Windows** loses its links (43 empty files, 15 of
them compiled), so Setup now says to clone it inside Ubuntu. A folder copied without git is fine:
Setup builds from a hard-linked copy with empty git repositories in the submodule folders, and never
writes to the user's folder.

### Decided: Setup fetches ReXGlue and XexTool-RE itself (owner, 2026-11-11)

`rexglue-sdk` and `XexTool-RE` are public GitHub projects under permissive licenses (ReXGlue:
BSD-3-Clause, partly from Xenia; XexTool-RE: MIT). Their source contains the Xbox 360 executable
decryption key, which is why Setup first asked users to bring their own copies. The owner decided
Setup should be seamless instead: both are now fetched like every other tool, from their official
projects at the pinned commits (`toolchain.json` `tool_sources`: ReXGlue `c94f5ebd` with submodules,
XexTool-RE `c421d377`), checked file by file against the builder's pins, and never rehosted. The user
supplies only their three original files. A folder passed with `--rexglue-source` / `--xex-tool-source`
is still accepted if it is the right version.

**XexTool-RE fix.** The builder pins a changed `src/compress/lzx.cpp` (`f3f85f00...`). It is the
owner's 4-line fix to the LZX decoder (an uncompressed block header needs 1..16 alignment bits,
including a full word when already aligned; MIT, written independently). Setup applies it as two
pinned line changes and saves the file with the CRLF line endings the pin has. It is not on the
official project yet; sending it upstream would let the line changes go.

Fetching ReXGlue inside Ubuntu also avoids the Windows checkout problem (links turned into empty files).

### Decided: `libc.prx` is always built on the user's PC

Main session, delegated by the owner (2026-11-10): `sce_module/libc.prx` is never redistributed. It is
the PS5_Vulkan clean-room runtime shim (GPL-3.0, `tooling/native/libc_builder.cpp`), built with
`companion_producer.build_libc()` to its pinned hashes (raw ELF `8ee6e124...`, signed prx
`e6ff45d1...`). Setup builds it **once per run** with the provisioned tools and places the same
file in both titles: the Avatar Editor build already produces it, and the dashboard step adds it to
`PPSA99735/sce_module/` whenever the dashboard package does not carry it (a release asset must never
include it). If the PS5 already has one, it is kept. The installed dashboard copy reads back over FTP
as `7e82ce9a...`, the FTP view of the same raw ELF.

## How the apps get into the build (no binaries in git)

The repo holds source only. The two Ex-Box titles reach the user without ever being committed:

- **Ex-Box dashboard (PPSA99735)** - our own code, may ship prebuilt. `installer/release.py` gives it
  two routes, and Setup uses whichever is there:
  1. **Bundled**: the Windows download's build step drops the built title in
     `dashboard/installer/payload/PPSA99735/` (git-ignored). Setup installs it with no network.
  2. **Release asset**: tag an Ex-Box GitHub release, upload the title as a zip, and fill `RELEASE`
     in `release.py` (tag, asset, size, SHA-256). Setup downloads it from the official release and
     verifies the SHA-256 - the same safe path PS5X360 uses. One repo, no rename needed.
- **Avatar Editor (PPSA99734)** - built on the user's PC from *their* `avatareditor.xex` (it can't be
  a single prebuilt binary, since it's assembled from the user's own files). It can also be installed
  from a finished package the user already built.
- **Launch helper** - built from source shipped in `launch-helper/` (see below).

So a release is: cut a tag, attach the dashboard zip (and optionally the PS5 payload SDK pointer),
set the pin, and package the installer. Binaries live in the release and the download, never in git.

## Decided: the launch helper (companion) is built on the user's own PC

Owner decision, 2026-11-10. The helper is what lets Ex-Box open the Avatar Editor and Xbox 360 games
itself. It is privileged code, so the public release never carries a ready-made copy.

- Users build it on their own PC from its GPL source, the same way the Avatar Editor is built.
- **Guided build (owner, 2026-11-10).** When the helper's source is in the folder, Setup shows the
  exact commands to build it and the folder to run them in, then on the next scan installs the file
  the build produced (`build/nxe_dashboard_daemon.elf`). Setup still does **not** run the compiler
  itself - the helper is privileged code, so the user runs `make`. See `installer/helper.py`; the
  checklist line for the helper goes to a "Build it first" state with the commands.
- **Shipped with Setup.** For a public release, the helper's source ships inside Ex-Box in the
  repo-root `launch-helper/` folder, so the download can build it without the user supplying anything.
  Setup looks there when the user's own folder has no helper (`helper.bundled_source()`), shows the
  build commands pointing at that folder, and installs the result. The `launch-helper/` folder is in
  the repo with a README; the **source files themselves are added by the owner** (see below).
- Setup never contains code it authored for the helper, never downloads it, and never runs the
  compiler. It installs the `.elf` a build produced (from the shipped source, the user's folder, or
  one they already had) and writes the one bare-name autoload line (backed up first).
- Without it nothing breaks: the checklist says to open the editor and PS5X360 from the PS5 home
  screen instead.
- Adopting a community launcher payload (the other option in the brief) is not used.

Still to do for this to work in a public release:

1. **The owner adds the helper's source to `launch-helper/`** (it is already public GPL code from
   another project; keep its `COPYING`/credit files and the repo's `THIRD_PARTY_NOTICES.md`). Setup
   recognises it by a `Makefile` beside `main.c` that builds `nxe_dashboard_daemon.elf`. This step is
   the owner's because the helper is privileged code, which Setup does not author, package or ship.
2. The build needs the PS5 payload SDK. Setup's tool setup (`toolchain/provision.py`) provides it.
