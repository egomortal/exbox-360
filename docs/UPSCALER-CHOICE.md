# Local image enlargement choice

Faithful (premultiplied Lanczos with local color bounds) is the default for icons, text, gradients and photos. AI preview is an explicit option for opaque images only. Neither method recovers original missing detail.

On four original synthetic 48-to-192 pixel fixtures, classic versus AI mean absolute RGB error was: icon 8.19/7.47; text 8.38/9.05; gradient 0.37/1.70; photo-like 2.57/3.71. AI sharpened the icon but invented a glow/bevel, changed small lettering and removed texture. These limited fixtures support the faithful default; they do not establish quality for every image or a TV. Owner TV evaluation remains pending.

AI uses the unchanged generic Real-ESRGAN realesr-general-x4v3 release v0.2.5.0. No training, tuning, console-art examples or substitution tables. Source-built ncnn CPU FP32 single-thread inference uses 128-pixel tiles with a 34-pixel halo. The seam fixture matches untiled output exactly. AI rejects transparency; faithful interpolation preserves alpha without using hidden RGB colors.

Outputs go only to a user-selected data folder/upscaled. Originals are retained. Source checkout destinations, path escapes, symlinks and conflicting outputs are rejected. Fixed PNG encoding omits metadata. Byte repeatability was tested in fresh processes under the pinned x86_64/Python 3.12.3/Pillow 10.2.0/NumPy 1.26.4 runtime, not across arbitrary CPUs or library versions.

## Local setup

Use WSL Python 3.12 with Pillow 10.2.0 and NumPy 1.26.4, CMake and a C++17 compiler. From this source folder run:

    python3 tools/prepare_upscale_runtime.py --data-root /absolute/user-data --build

This downloads pinned official source and generic weights, builds the CPU adapter locally, validates inference and writes a runtime receipt. It downloads no executable. Without --build it only describes the work.

Then run:

    python3 tools/upscale.py /absolute/input.png --data-root /absolute/user-data --runtime-dir /absolute/user-data/upscale-runtime --kind ui --scale 2

Add --method ai only for an opaque preview. The batch CLI accepts multiple files or a folder. In the setup window, enable Create larger image copies, select type and scale, then drop files or choose a folder. Choose the same data folder. EXBOX_UPSCALE_RUNTIME can override the runtime location. Windows uses WSL; source, input and runtime paths must be drive-qualified. No console connection occurs.

The runtime/model and private comparison sheet are excluded from this code-only export. tools/gh1_export.py verify_tree checks exact staged manifest membership and rejects unlisted data, image signatures, embedded image data and known input hashes. This is a bounded guard, not a proof that arbitrary text cannot encode art. X18 owns the final combined export and installer.
