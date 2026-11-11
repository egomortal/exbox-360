#!/usr/bin/env python3
"""Set up the Avatar Editor build tools, inside WSL (Ubuntu 24.04), with no admin rights.

    python3 provision.py --deps ~/.local/share/exbox-toolchain/dependencies \
        [--rexglue-source DIR] [--xex-tool-source DIR] [--jobs N] [--check]

Every item comes from its official upstream at the exact commit or SHA-256 in toolchain.json and
is checked against the editor builder's own per-file pins. Nothing is rehosted by Ex-Box. The
result is the `dependencies` folder that avatar-editor/local-builder/builder.py expects.

What it never does: use sudo, or download games, BIOS, Microsoft game files or console firmware.
ReXGlue and XexTool-RE are fetched from their official projects like everything else (owner
decision 2026-11-11; their public source includes the Xbox 360 executable key). A folder of your
own can be given with --rexglue-source / --xex-tool-source instead.

Safe to stop and run again: finished steps are remembered in <deps>/.provision/state.json.
Output for Setup: "PROGRESS <n> <total> <step> <text>" lines and one final "RESULT <json>" line.
Exit codes: 0 ready, 3 needs a one-time apt install (commands are printed), 1 a step failed
(its log is named).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BUILDER = REPO / "avatar-editor" / "local-builder"
PINS = json.loads((HERE / "toolchain.json").read_text(encoding="utf-8"))
LLVM = Path(PINS["platform"]["llvm"]["prefix"])
FORBIDDEN_REFERENCE = ("/opt/ps5-payload-sdk",)  # a clean setup must never borrow an existing install


class StepError(Exception):
    pass


class NeedsApt(Exception):
    def __init__(self, commands, missing):
        super().__init__("needs apt")
        self.commands, self.missing = commands, missing


# ---------------------------------------------------------------- helpers
class Provisioner:
    def __init__(self, deps: Path, jobs: int, rexglue: Path | None, xex: Path | None, quiet=False):
        self.deps = deps
        self.meta = deps / ".provision"
        self.src = self.meta / "src"
        self.build = self.meta / "build"
        self.prefix = self.meta / "prefix"
        self.cache = self.meta / "cache"
        self.logs = self.meta / "logs"
        self.jobs = jobs
        self.user = {"rexglue_sdk": rexglue, "xex-tool": xex}
        self.quiet = quiet
        for folder in (self.meta, self.src, self.build, self.prefix, self.cache, self.logs):
            folder.mkdir(parents=True, exist_ok=True)
        self.state_file = self.meta / "state.json"
        self.state = json.loads(self.state_file.read_text()) if self.state_file.is_file() else {}
        self.current = "setup"
        self.notes: list[str] = []

    # state
    def done(self, step: str, pin) -> bool:
        return self.state.get(step, {}).get("pin") == pin

    def mark(self, step: str, pin, **extra):
        self.state[step] = dict(pin=pin, at=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), **extra)
        tmp = self.state_file.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.state, indent=1, sort_keys=True))
        tmp.replace(self.state_file)

    def env(self, **extra):
        env = dict(os.environ)
        env["PATH"] = os.pathsep.join([str(self.deps / "host-tools"), str(LLVM / "bin"), str(self.prefix / "bin"),
                                       "/usr/local/bin", "/usr/bin", "/bin"])
        env["LLVM_CONFIG"] = str(LLVM / "bin" / "llvm-config")
        env.pop("PS5_PAYLOAD_SDK", None)
        env["LC_ALL"] = "C.UTF-8"
        env["LD_LIBRARY_PATH"] = str(self.prefix / "lib")
        env.update({k: str(v) for k, v in extra.items()})
        return env

    def run(self, argv, cwd=None, env=None, log_name=None, input_bytes=None):
        log = self.logs / f"{log_name or self.current}.log"
        with log.open("ab") as out:
            out.write(("\n$ " + " ".join(map(str, argv)) + "\n").encode())
            out.flush()
            result = subprocess.run([str(a) for a in argv], cwd=cwd, env=env or self.env(), stdout=out,
                                    stderr=subprocess.STDOUT, input=input_bytes)
        if result.returncode:
            tail = log.read_bytes()[-1500:].decode("utf-8", "replace").strip().splitlines()[-6:]
            raise StepError(f"{argv[0]} failed (exit {result.returncode}). Log: {log}\n  " + "\n  ".join(tail))

    def capture(self, argv, cwd=None):
        return subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True).stdout.strip()

    def git_fetch(self, url: str, commit: str, dest: Path, sparse=None, submodules=False):
        if (dest / ".git").exists() and self.capture(["git", "-C", dest, "rev-parse", "HEAD"]) == commit:
            return
        if dest.exists() or dest.is_symlink():
            if dest.is_symlink():
                dest.unlink()
            else:
                shutil.rmtree(dest)
        dest.mkdir(parents=True)
        self.run(["git", "init", "-q", dest])
        self.run(["git", "-C", dest, "remote", "add", "origin", url])
        if sparse:
            self.run(["git", "-C", dest, "sparse-checkout", "set", "--no-cone", *[f"/{p}/" for p in sparse]])
        self.run(["git", "-C", dest, "-c", "protocol.version=2", "fetch", "-q", "--depth", "1",
                  "--filter=blob:none", "origin", commit])
        self.run(["git", "-C", dest, "checkout", "-q", "--detach", "FETCH_HEAD"])
        if submodules:
            self.run(["git", "-C", dest, "submodule", "update", "--init", "--recursive", "--depth", "1"])
        if self.capture(["git", "-C", dest, "rev-parse", "HEAD"]) != commit:
            raise StepError(f"{url} did not check out {commit}")

    def download(self, url: str, sha256: str, name: str | None = None) -> Path:
        target = self.cache / (name or url.rsplit("/", 1)[-1])
        if target.is_file() and file_sha256(target) == sha256:
            return target
        part = target.with_suffix(target.suffix + ".part")
        request = urllib.request.Request(url, headers={"User-Agent": "ExBox360-Setup"})
        with urllib.request.urlopen(request, timeout=120) as response, part.open("wb") as out:
            shutil.copyfileobj(response, out, 1 << 20)
        if file_sha256(part) != sha256:
            part.unlink()
            raise StepError(f"{url} did not match its pinned SHA-256; nothing was used.")
        part.replace(target)
        return target

    def extract(self, archive: Path, dest: Path):
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)
        with tarfile.open(archive) as tar:
            tar.extractall(dest, filter="data")
        inner = [p for p in dest.iterdir()]
        if len(inner) == 1 and inner[0].is_dir():  # strip the single top folder
            for child in inner[0].iterdir():
                child.rename(dest / child.name)
            inner[0].rmdir()

    def link(self, link: Path, target: Path):
        link.parent.mkdir(parents=True, exist_ok=True)
        if link.is_symlink() or link.is_file():
            link.unlink()
        elif link.is_dir():
            shutil.rmtree(link)
        link.symlink_to(target)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def pinned_files(root_name: str) -> list[dict]:
    """Every per-file pin the editor builder records for one source role."""
    rows = []
    manifests = [BUILDER / "external-source-manifest.json", *sorted((BUILDER / "recipes").glob("*/manifest.json"))]
    for manifest in manifests:
        data = json.loads(manifest.read_text(encoding="utf-8"))
        for row in data.get("copies", []) if isinstance(data, dict) else []:
            if row.get("root") == root_name and row.get("path") and row.get("input_sha256"):
                rows.append(row)
    if root_name == "xex-tool":
        contract = json.loads((BUILDER / "xex-extract-contract.json").read_text(encoding="utf-8"))
        rows += [dict(path=p, input_sha256=[h]) for p, h in contract.get("provided_tool_source_files", {}).items()]
    return rows


def verify_tree(root_name: str, folder: Path) -> tuple[int, list[str]]:
    rows = pinned_files(root_name)
    seen, bad = set(), []
    for row in rows:
        if row["path"] in seen:
            continue
        seen.add(row["path"])
        path = folder / row["path"]
        accepted = row["input_sha256"] if isinstance(row["input_sha256"], list) else [row["input_sha256"]]
        if not path.is_file():
            bad.append(f"missing {row['path']}")
        elif file_sha256(path) not in accepted:
            bad.append(f"different {row['path']}")
    return len(seen), bad


# ---------------------------------------------------------------- steps
def step_preflight(p: Provisioner):
    if " " in str(p.deps):
        raise StepError("Pick a tools folder without spaces in its path (the SDK's scripts need that).")
    if str(p.deps).startswith("/mnt/"):
        p.notes.append("The tools folder is on the Windows drive; building there is much slower than in WSL's own home.")
    release = dict(line.split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line)
    if release.get("ID") != "ubuntu" or release.get("VERSION_ID", "").strip('"') != PINS["platform"]["release"]:
        p.notes.append(f"Tested on Ubuntu {PINS['platform']['release']}; this is {release.get('PRETTY_NAME', '?')}.")
    plat = PINS["platform"]
    def installed(package):
        out = subprocess.run(["dpkg-query", "-W", "-f", "${Status} ${Version}", package], capture_output=True, text=True)
        return out.stdout.split(" ", 3)[-1] if "install ok installed" in out.stdout else None
    missing_base = [pkg for pkg in plat["apt_base"] if not installed(pkg)]
    llvm = plat["llvm"]
    wrong_llvm = [pkg for pkg in llvm["packages"] if installed(pkg) != llvm["version"]]
    if missing_base or wrong_llvm:
        commands = []
        if missing_base:
            commands.append("sudo apt-get install -y " + " ".join(missing_base))
        if wrong_llvm:
            commands += [
                f"wget -qO- {llvm['key_url']} | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null",
                f"echo 'deb http://apt.llvm.org/noble/ {llvm['suite']} main' | sudo tee /etc/apt/sources.list.d/llvm-21.list",
                "sudo apt-get update",
                "sudo apt-get install -y " + " ".join(f"{pkg}={llvm['version']}" for pkg in llvm["packages"]),
            ]
        raise NeedsApt(commands, missing_base + wrong_llvm)
    version = p.capture([LLVM / "bin" / "clang", "--version"])
    if "21.1.8" not in version or "2078da43e25a" not in version:
        raise StepError(f"{LLVM}/bin/clang is not the pinned 21.1.8 (2078da43): {version.splitlines()[:1]}")
    for tool in ("ld.lld", "llvm-ar", "llvm-config", "llvm-tblgen", "clang-tblgen", "llvm-nm", "llvm-readelf"):
        if not (LLVM / "bin" / tool).exists():
            raise StepError(f"{LLVM}/bin/{tool} is missing; install the LLVM packages listed in toolchain.json.")
    pil = p.capture(["/usr/bin/python3", "-c", "import PIL,mako,yaml;from PIL import features;"
                     "print(PIL.__version__,features.version('freetype2'),features.version('raqm'))"])
    if pil.split() != ["10.2.0", "2.13.2", "0.10.1"]:
        raise StepError(f"Python's Pillow is {pil or 'missing'}; Ubuntu 24.04's python3-pil (10.2.0) is required.")
    return "Ubuntu packages and LLVM 21.1.8 are in place."


def step_sources(p: Provisioner):
    report = []
    for item in PINS["sources"]:
        dest = p.deps / "sources" / item["role"]
        key = "source:" + item["role"]
        if not p.done(key, item["commit"]) or not dest.is_dir():
            progress(p, f"fetching {item['role']}")
            p.git_fetch(item["git"], item["commit"], dest)
            prepare_source(p, item["role"], dest)
            count, bad = verify_tree(item["role"], dest)
            if bad:
                raise StepError(f"{item['role']} at {item['commit'][:12]} does not match {len(bad)} pinned files "
                                f"(e.g. {bad[0]}).")
            p.mark(key, item["commit"], checked_files=count)
        report.append(f"{item['role']} ({p.state[key].get('checked_files', 0)} files checked)")
    return "Fetched and checked: " + ", ".join(report)


def prepare_source(p: Provisioner, role: str, dest: Path):
    """Pinned, documented changes the builder expects on top of a fetched source (see toolchain.json)."""
    for change in PINS.get("source_transforms", []):
        if change["role"] != role:
            continue
        path = dest / change["path"]
        text = path.read_text(encoding="utf-8")  # reads either line ending as "\n"
        if change["new"] not in text:
            if text.count(change["old"]) != 1:
                raise StepError(f"{role}/{change['path']} does not contain the expected line to change.")
            text = text.replace(change["old"], change["new"])
        # Some pinned files keep Windows line endings; write those exactly as pinned.
        path.write_text(text, encoding="utf-8", newline="\r\n" if change.get("line_endings") == "crlf" else "\n")
    for wrap in PINS.get("source_wraps", []):
        if wrap["role"] != role:
            continue
        spec = parse_wrap(dest / wrap["wrap"])
        folder = dest / "subprojects" / spec["directory"]
        if folder.is_dir():
            continue
        source = p.download(spec["source_url"], spec["source_hash"], name=spec["source_filename"])
        patch = p.download(spec["patch_url"], spec["patch_hash"], name=spec["patch_filename"])
        with tarfile.open(source) as tar:
            tar.extractall(dest / "subprojects", filter="data")
        import zipfile
        with zipfile.ZipFile(patch) as archive:
            for info in archive.infolist():
                target = (dest / "subprojects" / info.filename).resolve()
                if not target.is_relative_to((dest / "subprojects").resolve()):
                    raise StepError("Unsafe file name in the Meson wrap patch.")
            archive.extractall(dest / "subprojects")


def parse_wrap(path: Path) -> dict:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.lstrip().startswith(("#", "[")):
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    for key in ("directory", "source_url", "source_hash", "source_filename", "patch_url", "patch_hash", "patch_filename"):
        if not values.get(key):
            raise StepError(f"{path.name} has no {key}.")
    if not values["source_url"].startswith(("http://zlib.net/", "https://zlib.net/")) and "github.com/mesonbuild" not in values["source_url"]:
        raise StepError(f"{path.name} points at an unexpected source host.")
    if not values["patch_url"].startswith("https://wrapdb.mesonbuild.com/"):
        raise StepError(f"{path.name} points at an unexpected patch host.")
    return values


def step_tool_sources(p: Provisioner):
    """ReXGlue and XexTool-RE, fetched from their official projects at the pinned commits (owner
    decision 2026-11-11). A folder passed with --rexglue-source / --xex-tool-source is used instead."""
    report = []
    for item in PINS["tool_sources"]:
        role, dest = item["role"], p.deps / "sources" / item["role"]
        key = "source:" + role
        given = p.user.get(role)
        if given:
            given = Path(given).expanduser().resolve()
            if not given.is_dir():
                raise StepError(f"{given} is not a folder.")
            if any(str(given).startswith(f) for f in FORBIDDEN_REFERENCE):
                raise StepError(f"{given} is an existing system install, not a source folder.")
            count, bad = verify_tree(role, given)
            if bad:
                raise StepError(f"Your {role} folder is not the supported version ({len(bad)} files differ, e.g. "
                                f"{bad[0]}). Leave it out and Setup fetches the right one.")
            p.link(dest, given)
            p.mark(key, item["commit"], checked_files=count, supplied=str(given))
        elif (not p.done(key, item["commit"]) or not dest.is_dir() or dest.is_symlink()
              or p.state.get(key, {}).get("supplied")):
            progress(p, f"fetching {role}")
            p.git_fetch(item["git"], item["commit"], dest, submodules=item.get("submodules", False))
            prepare_source(p, role, dest)
            count, bad = verify_tree(role, dest)
            if bad:
                raise StepError(f"{role} at {item['commit'][:12]} does not match {len(bad)} pinned files "
                                f"(e.g. {bad[0]}).")
            p.mark(key, item["commit"], checked_files=count)
        report.append(f"{role} ({p.state[key].get('checked_files', 0)} files checked)")
    return "Fetched and checked: " + ", ".join(report)


def step_native_sdk(p: Provisioner):
    """The SDK exactly as the editor builder's pins were made (see toolchain.json native_sdk.why)."""
    spec = PINS["native_sdk"]
    fork = next(s for s in PINS["sources"] if s["role"] == spec["fork_role"])
    sdk = p.deps / "native-sdk"
    pin = [spec["base_release_sha256"], spec["addons"]["sha256"], fork["commit"],
           hashlib.sha256(json.dumps(spec["wrapper_fixes"]).encode()).hexdigest()]
    if not p.done("native-sdk", pin):
        if sdk.exists():
            shutil.rmtree(sdk)
        # 1. The official payload SDK release (unzip keeps its file modes and links).
        unpacked = p.build / "sdk-release"
        if unpacked.exists():
            shutil.rmtree(unpacked)
        unpacked.mkdir(parents=True)
        base = p.download(spec["base_release"], spec["base_release_sha256"],
                          name="ps5-payload-sdk-" + spec["base_release"].split("/")[-2] + ".zip")
        p.run(["unzip", "-q", base, "-d", unpacked])
        (unpacked / spec["base_prefix"].rstrip("/")).rename(sdk)
        # 2. The official pacbrew bundle over it.
        install_addons(p, sdk, spec["addons"])
        # 3. The PS5 fork's headers and compiler wrappers on top.
        tree = p.build / "sdk-fork-export"
        if tree.exists():
            shutil.rmtree(tree)
        tree.mkdir(parents=True)
        archive = subprocess.run(["git", "-C", p.deps / "sources" / spec["fork_role"], "archive", fork["commit"]],
                                 capture_output=True, check=True).stdout
        p.run(["tar", "-x", "-C", tree], input_bytes=archive)
        for source, dest in spec["fork_overlay"]:
            if source.endswith("/"):
                (sdk / dest).mkdir(parents=True, exist_ok=True)
                p.run(["cp", "-a", "--remove-destination", f"{tree / source}/.", f"{sdk / dest}/"])
            else:
                shutil.copy2(tree / source, sdk / dest)
        # 4. Quoting fixes so the wrappers work in folders with spaces.
        for name, old, new in spec["wrapper_fixes"]:
            target = sdk / name
            text = target.read_text()
            if old not in text:
                raise StepError(f"{name} no longer contains the line its quoting fix expects.")
            target.write_text(text.replace(old, new))
        count, bad = verify_tree("native-sdk", sdk)
        if bad:
            raise StepError(f"native-sdk differs from the builder's pins in {len(bad)} files (e.g. {bad[0]}).")
        p.mark("native-sdk", pin, checked_files=count)
    # One SDK serves both roles the builder names.
    p.link(p.deps / "payload-sdk", Path("native-sdk"))
    return (f"Payload SDK v0.44 + pacbrew v0.41.1 + PS5 fork {fork['commit'][:12]} "
            f"({p.state['native-sdk']['checked_files']} headers checked).")


