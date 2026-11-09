# Compatibility design review

Reviewed the complete public [PS5X360 COMPATIBILITY.md](https://github.com/BrinooTk/PS5X360/blob/main/docs/COMPATIBILITY.md), headed v0.5.6 (2026-10-06), on 2026-11-05. Mutable upstream main; no immutable revision claimed.

The document distinguishes host results, development-console observations and unverified community reports. It reports title/firmware-specific crashes, freezes and rendering failures; its table is not a supported-games catalog. Booting or reaching menus does not establish gameplay compatibility. Performance is scene/build dependent. Online authentication is not supplied by working cover downloads. Zero-frame intervals have several possible causes; the emulator's diagnostic thresholds are not guest-operation deadlines. No Xbox 360 BIOS is required. Reports should identify title/version/region, release, firmware, scene and patches without attaching game data.

Integration decisions:

- Default compatibility to **Unknown**. Folder discovery, valid art, launch acknowledgement and foreground change never upgrade it to playable.
- Do not infer compatibility or performance from synthetic host gates. If later supplied, show evidence with its version, firmware, source and observation date; no static green badges from this document.
- Preserve PPSA50011 picker fallback. No automatic cycling, forced exit or restart based on loading time/FPS.
- Keep launch transport timeout distinct from emulator loading. Never add BIOS acquisition, login promises or emulator-setting modifications to Games.

Design gate PASS: launch spec distinguishes transport from gameplay; artwork failure cannot alter launch/compatibility; renderer handoff requires unknown status and provenance. This is a documentation review, not a compatibility test of any game.
