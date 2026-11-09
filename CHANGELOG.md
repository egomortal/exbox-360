# Changelog

Dates are when a change was tested or installed on the owner's PS5. "Experimental" means it works
on the PC test harness but has not been accepted on the TV yet.

## 2026-11-09: editor builder, Games integration, docs

### Names and icons
- The two apps are now called **Ex-Box 360** and **Ex-Box Avatars** on the PS5 home screen.
- New home-screen icons, chosen in the installer under **Icons**: "Xbox style (from my files)", made on your
  PC from your own loading art and avatar, or "Ex-Box default", our own artwork in `dashboard/installer-icons/`.
  The generator is `dashboard/tools/make_app_icons.py`.
- The installer applies both with `dashboard/tools/branding.py`. It also fixes the name on a console that
  registered an older name, backs up everything it replaces, and asks for a restart.

### Added to this repository
- **`avatar-editor/local-builder/`**: a source-only builder for the native Avatar Editor.
  It builds from your own original files on your own PC (Windows + WSL). It ships no editor binary,
  no fonts, no artwork and no SDK. See its `README.md`.
- **`games/`**: the Games blade library model, tiles, filters, artwork contract, and the
  launch/return design for Xbox 360 games through PS5X360.

### Avatar Editor (installed on the console, Nov 5–6)
- **Closet try-on delay (125 ms):** the highlight moves instantly, and the avatar only puts an item on once
  you stop on it. Fast scrolling through clothes stays smooth instead of loading every item you pass.
- **Sort / Filter on Y (Triangle):** this replaces the old Marketplace button. You can filter by type, source
  game, category and color, and sort by name. The game list builds itself from what you own, and items
  with no tag show up as "Other".
- **Xbox 360 splash:** a static loading screen generated from your own original orb-and-wordmark file.
- **Clean exit and "(B) Dashboard":** B on the main screen offers a return to the dashboard, and
  save, discard and cancel all exit cleanly.

### Ex-Box dashboard (installed builds r20–r23, Nov 6–7)
- **Your real avatar on the dashboard:** your saved editor appearance and three preset friends, using the
  original idle animations and the original wave. Friends stand on a back row behind you, as on the retail
  Metro dashboard. Selecting a friend makes them wave.
- **Loading splash** shared with the editor.
- **Installed-games list keeps your games:** PS5 games no longer vanish from the list when the
  system's game feed is briefly unavailable.
- **Dashboard features 1–10:**
  - avatar idle fidgets
  - Friends stage
  - Continue Playing
  - seven Music views with queue and activity
  - local promotions
  - Guide music icons navigable left/right
  - a full D-pad reachability sweep (Pictures and Videos grids no longer jump diagonally)
  - a read-only Compatibility & Health page
- **Themes:** compatibility layer and Theme Doctor for owner-supplied themes.
- **Experimental:** an in-game Guide preview for Xbox 360 games, and a GPU presenter (Stage 1) behind
  opt-in flags. On the TV the GPU path broke controller navigation, so it stays off by default.

### Xbox 360 games (PS5X360), design ready
- PS5X360 (by BrinooTk, MIT) can boot straight into a chosen game, skipping its own library.
  At startup it reads a one-time `launch-once.txt` from its storage folder. Line 1 is the game file
  (`.iso`/`.god` file or a folder's `default.xex`), line 2 is the title. It deletes the file after
  reading it. No change to PS5X360 is needed.
- Planned flow: dashboard → Xbox loading splash → brief black → the game's own boot screens.
- The installer will offer to download PS5X360 from its **official** GitHub release, pinned and
  checksum-verified, with credit to the author. Games, BIOS files and keys are never bundled or downloaded.

### Launching between apps
- Root cause found: the companion launcher was not starting at boot, because its autoload entry used a full
  path, which Payload Manager skips silently.
- A new companion that can launch installed titles by title ID has been built and loads correctly on the
  console. It is being tested before it becomes the default.
- The companion source is not published here. It includes privileged loader code that this repository
  deliberately does not distribute.

## Earlier
- 2026-11-05: repository front page.
- Native-title dashboard source (N4 base) and the initial clean export (X10).
