"""Stage T07 ConnectBar art from read-only retail packages; never delete files."""
import hashlib
from build_era_library import ROOT, RESOURCES, BUILDS, unpack, used_paths, library_source


def main():
    paths = [p for p in used_paths((ROOT / "main.cpp").read_text(encoding="utf-8"))
             if p.startswith("network/")]
    rows = ["destination\tsource\tsha256\tstatus"]
    for build, folder in BUILDS.items():
        packages = {name: unpack((RESOURCES / folder / "dash.xex" / name).read_bytes())
                    for name in ("network", "dashcomm")}
        for relative in paths:
            module, name = relative.split("/", 1)
            data, member = library_source(packages, module, name)
            dest = ROOT / "private/era-library" / str(build) / relative
            if data is None:
                base = ROOT / "private/xbox/unpacked" / relative
                rows.append(f"{dest.relative_to(ROOT).as_posix()}\t{base.relative_to(ROOT).as_posix()}\t"
                            f"{hashlib.sha256(base.read_bytes()).hexdigest()}\t9199 fallback (not copied)")
                continue
            status = "unchanged" if dest.is_file() and dest.read_bytes() == data else "updated" if dest.exists() else "created"
            dest.parent.mkdir(parents=True, exist_ok=True)
            if status != "unchanged":
                dest.write_bytes(data)
            rows.append(f"{dest.relative_to(ROOT).as_posix()}\t{folder}/dash.xex/{member}\t"
                        f"{hashlib.sha256(data).hexdigest()}\t{status}")
    manifest = ROOT / "private/network-art-sources.tsv"
    manifest.write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
    print(f"Verified {len(rows)-1} art paths; provenance: {manifest}")


if __name__ == "__main__":
    main()
