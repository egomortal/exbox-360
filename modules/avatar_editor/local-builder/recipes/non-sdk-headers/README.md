# Supplemental native include coverage

This manifest supplies original headers reached by literal includes in the accepted native source graph but absent from historical compiler dependency records. Source bodies remain in the user's pinned dependency trees. `raw_capture.h` is original project glue containing four function declarations; it is included under `authored/project/tools/native_editor/diagnostics/`.

Some headers belong to inactive platform branches. Staging them does not enable those branches or change compiler flags. All copied outputs retain the expected source bytes. Same-root source aliases are resolved to canonical files without changing their destination paths or hashes.
