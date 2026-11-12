#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the native Ex-Box 360 dashboard eboot (PPSA99735) from this repository.

    python3 dashboard/native/build_native.py --deps <dependencies folder> --out <fresh output folder>

Runs on Linux (WSL Ubuntu-24.04 on Windows). It compiles every unit from source, links, checks the
import list and the segment layout, converts and signs. It never downloads anything, never contacts
a console and never installs. The dependencies folder is described in README.md beside this file.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, subprocess, sys

HERE = Path(__file__).resolve().parent
DASH = HERE.parent
SUPPORT = HERE / "support"
sys.path.insert(0, str(DASH / "tools"))

# Imports that link but give a title nothing at run time. A build that pulls one in is refused.
REFUSED = {"isatty", "mkstemp", "fork", "link", "symlink", "readlink", "pathconf", "sceLncUtilLaunchApp",
           "strcasestr", "sceRandomGetRandomNumber"}
REFUSED_PREFIXES = ("sceKeyboard", "sceImeDialog")
WRAPS = ["strcasestr", "fork", "link", "symlink", "pathconf", "readlink", "mkstemp", "sceKeyboardInit",
         "sceKeyboardOpen", "sceKeyboardReadState", "sceKeyboardClose", "sceImeDialogInit",
         "sceImeDialogGetResult", "sceImeDialogTerm", "sceImeDialogGetStatus"]
HOMEBREW_LIBS = ["libSDL2_ttf.a", "libSDL2_image.a", "libSDL2.a", "libfreetype.a", "libharfbuzz.a", "libpng.a",
                 "libjpeg.a", "libwebpdemux.a", "libwebp.a", "libbz2.a", "libsamplerate.a", "libiconv.a"]
LINK_ORDER = ["dashboard", "model", "library", "bridge", "editor_bridge", "return_ticket", "ticket_file",
              "interface_index", "optional_modules", "launcher", "crt", "unavailable_posix", "string",
              "import_check", "native_session", "native_2d_client", "gpu2d", "avatar_gpu", "resize_gpu",
              "filtered_c"]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def undefined_symbols(path):
    """Names the linked ELF imports (symbol table entries with section index 0)."""
    d = Path(path).read_bytes()
    if d[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"not a little-endian ELF64: {path}")
    off = struct.unpack_from("<Q", d, 40)[0]
    size, count = struct.unpack_from("<HH", d, 58)
    sections = [struct.unpack_from("<II4QII2Q", d, off + i * size) for i in range(count)]
    result = {}
    for sec in sections:
        if sec[1] != 11:
            continue
        strings = sections[sec[6]]
        table = d[strings[4]:strings[4] + strings[5]]
        for pos in range(sec[4], sec[4] + sec[5], sec[9]):
            name, info, _other, index, _value, _length = struct.unpack_from("<IBBHQQ", d, pos)
            text = table[name:table.find(b"\0", name)].decode()
            if text and index == 0:
                result[text] = info & 15
    return result


