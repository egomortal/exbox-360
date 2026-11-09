"""Copy only used, differing dashboard art from the owner's XUIZ packages.

Run from any directory; --resources selects a read-only decrypted resource root.
No full unpack is written: package members are decoded in memory using xuiz.py's
end-of-name-table offsets. All extracted content stays in private/era-library.
"""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import sys
from input_paths import dashboard_resources

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/xur5"))
from xuiz_archive import members as archive_members
RESOURCES = dashboard_resources(ROOT)
BUILDS = {
    6770: "Blades/Retail/6770",
    13604: "Kinect/Retail/13604",
    15574: "Metro/V1/Retail/15574",
    17559: "Metro/V2/Retail/17559",
}


def unpack(raw):
    """Keep this public lookup API; use the shared byte-preserving parser.

    Runtime aliases normalize separators and retain the last duplicate, as the
    previous extractor did. Unknown ../ scene names stay in memory only.
    """
    return dict(archive_members(raw, normalize_slashes=True))


def used_paths(source):
    # load_library_art keeps literal module/file names, including its arrays.
    body = source.split("void load_library_art() {", 1)[1].split('\n#include "assets/update_art.inc.hpp"', 1)[0]
    paths = sorted(set(re.findall(r'"([A-Za-z0-9_]+/[^"\n]+\.(?:png|jpg|jpeg))"', body)))
    if not paths:
        raise ValueError("No library art names found in load_library_art")
    return paths


def library_source(packages, module, name):
    """NetworkMain moves ConnectBar icons to common and renames the console."""
    candidates = [(module, name)]
    if module == "dvd":
        # Metro XmfPadPlayback uses common:// for these five transport icons.
        # The package is dashcomm; keep the runtime dvd/ alias for library_art.
        common = {"ico_32x_last.png": "ico_32x_last.png",
                  "ico_32x_next.png": "ico_32x_next.png",
                  "ico_32x_stop.png": "ico_32x_stop.png",
                  "ico_32x_RR.png": "ico_32x_RR.png",
                  "ico_32x_FF.png": "ico_32x_FF.png"}
        if name in common:
            candidates += [("dashcomm", common[name])]
    if module == "pictures":
        # slideshowControls: 6770 names RotateLeft/Right; Metro moves transport
        # images to common:// (dashcomm). Missing Play/Pause aliases use dvd's
        # matching black transport images; missing Shuffle retains 9199 art.
        aliases = {"ico_32x_Play_black.png": [("dashcomm", "btn_32x_Play.png"), ("dvd", "ico_32x_play_black.png")],
                   "ico_32x_pause_black.png": [("dashcomm", "btn_32x_Pause.png"), ("dvd", "ico_32x_pause_black.png")],
                   "ico_32x_last.png": [("dashcomm", "ico_32x_last.png"), ("dashcomm", "btn_32x_Last.png")],
                   "ico_32x_stop.png": [("dashcomm", "ico_32x_stop.png"), ("dashcomm", "btn_32x_Stop.png")],
                   "ico_32x_next.png": [("dashcomm", "ico_32x_next.png"), ("dashcomm", "btn_32x_Next.png")],
                   "ico_32x_Rotate.png": [("pictures", "ico_32x_RotateRight.png"), ("dashcomm", "ico_32x_Rotate.png")],
                   "ico_32x_RotateCC.png": [("pictures", "ico_32x_RotateLeft.png"), ("dashcomm", "ico_32x_RotateCC.png")],
                   "ico_32x_shuffle_Off.png": [("pictures", "ico_32x_Shuffle_Off.png")]}
        # The black dvd images match Metro's common://ico_32x_Play alias,
        # whereas 6770 uses common://btn_32x_Play explicitly.
        options = aliases.get(name, [])
        if name in {"ico_32x_Play_black.png", "ico_32x_pause_black.png"} and "ico_32x_Rotate.png" in packages.get("dashcomm", {}):
            options = list(reversed(options))
        candidates += options
    if module == "network":
        if name == "console_64x.png":
            candidates += [("network", "ico_64x_console.png"), ("dashcomm", "console_64x.png"),
                           ("dashcomm", "ico_64x_console.png")]
        elif name == "livelogo_hor.png":
            candidates += [("dashcomm", "LiveLogo.png")]
        else:
            candidates += [("dashcomm", name)]
    for package, member in candidates:
        data = packages.get(package, {}).get(member)
        if data is not None:
            return data, f"{package}/{member}"
    return None, ""


def collect(resources, baseline, paths, archives=None, baseline_bytes=None):
    result, summaries = {}, []
    for build, directory in BUILDS.items():
        folder = resources / directory / "dash.xex"
        if not folder.is_dir():
            raise FileNotFoundError(folder)
        packages = {}
        for package in sorted(folder.iterdir()):
            if package.is_file():
                raw = archives.read(package) if archives else package.read_bytes()
                if raw[:4] == b"XUIZ":
                    packages[package.name] = archives.lookup(package, normalize_slashes=True) if archives else unpack(raw)
        copied = identical = missing = 0
        for relative in paths:
            module, name = relative.split("/", 1)
            data, _ = library_source(packages, module, name)
            if data is None:
                missing += 1
                continue
            base = baseline / relative
            base_data = baseline_bytes.get(relative) if baseline_bytes is not None else (base.read_bytes() if base.is_file() else None)
            if module != "network" and base_data is not None and data == base_data:
                identical += 1
                continue
            result[f"{build}/{relative}"] = data
            copied += 1
        summaries.append((build, copied, identical, missing))
    return result, summaries


def write_output(output, files):
    for relative in files:
        if any(p in {"", ".", ".."} or ":" in p or "\\" in p or "\0" in p for p in relative.split("/")):
            raise ValueError(f"Unsafe output path: {relative!r}")
    # Never delete stale files silently: the guide requires reporting removals.
    stale = sorted(p.relative_to(output).as_posix() for p in output.rglob("*")
                   if p.is_file() and p.name != "manifest.tsv" and p.relative_to(output).as_posix() not in files)
    if stale:
        raise ValueError("Stale output files (not deleted): " + ", ".join(stale))
    rows = ["build\tpath\tsha256"]
    for relative, data in files.items():
        destination = output / PurePosixPath(relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_bytes() != data:
            destination.write_bytes(data)
        build, path = relative.split("/", 1)
        rows.append(f"{build}\t{path}\t{hashlib.sha256(data).hexdigest()}")
    output.mkdir(parents=True, exist_ok=True)
    table = "\n".join(rows) + "\n"
    (output / "manifest.tsv").write_text(table, encoding="utf-8", newline="\n")
    return table


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path, default=RESOURCES)
    args = parser.parse_args()
    paths = used_paths((ROOT / "main.cpp").read_text(encoding="utf-8"))
    files, summaries = collect(args.resources, ROOT / "private/xbox/unpacked", paths)
    print(write_output(ROOT / "private/era-library", files), end="")
    print(f"Compared {len(paths)} runtime paths per build.", file=sys.stderr)
    for build, copied, identical, missing in summaries:
        print(f"{build}: {copied} copied, {identical} identical, {missing} absent (base fallback)", file=sys.stderr)


if __name__ == "__main__":
    main()