def install_addons(p: Provisioner, sdk: Path, addons: dict):
    """Unpack the SDK folder of the official pacbrew bundle over the SDK."""
    archive = p.download(addons["url"], addons["sha256"], name="ps5-payload-dev-" + addons["url"].split("/")[-2] + ".tar.gz")
    into = sdk / addons.get("into", "")
    into.mkdir(parents=True, exist_ok=True)
    prefix = addons["subtree"]
    with tarfile.open(archive) as tar:
        members = []
        for member in tar.getmembers():
            if member.name.startswith(prefix) and len(member.name) > len(prefix):
                member.name = member.name[len(prefix):]
                if member.islnk():  # hard links name their target by its full path in the archive
                    if not member.linkname.startswith(prefix):
                        continue
                    member.linkname = member.linkname[len(prefix):]
                if member.issym():  # e.g. target/include_common: the release has an empty folder there
                    there = into / member.name
                    if there.is_dir() and not there.is_symlink():
                        if any(there.iterdir()):
                            raise StepError(f"{member.name} is a link in the bundle but a non-empty folder in the SDK.")
                        there.rmdir()
                members.append(member)
        tar.errorlevel = 2  # a member that cannot be written is an error, not a silent skip
        tar.extractall(into, members=members, filter="data")


def verify_link_inputs(p: Provisioner) -> list[str]:
    """The builder's link step checks these tool and library files by SHA-256; check them here first."""
    plan = json.loads((BUILDER / "link-plan.json").read_text(encoding="utf-8"))
    bad = []
    for item in plan["required_files"]:
        if item["path"].startswith("${dependencies}/"):
            path = p.deps / item["path"][len("${dependencies}/"):]
            if not path.is_file():
                bad.append("missing " + item["path"][16:])
            elif file_sha256(path) != item["sha256"]:
                bad.append("different " + item["path"][16:])
    return bad


