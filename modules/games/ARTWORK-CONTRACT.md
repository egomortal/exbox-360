# Runtime owner artwork

`games_art.hpp` provides a CPU-only runtime PNG pipeline linked with libpng. It does not load a texture, draw, scan game storage, download art, copy files, or embed official artwork. The shared original 32x48 RGBA placeholder is generated in memory. The test creates tiny original colored squares, not game art.

## Connect and load

1. Owner connects an artwork directory separately from game data. Configure `art::Catalog.root` with its runtime path. The Windows source folder mentioned in QUEUE is not assumed to be the console runtime path and was not inspected.
2. The dashboard supplies an explicit map from `Entry.owner_art_key` to relative PNG filename in `Catalog.bindings`. Bind that opaque key to the game's stable identity using owner metadata: title ID first when available, otherwise an owner-confirmed exact name. Do not silently fuzzy-match, assign the same-name art to multiple regions, or use PPSA50011 as an individual 360 game ID. Conflicts/unmapped entries keep placeholders. Mapping storage is dashboard-owned; no PS5X360 files are read or changed.
3. On a visible tile binding change, call `catalog.request(slot, entry.key, entry.owner_art_key)` on the UI thread. This immediately resets the slot to the shared placeholder and returns a value request.
4. Submit `art::load(request, limits)` to the application's existing asset worker. Return the value result to the UI thread; call `slot.complete(request, result)`. Generation and game identity reject completions from a previous binding, recycled tile or root. The worker must not access the Slot itself.
5. Ready pixels are immutable tightly packed RGBA8, top-left origin, straight alpha, sRGB output from libpng's simplified API. Supply them to the renderer's upload queue; render placeholder until upload succeeds. Retain CPU pixels until upload consumption completes. Never use filenames as GPU handles.
6. Artwork replacement, directory reconnection and manual library refresh reissue requests; absence/failure is not cached permanently. No directory watcher or per-frame polling. The renderer owner may retain successful resources by binding/root revision and must invalidate that cache when the revision changes.

## Format, limits and failures

This first loader supports `.png`/`.PNG` only. JPEG, disc scans and other formats remain placeholders; they are not silently interpreted as PNG. Owner-provided PNG box art can drop into the connected root without source changes. If the folder uses another format, prepare a separate local PNG directory with owner approval; this chat does not convert or bundle the official originals.

Caller-supplied Limits specify encoded byte budget, decoded RGBA byte budget and maximum dimension. The decoder checks dimensions before allocating RGBA. These are per-job staging admission limits, not a change to dashboard memory policy or an arbitrary library-size cap. The worker owner must admit concurrent jobs against its existing total memory budget, including libpng scratch space; there is no unlimited worker fan-out or CPU/GPU cache in this module.

Missing root/key/file uses placeholder. Invalid, unsupported, escaping or oversized paths/data use placeholder plus a status code; no modal spam and no effect on navigation or launch identity. Errors can be shown in Details. Read is bounded; canonical paths reject traversal and symlinks escaping the connected root. Connection assumes an owner-controlled stable local tree: canonical validation is not an OS sandbox against concurrent malicious symlink replacement. No untrusted remote directory ingestion is provided.

The focused host gate covers actual libpng decode/pixel equality, worker result handoff, placeholder, root absence, missing-to-present refresh, unmatched mapping, malformed data, unsupported format, traversal/symlink escape, encoded/decoded/dimension budgets and stale completion. Native libpng linkage and renderer upload remain integration gates, not claimed passed here.
