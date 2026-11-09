# Games blade to shared GPU renderer

Status: content/CPU-artwork handoff ready; renderer integration and visual proof pending its owner. No dashboard renderer, editor or shared GPU source was modified. This specifies required behavior, not an assertion that the renderer already supplies it.

## Boundary and payload

| Games supplies | Renderer/dashboard owner supplies |
| --- | --- |
| Stable Entry.key, source ID, title, platform, availability | Bind rows/tiles to stable keys; never alias 360 items by PPSA50011 |
| Tile.subtitle and action_label | Text layout, clipping, fonts, focus highlight and accessible legends |
| All/Xbox 360/PS5 and visibility controls; search API | D-pad routing, text-entry affordance, setting persistence, scroll and visible range |
| Original placeholder or immutable RGBA8 art result | Asset worker scheduling, GPU upload, texture lifetime, cache admission and failure fallback |
| Inert intent with library key and destination title ID | Pending-status UI; no dispatch until approved adapter and proof |
| Launch/return snapshot specification | Dashboard-owned persistence and lifecycle restoration |
| Compatibility design: unknown by default | No unsupported playable/FPS badges; evidence provenance if later supplied |

## Artwork exchange

Use `Catalog` mappings for owner-art keys; request only visible tiles plus the renderer's already admitted prefetch range. `Slot.begin` resets to placeholder; submit the immutable Request to an asset worker and deliver Result to the UI thread. No file access or image decode in draw/input callbacks. A recycled tile must invalidate its generation before accepting new results.

Pixels are top-left RGBA8, tightly packed, straight alpha, sRGB; dimensions are explicit. Upload on the renderer-owned queue. Hold the shared CPU reference through copy completion, publish the texture only after readiness, and retire old textures only after in-flight draws finish. GPU failure retains the placeholder; never discard the tile or block controller input. Handle device reset by invalidating GPU handles and republishing only live bindings. Do not use source paths or raw CPU pointers as permanent resource IDs.

Apply the existing shared SDR/HDR color handling. Do not reinterpret these sRGB pixels as PQ or independently change tone mapping, output format or memory policy. If the renderer expects premultiplied alpha, convert once in its existing upload stage. Use contain-fit with centered letterboxing to preserve both box and disc artwork; no aspect-ratio distortion or automatic crop. Placeholder has no logo, trade dress or borrowed artwork. Geometry, margins, animation and authentic styling remain renderer-owner decisions; this handoff does not claim reference parity.

Caller must admit encoded buffers, decoded buffers, decoder scratch, concurrent jobs and GPU residency against existing budgets. Pass per-job limits into the loader; avoid unlimited pending work and cancel/drop obsolete requests. Shared placeholder should be uploaded once. Evict offscreen art under the existing cache policy, preserving active draws. CPU module has no hidden resident cache.

## Focus and states

Four-column grid matches the current model; viewport scroll follows selected row. Up from the first row reaches filters; down from filters reaches the grid when nonempty; B returns to controls. Empty library, search miss, hidden-all, unavailable source, missing art and upload failure must retain controls and readable state. Do not merge another renderer's navigation assumptions silently; coordinate any required model change through QUEUE.

Show Xbox 360 tiles as **Open PS5X360**, subtitle **Select game in PS5X360**. PS5 uses **Play**. Until the companion is admitted, show launch pending and do not dispatch. Keep availability separate from compatibility: a folder or successful PNG decode does not establish that a game plays. No game history is recorded from picker launch.

## Renderer acceptance gate (owner executes)

| Case | Required evidence |
| --- | --- |
| Mixed 360/PS5, filter and hide controls | Every control/tile reachable, deterministic selection, no duplicate key |
| Empty/search-miss/root disconnected | Focus remains on reachable controls, placeholder/error state readable |
| Async art + rapid scroll/filter/root replacement | No stale cover on another game, no blocking I/O on UI thread |
| Tall box, square disc, transparent PNG | Correct contain fit, alpha and SDR/HDR treatment; no stretching |
| Malformed/oversize/missing art and upload rejection | Placeholder remains, no crash or tile loss |
| Device reset and repeated page entry/exit | No stale handles, use-after-free or unbounded retained allocations |
| 1080p/4K, SDR/HDR where supported | Existing visual/input/performance gates pass; no new FPS claim from host tests |
| Resume/cold start with removed selected entry | Snapshot settings restored; selection falls back safely |

Handoff review gate PASS: field ownership, pixel/alpha/color contract, worker transfer, generation rules, memory ownership, failures, navigation and acceptance cases are specified. GPU/native/visual gates remain NOT RUN here. Dependencies: host libpng is available (1.6.43); native build owner must supply and validate its approved libpng linkage or propose an equivalent decoder adapter through QUEUE.
