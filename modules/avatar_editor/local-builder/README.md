# Local Avatar Editor Builder

Build the editor with the **125 ms closet try-on delay, Sort / Filter on Y/Triangle and an Xbox 360 splash screen** using your own original files and compatible dependencies. The highlight moves immediately; trying on the item waits until focus settles.

Extract the builder download and double-click **Start Builder.cmd**. Choose your originals folder, dependencies folder and a separate output folder, then click **Build**. The app validates versions, generates resources, compiles locally, checks the result and packages it. Cancel stops this build and preserves its logs.

**No ready-built editor, game files, fonts, SDKs or private assets are included.** The app does not download missing dependencies, install software, contact a console or upload anything. Windows Python is not required. An existing WSL Linux installation with Python 3.11 or newer is required.

The start script uses a process-only PowerShell execution-policy setting for the included launcher. It does not change your saved Windows execution policy.

## Sort / Filter controls

In a closet item grid, press **Y (Triangle)**. Use Left/Right to choose Type, Source game, Category, Color or Sort; press A to open the choices, Up/Down to browse and A to apply. B goes back, Y closes the menu, and X on the field row clears filters. LB/RB jumps through choice initials. Choices come from the supplied owned catalog; missing tags appear as Other.

A bottom-row **Y Sort / Filter** hint uses the original Y icon and font from your files, through the existing legend renderer. It appears in closet grids, beside the existing hints. The original five companions remain unchanged; a sixth legend file is generated locally. No original artwork is included in the builder download.
## Xbox 360 splash

The builder generates 1080p and 4K loading images from your own original Xbox 360 orb-and-wordmark image, `splash_360.png`. The logo is centered on a white background with its aspect ratio preserved. This is an authored static loading screen using the original artwork, not a recreation of the animated Xbox boot sequence. The generated images stay outside the eboot in `assets/ui/loading-1080p.png` and `assets/ui/loading-2160p.png`.

Supply the original PNG from the 2.0.9199.0 XAM shared resources (`u/xam/shrdres/splash_360.png`), 671 by 416 pixels, SHA256 `2881b4e94e386d6e15b03dc4e229ac006e3897e36d38b21c14a151198c0081fb`. Copy it to your originals folder; the builder never downloads it. Other images or modified copies are rejected. No logo pixels are bundled in this download.

At startup, a valid `/data/nxe/loading/current.png` of the exact presentation size still takes precedence. Otherwise the editor uses the packaged image for 1920x1080 or 3840x2160. Missing, invalid or unsupported-size images fall back to the existing neutral screen. No boot-time resizing or console setup is performed by the builder.

## Supply your files

Keep inputs outside the extracted builder folder. The originals folder contains:

```text
originals/
  avatareditor.xex
  xenonclatin.xtt
  splash_360.png
```

Use originals you are entitled to use. Supported hashes are in `guest-contract.json` and `companion-inputs.json`. Images and the font are extracted locally; a previous editor build is not an input.

The dependencies folder uses this layout. Directories can be WSL links to your existing installations. Preserve source trees, tool installation layouts and their dependent libraries.

```text
dependencies/
  sources/
    avatar_editor/
    rexglue_sdk/
    mesa-source/
    payload-sdk-source/
    vulkan-port-source/
    glm-source/
    fast-float-source/
    xex-tool/
  native-sdk/
    bin/                 # prospero-clang, prospero-clang++, prospero-lld, prospero-ar, llvm-objcopy
    target/              # include/ and lib/ from the compatible SDK
  payload-sdk/
    bin/                 # prospero-clang and prospero-clang++
    target/              # sysroot, including user/homebrew/include
  llvm21/
    bin/                 # clang, clang++, llvm-nm, llvm-readelf
  dxc/
    source/include/dxc/  # compatible DXC headers, not a private compiled editor
  native-tools/
    ps5-native-tool
  host-tools/
    rexglue
    glslangValidator
    spirv-val
    spirv-dis
    mesa_clc
    vtn_bindgen2
  python/
    bin/python3
```

These are **version-specific inputs**, not interchangeable latest releases. Source trees must include required submodules. `external-source-inputs.json` and `PROVENANCE.md` identify source revisions; `external-source-manifest.json` and `recipes/*/manifest.json` pin individual source files. `host-tool-inputs.json` records the tested Mesa helpers; `guest-contract.json` specifies ReXGlue 0.10.0. `mesa-producers.json`, `builtin-shader-contract.json`, `config-header-contract.json` and `pch-producer.json` define generated outputs. `link-plan.json` pins the SDK link inputs and `recipes/native-imports.json` defines the accepted import contract. Tool output and generated file hashes must match; the builder does not promise that arbitrary tools with the same version label are interchangeable.

Both the Python interpreter launching the builder and `dependencies/python/bin/python3` must have the modules they use. The Mesa generator interpreter needs Mako and PyYAML; the builder interpreter needs Pillow for caption and companion rendering. Companion rendering was validated with Pillow 10.2.0, FreeType 2.13.2, raqm 0.10.1 and zlib 1.3; generated hashes must match. The Windows launcher uses PowerShell/Windows Forms and does not need Python's Tk support. Host C++ headers, linker support and Clang AddressSanitizer/UndefinedBehaviorSanitizer runtimes are required for the focused behavior gate. Keep the SDK compiler wrappers' supporting executables and libraries intact. This is a local build front end for an already prepared toolchain, not a clean-PC installer.

## Checks

The delay and Sort/Filter source retains editor/runtime O2 and inherited dependency settings. The five LOAD limits remain **71 / 26 / 2 / 25 / 3 MiB**. Source hashes, imports, five hooks, ELF layout, SELF integrity and all six existing companion hashes plus both generated splash images are checked.

A focused host test executes the delay adapter with mocked delegates under sanitizers. Previously completed full Save/Discard/Cancel and replay validation is recorded as **reused validation of the exact delay source**, not a newly executed whole-application test. The builder runs no console test or performance benchmark.

Failures stop the pipeline and retain local error logs. Originals and dependencies are not rewritten. Start a fresh attempt after correcting an unsupported or missing input.

Each attempt creates `build-<timestamp>/inputs.json` and its separate `native-work/` workspace. Source preparation, generators, the focused gate and native compilation share that workspace. The focused gate uses the supplied ReXGlue SDK for host headers. Final receipts are under `native-work/workflow-receipts/`; the completed download is `native-work/package/Editor-125ms-Sort-Filter-Splash.zip`, with its eboot hash in `native-work/package/package-result.json`. A failed attempt retains its stage logs and cannot be reused as a fresh output folder.

## Sharing

Share only the builder source/download. Do not add originals, generated source containing original data, SDKs, build folders or editor binaries to a public repository. Check files before publishing; `.gitignore` alone cannot guarantee this.

See `BUILD-STATUS.md` and `validation.json` for the completed local build validation and its scope.
