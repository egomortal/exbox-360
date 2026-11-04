# Current native dashboard source export

This snapshot carries installed N4 dashboard source (PPSA99735, e5e9f299), including N2 native audio. Reviewed standalone host-bridge and decoder include relocations are preserved. Independent X-track candidates are not combined. No console packages, original/derived artwork, generated guest code or AvatarEditorRecomp sources are included.

`make host build/test_dash build/test_library` builds desktop validation. `make native` stops with a dependency explanation. The old loader eboot target was removed so it cannot be mistaken for PPSA99735.

## Native build closure pending

Internal N4 uses a pinned native SDK, separately built SDL/static libraries, platform compatibility archives, native startup/link support and packaging tool. Source recipes, transitive licences and clean-machine dependency closure are not yet included. Inherited sce_module/libc.prx needs a documented user-input or redistributable-source provenance decision and is excluded. Editor PPSA99734 needs its own build contract from the editor chat. A staged-package installer does not close these gaps.

No Git or publishing actions were performed. Claude reviews/publishes updates. Host results and source manifest are adjacent to this export. Frozen fallback and N4 packages remain unchanged.