def step_system_links(p: Provisioner):
    p.link(p.deps / "llvm21", LLVM)
    (p.deps / "python" / "bin").mkdir(parents=True, exist_ok=True)
    p.link(p.deps / "python" / "bin" / "python3", Path("/usr/bin/python3"))
    return "llvm21 -> LLVM 21.1.8; python -> Ubuntu python3 with Pillow 10.2.0, Mako, PyYAML."


def step_dxc(p: Provisioner):
    item = next(t for t in PINS["git_tools"] if t["name"] == "dxc")
    dest = p.deps / "dxc" / "source"
    if not p.done("dxc", item["commit"]):
        p.git_fetch(item["git"], item["commit"], dest, sparse=item["sparse"])
        if not (dest / "include" / "dxc").is_dir():
            raise StepError("The DXC headers were not found after checkout.")
        p.mark("dxc", item["commit"])
    return f"DXC headers at {item['commit'][:12]}."


def step_venv(p: Provisioner):
    pin = [w["sha256"] for w in PINS["python_wheels"]]
    venv = p.meta / "venv"
    if not p.done("venv", pin):
        if venv.exists():
            shutil.rmtree(venv)
        p.run(["/usr/bin/python3", "-m", "venv", venv])
        req = p.meta / "requirements.txt"
        req.write_text("".join(f"{w['name']} --hash=sha256:{w['sha256']}\n" for w in PINS["python_wheels"]))
        p.run([venv / "bin" / "pip", "install", "-q", "--no-deps", "--only-binary", ":all:", "--require-hashes",
               "-r", req])
        p.mark("venv", pin)
    return "Meson 1.9.1 and Mako 1.3.10 (hash-checked wheels from PyPI)."


