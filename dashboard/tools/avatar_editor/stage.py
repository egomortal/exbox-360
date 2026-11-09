"""Copy the owner's prepared assets byte-for-byte; never run or copy host binaries/saves."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import os
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from input_paths import prepared_editor
from verified_files import copy_verified, sha256  # compatibility exports for existing callers

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = prepared_editor(ROOT)
DEST = ROOT / "private/avatar-editor"
CONSOLE = "/data/homebrew/ExBox360/avatar-editor"
PROVENANCE = (
    "asset-provenance.json", "active-font-provenance.json", "font-provenance.json",
    "hair-integration-report.json", "closet-import-report.json",
    "TEST-REPORT-20261028.md", "release-provenance.json",
)
FLAGGED = (
    "00800018-55bc-0002-930f-f6df1ed8bf7f",
    "00800018-817f-0002-1d58-022f6c85ff8b",
    "00800018-8d36-0002-cadc-c209f2c41093",
    "00800018-9ad5-0002-f6a4-adf0fd118784",
)


def checked_destination(dest, relative):
    path = dest / relative
    if not path.resolve().is_relative_to(dest.resolve()):
        raise ValueError(f"destination escapes staging root: {relative}")
    if path.is_symlink() or any(p.is_symlink() for p in path.parents):
        raise ValueError(f"symlink in destination: {relative}")
    return path


def put_generated(dest, relative, content):
    path = checked_destination(dest, relative)
    payload = content.encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != payload:
            raise ValueError(f"existing generated file differs (preserved): {path}")
    else:
        with path.open("xb") as stream:
            stream.write(payload)
    return {"path": relative, "source": "AV2 staging tool (generated)",
            "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def prepared_asset_files(source,directories=None):
    """Validate each directory once; retain the old sorted file enumeration."""
    assets = source.resolve() / "assets"
    if assets.is_symlink() or not (assets / "closet/closet_index.tsv").is_file():
        raise ValueError("prepared editor/assets/closet/closet_index.tsv is required")
    files=[];pending=[assets]
    while pending:
        directory=pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                path=Path(entry.path)
                if entry.is_symlink():raise ValueError(f"symlink or escaped source: {path}")
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                    if directories is not None:directories.append(path)
                elif entry.is_file(follow_symlinks=False):
                    if path.suffix.lower() in {".exe", ".dll", ".cmd", ".bat", ".ps1"}:
                        raise ValueError(f"unexpected host executable in assets: {path}")
                    files.append(path)
    yield from sorted(files)


def stage(source, dest):
    source = source.resolve()
    dest = dest.resolve()
    if dest.is_relative_to(source) or source.is_relative_to(dest):
        raise ValueError("source and destination must be separate trees")
    assets = source / "assets"
    if not (assets / "closet/closet_index.tsv").is_file():
        raise ValueError("prepared editor/assets/closet/closet_index.tsv is required")
    dest.mkdir(parents=True, exist_ok=True)
    manifest = []
    started = time.monotonic()
    for path in prepared_asset_files(source):
        relative = path.relative_to(source).as_posix()
        record = copy_verified(path, checked_destination(dest, relative))
        record["path"] = relative
        manifest.append(record)
        if len(manifest) % 2000 == 0:
            print(f"verified {len(manifest)} assets ({time.monotonic()-started:.0f}s)", flush=True)
    for name in PROVENANCE:
        path = source / name
        if path.is_file():
            relative = "provenance/" + name
            record = copy_verified(path, checked_destination(dest, relative))
            record["path"] = relative
            manifest.append(record)
    config = ("# AV2 console paths; backend is not implemented in run 1.\n"
              f'game_data_root = "{CONSOLE}/assets"\n'
              f'avatar_asset_pack_dir = "{CONSOLE}/assets"\n'
              f'avatar_closet_dir = "{CONSOLE}/assets/closet"\n'
              f'user_data_root = "{CONSOLE}/userdata"\n'
              f'avatar_data_dir = "{CONSOLE}/userdata/avatar"\n'
              'content_root = "outfits"\nonline_mode = false\navatar_marketplace_server = ""\n')
    manifest.append(put_generated(dest, "avatareditor.toml", config))
    policy = {"excluded_from_selection": list(FLAGGED),
              "reason": "Confirmed neckline defects; original payloads/index retained unchanged.",
              "enforced_by_runtime": False,
              "required_before_editor_enable": "Apply these exclusions in the paged catalog adapter."}
    manifest.append(put_generated(dest, "selection-policy.json", json.dumps(policy, indent=2) + "\n"))
    # Generated index is local provenance, not an on-console resident catalog.
    with (dest / "staging-manifest.jsonl").open("w", encoding="utf-8", newline="\n") as stream:
        for record in manifest:
            stream.write(json.dumps(record, ensure_ascii=True) + "\n")
    asset_records = [r for r in manifest if r["path"].startswith("assets/")]
    with (assets / "closet/closet_index.tsv").open(encoding="utf-8") as stream:
        rows = sum(bool(line.strip()) for line in stream)
    summary = {"complete": True, "source": str(source), "asset_files": len(asset_records),
               "asset_bytes": sum(r["bytes"] for r in asset_records), "catalog_rows": rows,
               "manifest_sha256": sha256(dest / "staging-manifest.jsonl"),
               "host_binaries_copied": False, "userdata_copied": False,
               "core_initialized": False}
    with (dest / "staging-summary.json").open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(summary, stream, indent=2)
        stream.write("\n")
    print(json.dumps(summary), flush=True)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    stage(args.source, DEST)


if __name__ == "__main__":
    main()
