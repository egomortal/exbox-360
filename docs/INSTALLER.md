# Ex-Box Setup (installer X18 v2)

One folder, one button. Double-click `dashboard/ExBox Setup.cmd`. A window opens in your browser:

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

## What Setup looks for

| Checklist line | Found when the folder has | Installs to |
|---|---|---|
| Ex-Box 360 dashboard app (needed) | `PPSA99735/eboot.bin`, or the copy shipped in `installer/payload/` | `/data/homebrew/PPSA99735` |
| Dashboard art, one line per look | original dashboard files (recognised by fingerprint), a `dashboard-resources` tree, system-update ZIPs, or art already prepared by `setup.py` | `/data/homebrew/ExBox360/...` |
| Shared artwork | `shrdres.xzp` or prepared shared art | same |
| Fonts | `xenonclatin.xtt`, or files in a `fonts` folder | `.../private_art/fonts` |
| Avatar kit | a `.nxak` kit | `.../private_art/avatar/kit360.nxak` |
| Avatar Editor | a finished build (`PPSA99734/` or the builder's ZIP), **or** the three originals plus a `dependencies` tools folder (built headless with `avatar-editor/local-builder/builder.py`) | `/data/homebrew/PPSA99734` |
| Trailers | `manifest.tsv` + `packed/` from the trailer tool | `/data/homebrew/ExBox360/trailers` |
| Music | mp3 / flac / ogg / wav | `/data/nxe/music` |
| PS5X360 (optional, ticked) | nothing needed: fetched from the official release. `PPSA50011.zip` in the folder skips the download | `/data/homebrew/PPSA50011` |
| Your Xbox 360 games | `.iso`, GOD, `default.xex` - **counted only, never copied or fetched** | - |
| Loading art | `splash_360.png` (pinned) or `loading.png` | Xbox-style icons |
| Launch helper | `exbox360-daemon.elf` you built yourself | `/data/pldmgr/payloads/ExBox360-Daemon` + one bare-name autoload line |

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
- The editor build needs the user's own tools folder; a self-provisioned toolchain (brief section 2.3)
  is not built. In a practice run the build is described, not started (`--build-editor` starts it).
- Raw videos are found but not converted (run `import_trailers.py` first). Dashboard loading-screen
  images from `splash_360.png` are not generated here.
- Repair is "press Install again" (it re-sends only what Doctor found wrong); there is no separate button.
- Not packaged as a single Windows download; it runs from the repository with Python.
- The dashboard package is not in the repository (no prebuilt binaries). A release must place it in
  `dashboard/installer/payload/PPSA99735/`.

## Open decision: how the launch helper (companion) ships

The helper is what lets Ex-Box open the Avatar Editor and Xbox 360 games itself. It is privileged
code, so the public release must not carry a ready-made copy.

**Recommendation: build it on the user's own PC**, the same way the Avatar Editor is built, from its
GPL source, and let Setup install the result. Setup already works this way: it never contains or
downloads the helper; it only installs an `exbox360-daemon.elf` the user built and put in their
folder, and writes the autoload line. Without it nothing breaks - the checklist says to open the
editor and PS5X360 from the PS5 home screen instead.

What the owner still has to decide:

1. Publish the helper's source so users can build it (recommended), or leave the helper out of the
   public release entirely for now.
2. If published: whether Setup should run that build for the user (needs the same toolchain as the
   editor) or only accept a file they built themselves (what it does today).
3. The alternative in the brief - adopting an existing community launcher payload - is not
   recommended: it adds a third-party privileged dependency we cannot review or pin as easily.
