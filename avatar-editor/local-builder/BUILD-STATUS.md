# Xbox splash Local Builder validation passed

Validated locally on November 5, 2026. The source-only download requires users' original files and compatible dependencies.

- Includes the unchanged 125 ms try-on delay and Sort/Filter on Y/Triangle.
- Produces 1080p and 4K static Xbox logo splash images from the supplied original `splash_360.png`; no original artwork is embedded in this download or the eboot.
- The existing valid runtime override takes precedence; otherwise the matching packaged image loads. Invalid/missing images retain the neutral fallback. No boot-time scaling or new display owner.
- Two native units rebuilt, 1,217 unchanged object/source hashes checked and reused. All 1,219 native units remain covered.
- Editor/runtime O2, 441 imports, five hooks, layout, SELF and LOAD caps 71/26/2/25/3 MiB PASS.
- Fresh focused delay, Sort/Filter and production splash-loader gates PASS with fatal ASan/UBSan. Actual generated 1080p/4K images passed the decoder. Existing six companions remain byte-identical.
- Builder unit tests and launcher syntax pass; see the validation receipt for the final count.

Eboot SHA256: `ef199349102fb2f37f694441d8819d1e2b95a66407530063323f7ef5553ffde0`.

This is an authored static layout using original Xbox artwork, not the animated retail boot sequence. Private generated preview inspected. No console contact, native execution, startup-time claim, installation or publishing. Prior full Save/Discard/Cancel/replay proof remains reused only for unchanged delay/save source; no fresh whole-application run is claimed.