def import_check_source(imports):
    """Relocated-pointer checks: reads each import's address at start-up, never calls it."""
    names = sorted(imports)
    code = ["// Generated relocated-pointer checks; never calls the imports.",
            'extern "C" void exbox_native_note(const char*,...);']
    for i, name in enumerate(names):
        decl = f"void exbox_import_{i}()" if imports[name] == 2 else f"unsigned char exbox_import_{i}"
        code.append(f'extern "C" {decl} asm("{name}") __attribute__((weak));')
    code.append('extern "C" bool exbox_check_native_imports(){unsigned missing=0;')
    for i, name in enumerate(names):
        code.append(f'{{const void* volatile address=reinterpret_cast<const void*>(&exbox_import_{i});'
                    f'if(!address){{exbox_native_note("missing native import {name}\\n");++missing;}}}}')
    code += [f'exbox_native_note("native imports checked={len(names)} missing=%u\\n",missing);return missing==0;', "}"]
    return "\n".join(code) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--deps", required=True, type=Path, help="dependencies folder (see README.md)")
    ap.add_argument("--out", required=True, type=Path, help="output folder; must not exist yet")
    ap.add_argument("--expect-eboot-sha256", help="fail unless the signed eboot has this SHA-256")
    a = ap.parse_args()
    deps = a.deps.resolve()
    out = a.out.resolve()
    if out.exists():
        sys.exit(f"{out} already exists; choose a fresh output folder")

    need = {
        "native SDK compiler": deps / "native-sdk/bin/prospero-clang++",
        "native SDK C compiler": deps / "native-sdk/bin/prospero-clang",
        "native SDK linker": deps / "native-sdk/bin/prospero-lld",
        "native SDK symbol tool": deps / "native-sdk/bin/llvm-nm",
        "native SDK platform library": deps / "native-sdk/target/lib/libps5platform.a",
        "payload SDK homebrew headers": deps / "payload-sdk/target/user/homebrew/include/SDL2/SDL.h",
        "LLVM 21 clang": deps / "llvm21/bin/clang",
        "title tool": deps / "native-tools/ps5-native-tool",
        "RADV archive": deps / "dashboard/libvulkan_radeon.a",
        "locale accessor archive": deps / "dashboard/libav2_locale_accessor.a",
        "tempfile compat archive": deps / "dashboard/libav2_tempfile_compat.a",
        "Agc stub": deps / "dashboard/stubs/libSceAgc.so",
        "AgcDriver stub": deps / "dashboard/stubs/libSceAgcDriver.so",
    }
    homebrew = deps / "payload-sdk/target/user/homebrew"
    for name in HOMEBREW_LIBS:
        need["payload SDK " + name] = homebrew / "lib" / name
    missing = [f"  {label}: {path}" for label, path in need.items() if not path.exists()]
    if missing:
        sys.exit("The dependencies folder is incomplete. Missing:\n" + "\n".join(missing) + "\nSee dashboard/native/README.md.")

    obj = out / "obj"
    obj.mkdir(parents=True)
    support = out / "support"   # the link recipe writes a generated map here, so work on a copy
    shutil.copytree(SUPPORT, support)
    steps = []

    def run(label, cmd):
        r = subprocess.run([str(x) for x in cmd], capture_output=True, text=True)
        (out / (label + ".log")).write_text(r.stdout + r.stderr)
        steps.append({"label": label, "exit": r.returncode})
        if r.returncode:
            sys.exit(f"{label} failed:\n{(r.stdout + r.stderr)[-4000:]}")
        print(label, "ok", flush=True)

    sdk = deps / "native-sdk"
    # No folder of the PC that builds this ends up inside the eboot: every path a compiler could
    # embed (assert file names, debug info) is mapped to a fixed name. This also makes the eboot
    # the same wherever the repository and the dependencies folder live.
    path_maps = [f"-ffile-prefix-map={out}=/exbox/build", f"-ffile-prefix-map={deps}=/exbox/deps",
                 f"-ffile-prefix-map={a.deps.absolute()}=/exbox/deps", f"-ffile-prefix-map={DASH}=/exbox/dashboard"]
    cxx_flags = [*path_maps, "-std=c++17", "-O2", "-ffp-contract=on", "-Wall", "-Wextra", "-Werror", "-D__PROSPERO__",
                 "-DEXBOX_NATIVE_TITLE", "-DNXE_NATIVE_PRESENTER", "-I" + str(support / "presenter"),
                 "-I" + str(homebrew / "include/SDL2"), "-ffunction-sections", "-fdata-sections", "-I" + str(DASH),
                 "-I" + str(support), "-I" + str(homebrew / "include"), "-I" + str(DASH / "modules/host_bridge")]
    entry = ["-Dmain=exbox_dashboard_main"]
    cxx_units = [
        ("dashboard", DASH / "main.cpp", entry), ("model", DASH / "dash_model.cpp", []),
        ("library", DASH / "ps5_library.cpp", []), ("bridge", DASH / "modules/host_bridge/nxe_bridge.cpp", []),
        ("editor_bridge", DASH / "modules/editor_dashboard/bridge.cpp", []),
        ("return_ticket", DASH / "modules/avatar_editor/native/return_ticket.cpp", []),
        ("ticket_file", DASH / "modules/avatar_editor/native/ticket_file.cpp", []),
        ("interface_index", DASH / "modules/native_dashboard/interface_index.cpp", []),
        ("optional_modules", DASH / "modules/native_dashboard/optional_modules.cpp", []),
        ("launcher", support / "launcher.cpp", []), ("crt", support / "app_crt.cpp", []),
        ("unavailable_posix", support / "unavailable_posix.cpp", []),
        ("filtered_c", DASH / "x21/filtered_c.cpp", entry),
    ]
    for name, src, extra in cxx_units:
        run("compile-" + name, [sdk / "bin/prospero-clang++", *cxx_flags, *extra, "-c", src, "-o", obj / (name + ".o")])
    for name in ("native_session", "native_2d_client", "gpu2d", "avatar_gpu", "resize_gpu"):
        run("compile-" + name, [sdk / "bin/prospero-clang", *path_maps, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                                "-ffunction-sections", "-fdata-sections", "-I" + str(DASH / "x21"), "-c",
                                DASH / "x21" / (name + ".c"), "-o", obj / (name + ".o")])
    run("compile-string", [sdk / "bin/prospero-clang", *path_maps, "-O2", "-Wall", "-Wextra", "-Werror", "-c",
                           support / "string_compat.c", "-o", obj / "string.o"])

    wraps = " ".join("--wrap=" + w for w in WRAPS)
    libs = " ".join(f'"$lib/{n}"' for n in HOMEBREW_LIBS)
    units = " ".join(LINK_ORDER)
    (out / "link.sh").write_text(f'''#!/usr/bin/env bash
set -euo pipefail
state="{out}"
deps="{deps}"
support="$state/support"
sdk="$deps/native-sdk"
lib="$deps/payload-sdk/target/user/homebrew/lib"
source "$support/native-platform-link.sh"
export PS5_CLANG="$deps/llvm21/bin/clang"
native_link_recipe "$support" "$sdk"
objects=()
for unit in {units}; do objects+=("$state/obj/$unit.o"); done
"$sdk/bin/prospero-lld" -T "$support/ps5-pie-unwind.ld" -L "$support" --eh-frame-hdr --error-limit=0 {wraps} "${{radv_link_flags[@]}}" --version-script "$support/app-local.map" --exclude-libs=ALL -e _start -o "$state/llvm-pie.elf" "${{objects[@]}}" --start-group {libs} --end-group "$deps/dashboard/libav2_locale_accessor.a" "$deps/dashboard/libav2_tempfile_compat.a" --whole-archive "$deps/dashboard/libvulkan_radeon.a" --no-whole-archive "$deps/dashboard/stubs/libSceAgc.so" "$deps/dashboard/stubs/libSceAgcDriver.so" "${{radv_link_inputs[@]}}" --as-needed "$sdk"/target/lib/*.so > "$state/link.log" 2>&1
''')

    def link(label):
        r = subprocess.run(["bash", str(out / "link.sh")], capture_output=True, text=True)
        steps.append({"label": label, "exit": r.returncode})
        if r.returncode:
            log = (out / "link.log").read_text() if (out / "link.log").exists() else ""
            sys.exit(f"{label} failed:\n{(r.stdout + r.stderr + log)[-4000:]}")
        print(label, "ok", flush=True)

    # Pass 1 links with an empty checker to learn the import list; pass 2 links the real checker.
    generated = out / "import_check.cpp"
    generated.write_text('extern "C" bool exbox_check_native_imports(){return true;}\n')
    run("compile-import-bootstrap", [sdk / "bin/prospero-clang++", *cxx_flags, "-c", generated, "-o", obj / "import_check.o"])
    link("link-bootstrap")
    imports = undefined_symbols(out / "llvm-pie.elf")
    bad = sorted(n for n in imports if n in REFUSED or n.startswith(REFUSED_PREFIXES))
    if bad:
        sys.exit("The build imports functions that do not work in a title: " + ", ".join(bad))
    generated.write_text(import_check_source(imports))
    run("compile-import-final", [sdk / "bin/prospero-clang++", *cxx_flags, "-c", generated, "-o", obj / "import_check.o"])
    link("link-final")
    final = undefined_symbols(out / "llvm-pie.elf")
    extra = sorted(set(final) - set(imports))
    if extra:
        sys.exit("The final link imports names the checker does not cover: " + ", ".join(extra))

    tool = deps / "native-tools/ps5-native-tool"
    run("convert", [tool, "link", "--in", out / "llvm-pie.elf", "--out", out / "eboot.elf", "--stub-dir", sdk / "target/lib",
                    "--stub", deps / "dashboard/stubs/libSceAgc.so", "--stub", deps / "dashboard/stubs/libSceAgcDriver.so",
                    "--module-sdk", "0x02000009", "--companion-sdk", "0x08050001", "--file-name", "eboot.elf"])
    from check_native_layout import inspect
    layout = inspect(out / "eboot.elf", "dashboard")
    (out / "layout.json").write_text(json.dumps(layout, indent=2) + "\n")
    if not layout["passed"]:
        sys.exit("Segment layout is over the dashboard limits; see layout.json")
    title = out / "PPSA99735"
    title.mkdir()
    run("sign", [tool, "self", "--sign", "--in", out / "eboot.elf", "--out", title / "eboot.bin", "--magic", "0x1D3D154F"])
    r = subprocess.run([str(tool), "self", "--inspect", "--file", str(title / "eboot.bin")], capture_output=True, text=True)
    (out / "self-inspect.log").write_text(r.stdout + r.stderr)
    if r.returncode or "integrity: valid" not in r.stdout:
        sys.exit("The signed eboot failed inspection; see self-inspect.log")

    blob = (title / "eboot.bin").read_bytes()
    leaked = sorted({str(x) for x in (out, deps, a.deps.absolute(), DASH, Path.home()) if str(x).encode() in blob})
    if leaked:
        sys.exit("The eboot contains a folder path from this PC (" + ", ".join(leaked) + "); refusing to finish")
    pinned = json.loads((HERE / "dependencies.json").read_text())["reference_hashes"]
    used = {rel: sha(deps / rel) for rel in pinned if (deps / rel).is_file()}
    result = {
        "eboot_sha256": sha(title / "eboot.bin"), "eboot_bytes": (title / "eboot.bin").stat().st_size,
        "elf_sha256": sha(out / "llvm-pie.elf"), "imports": len(final),
        "layout_loads": [[l["filesz"], l["memsz"]] for l in layout["loads"]], "layout_limits": layout["limits"],
        "dependencies_differing_from_reference": sorted(rel for rel, h in used.items() if h != pinned[rel]),
        "dependencies_not_hashed": sorted(rel for rel in pinned if rel not in used),
        "steps": steps, "console_accessed": False, "installed": False,
        "still_needed_for_a_title_folder": ["sce_sys/param.json", "sce_sys/icon0.png", "sce_module/libc.prx (built locally, see README.md)"],
    }
    (out / "build-manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "steps"}, indent=2))
    if a.expect_eboot_sha256 and result["eboot_sha256"] != a.expect_eboot_sha256:
        sys.exit("eboot hash differs from --expect-eboot-sha256")


if __name__ == "__main__":
    main()