def git_tool(p: Provisioner, name: str) -> Path:
    item = next(t for t in PINS["git_tools"] if t["name"] == name)
    dest = p.src / name
    if not p.done("git:" + name, item["commit"]):
        p.git_fetch(item["git"], item["commit"], dest, sparse=item.get("sparse"))
        p.mark("git:" + name, item["commit"])
    return dest


def cmake_build(p: Provisioner, name, source, options, targets=None, install=False):
    build = p.build / name
    p.run(["cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           f"-DCMAKE_C_COMPILER={LLVM}/bin/clang", f"-DCMAKE_CXX_COMPILER={LLVM}/bin/clang++",
           f"-DCMAKE_INSTALL_PREFIX={p.prefix}", *options])
    argv = ["cmake", "--build", build, "--parallel", str(p.jobs)]
    for target in targets or []:
        argv += ["--target", target]
    p.run(argv)
    if install:
        p.run(["cmake", "--install", build])
    return build


def step_spirv_tools(p: Provisioner):
    source, headers = git_tool(p, "spirv-tools"), git_tool(p, "spirv-headers-tools")
    pin = [p.state["git:spirv-tools"]["pin"], p.state["git:spirv-headers-tools"]["pin"]]
    if not p.done("spirv-tools", pin):
        build = cmake_build(p, "spirv-tools", source, [f"-DSPIRV-Headers_SOURCE_DIR={headers}", "-DSPIRV_SKIP_TESTS=ON",
                            "-DSPIRV_WERROR=OFF", "-DBUILD_SHARED_LIBS=OFF"], targets=["spirv-val", "spirv-dis"])
        for tool in ("spirv-val", "spirv-dis"):
            p.link(p.deps / "host-tools" / tool, build / "tools" / tool)
        p.mark("spirv-tools", pin)
    return "spirv-val and spirv-dis built."


