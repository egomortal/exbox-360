"""Assemble the reproducible local audit from its measured artifacts."""
from pathlib import Path
from collections import Counter
import csv
import hashlib
import json

ROOT=Path(__file__).resolve().parent
OUT=ROOT/"build/audit-20261025"
pixel_rows=list(csv.DictReader((OUT/"final/source-pixels.tsv").open(),delimiter="\t"))
assert all(int(row["changed_pixels"])==0 and int(row["identity_resample_changes"])==0 for row in pixel_rows)
media=json.loads((OUT/"media-verification.json").read_text())
assert media["verified"]==79 and not media["errors"]
video=json.loads((OUT/"video-pixel-comparison.json").read_text())
source=json.loads((OUT/"source-video-quality.json").read_text())
fonts=json.loads((OUT/"fonts.json").read_text())
dimensions=Counter((row["width"],row["height"]) for row in media["results"])
table="\n".join(f"| {row['scene']} | {row['viewport'][2]}×{row['viewport'][3]} | {row['independent_lanczos_psnr_db']} | {row['mean_channel_delta']} | {row['processing_ms_p95']} |" for row in video)
report=f'''# Ex-Box 360 local navigation and rendering audit

2026-10-25. Native SDL renderer, 1920×1080. No PS5 connection, installation, launch,
or daemon change was made during this audit. The desktop build and PS5 ELF compile.
The existing avatar kit, geometry, animations and direct-memory allocator are retained.

## Corrected and verified

| Area | Changes and evidence |
|---|---|
| Metro navigation | Up reaches the top tabs; Left/Right selects tabs; Down/A enters content. LB/RB switches tabs. B returns to tabs, then Home. A directional reachability search covers every title, Pins, Recent and tabs for libraries of 1–19 titles, including partial pages. |
| Metro content | Seven content slots plus separate Pins and Recent, matching the reference arrangement. Native Pins/Recent lists, installed-library first-letter search, and functional Settings. Refresh clamps focus after a title disappears. |
| Blades navigation | Left/Right and bumpers switch the visible blades; Up/Down selects rows. System contains actual settings, including in-place toggles, era and style changes and PS5 Home exit. |
| NXE / Kinect | Vertical channels and horizontal cards remain separate. Kinect's original patterned card textures are restored and its icon reflection fits within the card. |
| Details / Library | Correct modal input priority, Y details, B return, launch-failure dialog return, page/first/last navigation and empty states. |
| Artwork | Source images are retained rather than pre-shrunk to 256px. Each era contains the whole cover in its own bounds. Category reflections are retained for NXE/Kinect and excluded from Metro's flat symbol area. Nine smaller native Blades glyphs are read from the owner's extracted library. |
| Icons | Pins is a pushpin; magnifier has its handle; Recent is a clock. These three are antialiased reconstructed symbols. Controller Map uses the chosen button style immediately. |
| Gloss / edges | Original button shading is retained; the extra white gloss layer is removed. Alpha-aware Lanczos-3 filtering replaces nearest/point scaling for relevant art, icons, pane slices and video. The notification's segmented ring is antialiased. |
| Typography | All 13,588 recovered XTT glyphs independently parse. The compressed source outlines are byte-identical to recovered glyf data. Classic font family is Xbox TC; newer recovered family is Xbox UI SC. Windows-owned Segoe UI and Segoe UI Light supply the later-layout faces. Metro headings are larger, lighter and repositioned against the reference. |
| Color | Per-blade red/orange/green/blue/purple palettes; native Kinect texture, gray Metro background, original NXE patterns and deliberate theme tints. Theme tint is an intentional color change; default source pixels are not recolored. |
| Playback | Source resolution up to 1080p, source-near 24/25/30 fps, JPEG q2, manifest display aspect, no forced stretch/crop or overlaid TRAILER badge. Full-screen video fits above captions and prompts. Audio timestamps and durations match the video. |
| Render cost | Static scaled-art cache capped at 48 MiB; decoded title-art cache capped at 64 MiB. One current scaled video frame is reused until the decoder frame or viewport changes. Modal views reuse the exact hub backdrop instead of redrawing hidden content. |

Navigation tests: **504 checks, zero failures**. Library tests: **43 checks, zero failures**.
Both native targets compile; an existing unused-variable warning in avatar_kit.hpp is retained.

## Pixel and video evidence

**{len(pixel_rows)} source image checks**: zero changed pixels on load and zero changes in
identity-size resampling. This covers pane textures, category art, native Blades icons,
glossy buttons, Guide assets, toast glyphs and the 600×600 Fallout 4 cover.
See `source-pixels.tsv`. This is a source-preservation check, not a claim that every
retail screen is reproduced pixel for pixel.

All **79 packs and 108 manifest rows** pass boundary, first/middle/last frame, profile,
resolution, aspect and audio-duration validation. Resolutions:
{', '.join(f'{count} at {w}×{h}' for (w,h),count in sorted(dimensions.items()))}.
Frame packs occupy {sum(row['bytes'] for row in media['results'])/1e9:.2f} GB, plus PCM audio.
The official Fallout 4 1080p stream replaces the previous 360p intermediate locally.

Final viewport crops are compared with Pillow's independent Lanczos renderer using
the exact simultaneously exported decoded frame. Flat/fade frames are rejected.
This measures picture placement and resizing separately from video encoding.

| View | Picture viewport | PSNR vs reference (dB) | Mean channel delta (0–255) | Local p95 processing (ms) |
|---|---:|---:|---:|---:|
{table}

The q2 JPEG pack itself is lossy: a detailed 1080p source-frame comparison measures
**{source['encoding_psnr_db']} dB**, mean channel delta **{source['mean_channel_delta']}**.
Resizing cannot retain information finer than the destination pixel grid; lower-resolution
original videos and 32–96px retail icons cannot become native high-resolution assets.
The application retains the best original locally and resamples directly for the destination.

Timing is headless desktop processing, excludes the deliberate frame delay, and is not a PS5
frame-rate measurement. See CSV files for distributions and first-frame costs. Any local
sample exceeding the 33.3ms budget remains a performance finding; console playback speed
and TV output require a later authorized device check.

## Visual review and exactness limits

The `final/` folder contains full-resolution native renders for all four styles,
top tabs, last pages, focus states, empty Pins, search, settings, Details, Guide,
three button sets, notifications and tints. Original frames remain available alongside
the contact sheet; the sheet is only an overview, not the pixel evidence.

The native layouts remain recreations. Blades' full curved panel geometry and its
era-specific left-side Guide are not exact retail scene reconstructions. The shared
NXE Guide remains in all four styles. Metro contains local titles and native functions,
not the retired online promo/social feeds. The three large social glyphs are generated
fallbacks, while native small equivalents are used in Blades. The separate existing
`SIDE_FACE_ARTIFACT_AUDIT.md` still records unresolved avatar face/material findings.
These are open fidelity items, not passed pixel-exact checks.

## Primary visual references

The original dashboard designer's [Xbox 360 gallery](https://www.i4design.com/xbox-360)
provides Blades Games/Media/System/Xbox Live, early Guide, NXE and Metro pages. Reference
images and exact image URLs are saved in `references/sources.json`. Existing era captures
include `blades-1888.jpg`, `kinect-home.jpg`, `metro-home.jpg`, and the separate purple
`kinect-hub.jpg`; the Kinect Hub is not substituted for Kinect Home.
[DashX360](https://github.com/ZivvoZ/dashx360) was also reviewed for era/controller context.
No code or proprietary asset bundle was imported from it.

## Reproduce locally

Build with `make test host all` in WSL. Run `run_visual_audit.py` using the bundled
Windows Python; `--video` adds exact decoded-frame captures and timing CSVs.
`compare_native_video.py` compares final crops with Pillow/numpy. `verify_media.py .`
checks packs and audio. `validate_owner_fonts.py` additionally needs FontTools.
The fixture is isolated by NXE_FS_ROOT and never installed or read as real console data.

The standard `prepare_trailers.py` entry point now calls the native-quality preparer.
It supports --download, --title, --upgrade and --rebuild. The prior local app, scripts,
360p packs, and Fallout source are backed up before promotion. Future deployment needs
the updated ELF, fonts and media together. No automatic console action is part of this audit.
'''
docs=ROOT/"docs";docs.mkdir(exist_ok=True)
(docs/"LOCAL_AUDIT_20261025.md").write_text(report,encoding="utf-8")
(OUT/"AUDIT.md").write_text(report,encoding="utf-8")
files=["main.cpp","dash_model.cpp","dash_model.hpp","dashboard_layout.hpp","image_resample.hpp","Makefile","build/eboot.elf"]
(OUT/"build-hashes.json").write_text(json.dumps({name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files},indent=2)+"\n")
print("Audit report assembled")
