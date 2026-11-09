# Games integration content handoff

Continuation completed and PAUSED at the owner's request. See LAUNCH-RETURN-SPEC.md, ARTWORK-CONTRACT.md, COMPATIBILITY-DESIGN.md, RENDERER-HANDOFF.md and NEEDS-FOR-MORNING.md. The added CPU artwork module requires libpng; the original library/blade modules remain dependency-free. PNG is the supported runtime artwork format. Official art is never included.

Original GPL-3.0-or-later C++17, console-independent. No bundled external assets. Library/blade modules use the standard library; the CPU art module links libpng.

`games_library.hpp` accepts a supplied list of immediate folder names from the documented `assets/roms/<Game>/` layout. It never scans or reads those folders. Each folder is a browse candidate, not a verified playable game. A folder rename changes identity. Metadata is optional: display name and an opaque key to artwork separately supplied by the owner. Missing/unresolved artwork must use the renderer's original placeholder. No ROM metadata extraction, cover downloader, remote URL fetching or PS5X360 configuration access is included. Duplicate folder records use the first metadata record. Ordering is deterministic byte ordering; case and Unicode folder identities are preserved.

For PS5, adapt the dashboard's already supplied installed-game list into `InstalledTitle`; preserve `title:<ID>` identity. Do not run a new console scan. Filtered system/homebrew entries must remain filtered by the existing provider. This adapter accepts PPSA IDs, excludes PPSA50011 itself, and does not imply that every PPSA ID is a retail game. Other platform adapters can supply Entry records with their own source IDs; the current filter UX exposes the two approved platforms only.

`games_blade.hpp` is content and controller logic for the dashboard renderer owner. It is not wired into `dash_model` or `main.cpp`. Reuse the existing Card title/image/subtitle visual fields, but carry the separate library key and action intent. Do not insert all 360 entries into `dash::App` with the same title ID: that aliases their focus/pins/history. The existing four-column viewer convention is retained.

Render the All / Xbox 360 / PS5 filter row and two show/hide controls, then the tile grid. Left/right traverses controls; A selects/toggles; down enters the grid; up from its first row or B returns to controls. Empty/search-miss/hidden-all results retain reachable controls. Expose `search(text)` via the dashboard's existing text-input affordance. It filters owned titles only; ASCII matching is case-insensitive, other UTF-8 bytes exact. Source choices never require typing. Persist filter/hide preferences through the dashboard's settings layer when integrated. Refresh preserves focused entry identity where present; otherwise selects the first tile. No arbitrary library entry cap.

360 tiles say **Open PS5X360**, with **Select game in PS5X360** subtitle. PS5 tiles say **Play**. A returns an inert intent; `dispatch_allowed` is always false. No argv, game path, process launch, daemon call or return handling exists. Unknown location availability is never claimed available; unavailable sources return an explanatory reason. Renderer must show launch-pending status and must not dispatch these intents. Folder availability does not prove installed player availability.

Public documentation reviewed on 2026-11-05:
- https://github.com/BrinooTk/PS5X360/blob/main/docs/INSTALLATION.md
- https://github.com/BrinooTk/PS5X360/blob/main/docs/BUILD.md

Neither documents game-select argv/deep links. `game_paths.txt` is discovery configuration. Current installation docs describe extra roots/external locations, superseding the brief's USB statement. The continuation also reviewed the requested COMPATIBILITY.md; no implementation inference or pinned upstream SHA is claimed.

Host gates use synthetic names and metadata only, clang++ C++17 O2, strict warnings, ASan/UBSan. Build each `*_test.cpp` separately and run the resulting executable. No full-dashboard build, visual parity, native package or hardware validation is claimed. Queue carries gate results and SHA256 records.

Next gate: dashboard owner integrates these content/focus contracts and verifies visual layout and input routing. Actual dispatch/return requires owner/Claude's companion-v4b console proof and a defined failure/return contract. Recently played and continue-last must not infer successful gameplay from opening the picker; await reliable session evidence. This avoids false play history.