def step_glslang(p: Provisioner):
    source = git_tool(p, "glslang")
    pin = p.state["git:glslang"]["pin"]
    if not p.done("glslang", pin):
        build = cmake_build(p, "glslang", source, ["-DBUILD_TESTING=OFF", "-DGLSLANG_TESTS=OFF", "-DENABLE_OPT=OFF",
                            "-DENABLE_GLSLANG_BINARIES=ON"])
        binary = next((b for b in (build / "StandAlone" / "glslang", build / "StandAlone" / "glslangValidator")
                       if b.is_file()), None)
        if not binary:
            raise StepError("glslang built no compiler.")
        p.link(p.deps / "host-tools" / "glslangValidator", binary)
        p.mark("glslang", pin)
    return "glslangValidator built."


def step_translator(p: Provisioner):
    source, headers = git_tool(p, "spirv-llvm-translator"), git_tool(p, "spirv-headers-translator")
    pin = [p.state["git:spirv-llvm-translator"]["pin"], p.state["git:spirv-headers-translator"]["pin"], "rpath"]
    if not p.done("translator", pin):
        cmake_build(p, "translator", source, [f"-DLLVM_DIR={LLVM}/lib/cmake/llvm",
                    f"-DLLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR={headers}", "-DLLVM_SPIRV_INCLUDE_TESTS=OFF",
                    "-DLLVM_SPIRV_ENABLE_LIBSPIRV_DIS=OFF", "-DBUILD_SHARED_LIBS=ON", "-DLLVM_LINK_LLVM_DYLIB=ON",
                    f"-DCMAKE_INSTALL_RPATH={p.prefix}/lib"],
                    install=True)
        p.mark("translator", pin)
    return "SPIRV-LLVM-Translator built."


