# Dashboard source

Everything that builds the Ex-Box 360 native dashboard title (PPSA99735) lives here. The folder layout is
kept exactly as the build expects, so run every command from inside this folder.

| Path | What it is |
|---|---|
| `main.cpp`, `dash_model.*`, `ps5_library.*`, `*.hpp` | The dashboard program: model, layout, renderer and input |
| `ui/` | One file per screen or panel (Guide, blades, settings, media pages …) |
| `runtime/` | Scenes, music, video and sound playback |
| `modules/` | Larger pieces: avatar editor hand-off, avatar slots, video, GPU presenter, host bridge |
| `vendor/` | Third-party code, with its licences |
| `tests/` | Host tests (`python -B tests/<name>.py`, or `make test` for the C++ ones) |
| `tools/` | Build helpers and audit scripts used while reconstructing the look |
| `Makefile`, `build-wsl.sh` | Build the title in WSL with the PS5 payload SDK |
| `ExBox Setup.cmd`, `setup.py`, `launch_setup.py` | The Windows setup app (installer) |
| `tools/branding.py`, `installer-icons/` | Installer step: app names (Ex-Box 360 / Ex-Box Avatars) and home-screen icons |
| `deploy.py`, `deploy_update.py`, `activate.py` | Copy a build to your console over FTP |
| `run_*_audit.py`, `audit_*.py`, `verify_*.py` | Visual and media checks against your own original files |

Build steps are in [`../docs/BUILDING.md`](../docs/BUILDING.md).

Scripts that need your own original files read them from `private/` inside this folder (ignored by git),
or from the folder named in `EXBOX_ASSET_DIR`.
