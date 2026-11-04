# SPDX-License-Identifier: GPL-3.0-or-later
"""Presentation only: setup.py remains the only importer and writer."""
import json
from pathlib import PureWindowsPath

def wsl_path(value):
    p=PureWindowsPath(str(value))
    if not p.is_absolute() or len(p.drive)!=2 or p.drive[1]!=":" or not p.drive[0].isalpha():
        raise ValueError("The host build needs a local drive path, such as C:\\ExBox.")
    return "/mnt/"+p.drive[0].lower()+"/"+"/".join(p.parts[1:])

def command(project, inputs, args, executable, windows=False, build=False):
    if windows and build:
        project=wsl_path(project)
        extra=["--inputs",wsl_path(inputs)] if inputs else []
        return ["wsl.exe","-d","Ubuntu-24.04","--cd",project,"--","python3","-u","setup.py",*extra,*args]
    extra=["--inputs",str(inputs)] if inputs else []
    return [str(executable),"-u",str(project)+"/setup.py",*extra,*args]

def result_document(output):
    # Progress lines may precede setup's final formatted JSON. Only accept a
    # complete object whose remaining suffix is whitespace; no brace guessing.
    decoder=json.JSONDecoder()
    for n,line in enumerate(output.splitlines(keepends=True)):
        if not line.lstrip().startswith("{"):continue
        tail="".join(output.splitlines(keepends=True)[n:]).lstrip()
        try: value,end=decoder.raw_decode(tail)
        except ValueError:continue
        if isinstance(value,dict) and not tail[end:].strip():return value
    return None

def friendly_error(text):
    low=text.lower()
    if "missing input:" in low or "no supported inputs" in low:
        return "No usable input was found. Choose the files or folder you downloaded, then check again."
    if "invalid zip" in low or "invalid update zip" in low or "crc" in low or "badzipfile" in low:
        return "This archive is damaged or incomplete. Keep the original and download a fresh copy."
    if "unsupported or missing 12581" in low:
        return "The avatar editor needs its matching original executable and shader pack. This pair is missing or does not match."
    if "requires-delta-base" in low:
        return "This is a delta update. Add its matching original base update, then prepare again. The delta has been preserved."
    if "local xex tool source" in low:
        return "Raw updates need the local recovery tool source in inputs/tool-sources/xextool-re."
    if "permission" in low or "access is denied" in low:
        return "Setup cannot read or write this location. Choose a local folder you can access."
    if "symlink" in low:
        return "Choose the original folder instead of a folder link. No linked files were imported."
    if "unmapped existing" in low or "conflict" in low:
        return "Existing output does not match these inputs. It has been preserved; review the details before preparing again."
    return "Setup could not finish. Your originals are retained; the details below identify the failed step."

def row_message(row):
    state=row.get("status", "unknown")
    if state=="requires-delta-base":return "Matching original base update needed; delta retained."
    if state=="damaged":return "Damaged or incomplete archive; get a fresh copy."
    if state=="unverified":return "Version not verified; keep this source for mapping or its matching base update."
    if state=="unknown":return "Not recognized yet; source retained."
    return row.get("verification",row.get("kind","Recognized input"))


def feature_message(value):
    """Translate display copy only; preserve the engine's machine status."""
    if value == "missing mapped module input":
        return "Add original dashboard files for this era"
    return value