def step_llvm_sources(p: Provisioner):
    pin = [d["sha256"] for d in PINS["downloads"] if d["name"] in ("clang-src", "libclc-src", "llvm-cmake-src")]
    root = p.src / "llvm-21.1.8"
    if not p.done("llvm-sources", pin):
        for name, folder in (("clang-src", "clang"), ("libclc-src", "libclc"), ("llvm-cmake-src", "cmake")):
            item = next(d for d in PINS["downloads"] if d["name"] == name)
            p.extract(p.download(item["url"], item["sha256"]), root / folder)
        p.mark("llvm-sources", pin)
    return "Clang and libclc 21.1.8 source (official release files, SHA-256 checked)."


def step_libclc(p: Provisioner):
    pin = [p.state["llvm-sources"]["pin"], p.state["translator"]["pin"]]
    if not p.done("libclc", pin):
        cmake_build(p, "libclc", p.src / "llvm-21.1.8" / "libclc", [f"-DLLVM_DIR={LLVM}/lib/cmake/llvm",
                    "-DLIBCLC_TARGETS_TO_BUILD=spirv-mesa3d-;spirv64-mesa3d-", f"-DLLVM_SPIRV={p.prefix}/bin/llvm-spirv"],
                    install=True)
        p.mark("libclc", pin)
    return "libclc (SPIR-V targets) built."


def step_clang_api(p: Provisioner):
    pin = p.state["llvm-sources"]["pin"]
    if not p.done("clang-api", pin):
        cmake_build(p, "clang-api", p.src / "llvm-21.1.8" / "clang", [f"-DLLVM_DIR={LLVM}/lib/cmake/llvm",
                    "-DLLVM_INCLUDE_TESTS=OFF", "-DCLANG_INCLUDE_TESTS=OFF", "-DCLANG_BUILD_TOOLS=OFF",
                    "-DCLANG_INCLUDE_DOCS=OFF", "-DCLANG_BUILD_EXAMPLES=OFF", f"-DCLANG_TABLEGEN={LLVM}/bin/clang-tblgen",
                    f"-DLLVM_TABLEGEN={LLVM}/bin/llvm-tblgen"], targets=["clang-tablegen-targets"])
        p.mark("clang-api", pin)
    return "Clang API headers generated."


def step_mesa_clc(p: Provisioner):
    mesa = p.deps / "sources" / "mesa-source"
    pin = [p.state["source:mesa-source"]["pin"], p.state["libclc"]["pin"], p.state["clang-api"]["pin"],
           p.state["venv"]["pin"]]
    if not p.done("mesa-clc", pin):
        pkgconfig = p.meta / "pkgconfig"
        pkgconfig.mkdir(exist_ok=True)
        for sub in ("lib/pkgconfig/LLVMSPIRVLib.pc", "share/pkgconfig/libclc.pc"):
            shutil.copy2(p.prefix / sub, pkgconfig / Path(sub).name)
        libclang = sorted((LLVM / "lib").glob("libclang-cpp.so.21*"))
        if not libclang:
            raise StepError("libclang-cpp.so.21 was not found in the LLVM packages.")
        p.link(p.prefix / "lib" / "libclang-cpp.so", libclang[0])
        build = p.build / "mesa-clc"
        if build.exists():
            shutil.rmtree(build)
        clang_src = p.src / "llvm-21.1.8" / "clang"
        env = p.env(CC=LLVM / "bin" / "clang", CXX=LLVM / "bin" / "clang++",
                    CXXFLAGS=f"-I{clang_src}/include -I{p.build}/clang-api/include",
                    LDFLAGS=f"-L{p.prefix}/lib -Wl,-rpath,{p.prefix}/lib", PKG_CONFIG_PATH=pkgconfig,
                    LD_LIBRARY_PATH=p.prefix / "lib")
        meson = p.meta / "venv" / "bin" / "meson"
        p.run([meson, "setup", build, mesa, "-Dbuildtype=release", "-Dmesa-clc=enabled", "-Dinstall-mesa-clc=true",
               "-Dgallium-drivers=", "-Dvulkan-drivers=", "-Dplatforms=", "-Dglx=disabled", "-Degl=disabled",
               "-Dgbm=disabled", "-Dopengl=false", "-Dgles1=disabled", "-Dgles2=disabled", "-Dllvm=enabled",
               "-Dshared-llvm=enabled", "-Dbuild-tests=false", "-Dvalgrind=disabled", "-Dlibunwind=disabled",
               "-Dzstd=disabled", "-Dxmlconfig=disabled", "-Dtools="], env=env)
        p.run(["ninja", "-C", build, f"-j{p.jobs}", "src/compiler/clc/mesa_clc", "src/compiler/spirv/vtn_bindgen2"],
              env=env)
        p.link(p.deps / "host-tools" / "mesa_clc", build / "src" / "compiler" / "clc" / "mesa_clc")
        p.link(p.deps / "host-tools" / "vtn_bindgen2", build / "src" / "compiler" / "spirv" / "vtn_bindgen2")
        p.mark("mesa-clc", pin)
    return "mesa_clc and vtn_bindgen2 built from the PS5 Mesa fork."


def step_native_tool(p: Provisioner):
    """Built exactly as the editor builder's pin was made: clang++-21 -O2 against Ubuntu's zlib (-lz).
    The result is byte-for-byte the pinned ps5-native-tool (48bb5f0b...)."""
    vulkan = p.deps / "sources" / "vulkan-port-source"
    pin = [p.state["source:vulkan-port-source"]["pin"], "system-zlib"]
    if not p.done("ps5-native-tool", pin):
        native = vulkan / "tooling" / "native"
        out = p.deps / "native-tools" / "ps5-native-tool"
        out.parent.mkdir(parents=True, exist_ok=True)
        p.run([LLVM / "bin" / "clang++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
               "native_app_builder.cpp", "self_container.cpp", "elf_object.cpp", "sce_module_writer.cpp",
               "-lz", "-o", out], cwd=native)
        p.mark("ps5-native-tool", pin)
    return "ps5-native-tool built from PS5_Vulkan against Ubuntu's zlib."


def stage_plain_checkout(p: Provisioner, source: Path) -> Path:
    """ReXGlue's CMake insists each vendored submodule folder has a `.git` entry. A folder copied
    without git (files only) has everything else, so build from a hard-linked copy that gets an
    empty git repository in each submodule folder that has files. Your own folder is never written to."""
    gitmodules = source / ".gitmodules"
    paths = re.findall(r"^\s*path\s*=\s*(\S+)", gitmodules.read_text(), re.M) if gitmodules.is_file() else []
    need = [d for d in paths if (source / d).is_dir() and any((source / d).iterdir()) and not (source / d / ".git").exists()]
    if not need:
        return source
    staged = p.deps / ".provision" / "build" / "rexglue-source"
    if staged.exists():
        shutil.rmtree(staged)
    staged.parent.mkdir(parents=True, exist_ok=True)
    if subprocess.run(["cp", "-al", str(source), str(staged)], capture_output=True).returncode != 0:
        shutil.rmtree(staged, ignore_errors=True)  # different drive: hard links impossible, copy instead
        p.run(["cp", "-a", str(source), str(staged)])
    for d in need:  # a real (empty) repository: some vendored builds read HEAD from it
        p.run(["git", "-C", str(staged / d), "init", "-q"])
        p.run(["git", "-C", str(staged / d), "-c", "user.name=Ex-Box Setup", "-c", "user.email=setup@localhost",
               "commit", "-q", "--allow-empty", "-m", "files copied without git"])
    return staged


def step_rexglue_tool(p: Provisioner):
    source = p.deps / "sources" / "rexglue_sdk"
    item = next(s for s in PINS["tool_sources"] if s["role"] == "rexglue_sdk")
    if p.state.get("source:rexglue_sdk", {}).get("pin") != item["commit"] or not source.exists():
        raise StepError("ReXGlue's source is not in place; run the tool-sources step first.")
    pin = item["commit"]
    if not p.done("rexglue-tool", pin):
        lost = [f.name for f in (source / "thirdparty/libmspack/cabextract/mspack").glob("*.[ch]") if f.stat().st_size == 0]
        if lost:
            raise StepError("Your ReXGlue folder has empty files where git keeps links "
                            f"(thirdparty/libmspack/cabextract/mspack/{lost[0]} and {len(lost) - 1} more). That happens "
                            "when it is cloned or copied on Windows. Clone it inside Ubuntu instead: "
                            "git clone --recursive https://github.com/rexglue/rexglue-sdk.git, then check out "
                            f"commit {pin[:8]} and run git submodule update --init --recursive.")
        built_from = stage_plain_checkout(p, source.resolve())
        build = cmake_build(p, "rexglue", built_from, ["-DCMAKE_CXX_STANDARD=23", "-DCMAKE_C_FLAGS=-march=x86-64-v2",
                            "-DCMAKE_CXX_FLAGS=-march=x86-64-v2", "-DREXGLUE_BUILD_TESTS=OFF"], targets=["rexglue"])
        # ReXGlue writes its program to <source>/out/<platform>/, not to the build folder
        found = sorted((q for where in (build, built_from / "out") if where.is_dir()
                        for q in where.rglob("rexglue") if q.is_file() and os.access(q, os.X_OK)),
                       key=lambda q: len(q.parts))
        if not found:
            raise StepError("The ReXGlue build produced no rexglue program.")
        binary = found[0]
        version = p.capture([binary, "--version"])
        if not re.fullmatch(r"0\.10\.0(?:\.0)?(?:[-+][A-Za-z0-9_.-]+)?", version.strip()):
            raise StepError(f"rexglue reports version {version!r}; 0.10.0 is required.")
        p.link(p.deps / "host-tools" / "rexglue", binary)
        p.mark("rexglue-tool", pin, version=version.strip())
    return f"rexglue {p.state['rexglue-tool'].get('version', '')} built from ReXGlue's source."


def step_link_inputs(p: Provisioner):
    bad = verify_link_inputs(p)
    if bad:
        raise StepError(f"{len(bad)} files the editor links against differ from the builder's pins: " + ", ".join(bad[:4]))
    return "Every SDK library and tool the editor links against matches the builder's pins."


STEPS = [
    ("preflight", "Checking this PC's build packages", step_preflight),
    ("sources", "Fetching the pinned public sources", step_sources),
    ("tool-sources", "Fetching ReXGlue and XexTool-RE", step_tool_sources),
    ("native-sdk", "Setting up the PS5 payload SDK", step_native_sdk),
    ("links", "Linking LLVM 21 and Python", step_system_links),
    ("dxc", "Fetching shader-compiler headers", step_dxc),
    ("venv", "Installing Meson and Mako", step_venv),
    ("spirv-tools", "Building SPIR-V tools", step_spirv_tools),
    ("glslang", "Building the GLSL compiler", step_glslang),
    ("translator", "Building the SPIR-V translator", step_translator),
    ("llvm-sources", "Fetching Clang and libclc source", step_llvm_sources),
    ("libclc", "Building libclc", step_libclc),
    ("clang-api", "Generating Clang API headers", step_clang_api),
    ("mesa-clc", "Building Mesa's shader tools", step_mesa_clc),
    ("ps5-native-tool", "Building the PS5 app converter", step_native_tool),
    ("rexglue-tool", "Building ReXGlue", step_rexglue_tool),
    ("link-inputs", "Checking the files the editor links against", step_link_inputs),
]


def progress(p: Provisioner, text: str):
    index = next((i for i, s in enumerate(STEPS, 1) if s[0] == p.current), 0)
    if not p.quiet:
        print(f"PROGRESS {index} {len(STEPS)} {p.current} {text}", flush=True)


def provision(deps: Path, jobs: int, rexglue=None, xex=None, only=None) -> tuple[int, dict]:
    p = Provisioner(deps, jobs, rexglue, xex)
    results = []
    for name, title, function in STEPS:
        if only and name not in only:
            continue
        p.current = name
        progress(p, title)
        try:
            text = function(p)
        except NeedsApt as need:
            result = dict(status="needs-apt", step=name, missing=need.missing, commands=need.commands,
                          why=PINS["platform"]["apt_base_why"] + " " + PINS["platform"]["llvm"]["why"])
            return 3, result
        except (StepError, subprocess.CalledProcessError, OSError, urllib.error.URLError, KeyError, ValueError, tarfile.TarError) as error:
            return 1, dict(status="failed", step=name, error=str(error), steps=results, notes=p.notes)
        results.append(dict(step=name, text=text or ""))
    (deps / "PROVISIONED.json").write_text(json.dumps(dict(pins=PINS, state=p.state), indent=1))
    return 0, dict(status="ready", dependencies=str(deps), steps=results, notes=p.notes)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--deps", type=Path, default=Path.home() / ".local/share/exbox-toolchain/dependencies")
    parser.add_argument("--rexglue-source", type=Path)
    parser.add_argument("--xex-tool-source", type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, min(os.cpu_count() or 2, 8)))
    parser.add_argument("--only", nargs="+", choices=[s[0] for s in STEPS])
    parser.add_argument("--check", action="store_true", help="only report what is already set up")
    args = parser.parse_args(argv)
    deps = args.deps.expanduser().absolute()
    if args.check:
        state = deps / ".provision" / "state.json"
        print("RESULT " + json.dumps(dict(status="check", state=json.loads(state.read_text()) if state.is_file() else {})))
        return 0
    deps.mkdir(parents=True, exist_ok=True)
    code, result = provision(deps, args.jobs, args.rexglue_source, args.xex_tool_source, args.only)
    print("RESULT " + json.dumps(result), flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
