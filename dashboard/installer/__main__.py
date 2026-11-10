"""Ex-Box Setup entry point.

    python -m installer                         open the installer window
    python -m installer scan FOLDER             show the found / missing / unlocks checklist
    python -m installer install FOLDER --practice     full install into a practice folder (no PS5)
    python -m installer install FOLDER --console IP   install to a PS5 (asks first)
    python -m installer doctor  --practice | --console IP
    python -m installer restore --practice | --console IP
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import VERSION, catalog, engine
from .targets import FtpTarget, LocalTarget

PAYLOAD = Path(__file__).resolve().parent / "payload"  # the dashboard package shipped with a release
MARK = {"found": "[FOUND]  ", "download": "[FETCH]  ", "partial": "[PARTLY] ", "missing": "[MISSING]",
        "build": "[BUILD]  "}


def do_scan(folder) -> dict:
    return catalog.scan(folder, catalog.Hasher(engine.home() / "hash-cache.json"), [PAYLOAD])


def print_checklist(result: dict, record: dict | None = None) -> None:
    print(f"\nFolder: {result['folder']}   ({result['files_seen']:,} files looked at)\n")
    group = None
    for item in result["items"]:
        if item["group"] != group:
            group = item["group"]
            print(f"== {group} ==")
        print(f"  {MARK[item['status']]} {item['title']}")
        if item["status"] in ("found", "download"):
            print(f"             {item['detail']}")
            print(f"             Unlocks: {item['unlocks']}")
        elif item["status"] == "build":
            print(f"             {item['detail']}")
            print(f"             Unlocks: {item['unlocks']}")
            for command in item["build"]["commands"]:
                print(f"               $ {command}")
            print(f"             Then it appears at: {item['build']['output']}")
            print(f"             Needs: {item['build']['needs']}")
        else:
            if item["detail"]:
                print(f"             {item['detail']}")
            print(f"             Without it: {item['missing']}")
            print(f"             Where it comes from: {item['source']}")
        for note in item["notes"]:
            print(f"             Note: {note}")
    if result["wrong"]:
        print("\n== Files that could not be used ==")
        for line in result["wrong"]:
            print("  - " + line)
    print("\n== What you'll get ==")
    for line in result["preview"]["get"]:
        print("  + " + line)
    for line in result["preview"]["skip"]:
        print("  - " + line)
    news = catalog.new_since(result, record)
    if news:
        print("\n== New since last time ==")
        for line in news:
            print("  * " + line)
    print()


def open_target(args):
    if args.console:
        return FtpTarget(args.console, args.port)
    if args.target:
        return LocalTarget(Path(args.target))
    return engine.practice_target()


def main(argv=None) -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    parser = argparse.ArgumentParser(prog="installer", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", action="version", version=VERSION)
    sub = parser.add_subparsers(dest="action")
    ui = sub.add_parser("ui", help="open the installer window (the default)")
    ui.add_argument("--no-browser", action="store_true")
    ui.add_argument("--port", type=int, default=0)
    scan = sub.add_parser("scan", help="show the checklist for a folder")
    scan.add_argument("folder")
    scan.add_argument("--json", action="store_true")
    for name in ("install", "doctor", "restore"):
        p = sub.add_parser(name)
        if name == "install":
            p.add_argument("folder")
            p.add_argument("--icons", choices=["xbox", "default"], default="xbox")
            p.add_argument("--no-ps5x360", action="store_true")
            p.add_argument("--no-helper", action="store_true")
            p.add_argument("--download", action="store_true", help="in a practice run, really download PS5X360")
            p.add_argument("--build-editor", action="store_true", help="in a practice run, really build the editor")
            p.add_argument("--yes", action="store_true", help="do not ask before a real console install")
            p.add_argument("--json", action="store_true")
        where = p.add_mutually_exclusive_group()
        where.add_argument("--practice", action="store_true", help="use the practice folder (default)")
        where.add_argument("--console", metavar="IP", help="the PS5's address")
        p.add_argument("--target", help="practice folder to use instead of the default one")
        p.add_argument("--port", type=int, default=2121)
    args = parser.parse_args(argv)

    if args.action in (None, "ui"):
        from . import server
        return server.serve(port=getattr(args, "port", 0), browser=not getattr(args, "no_browser", False))
    try:
        if args.action == "scan":
            result = do_scan(args.folder)
            if args.json:
                print(json.dumps(result, indent=1))
            else:
                print_checklist(result)
            return 0
        with open_target(args) as target:
            if args.action == "doctor":
                report = engine.doctor(target)
                print("\n".join(report["lines"]))
                return 0 if report["ok"] else 1
            if args.action == "restore":
                report = engine.restore(target)
                print(report["summary"])
                return 0 if report["ok"] else 1
            result = do_scan(args.folder)
            if not args.json:
                print_checklist(result, engine.load_record(target))
            if not target.practice and not args.yes:
                answer = input(f"Install to the {target.label}? Type yes to go ahead: ")
                if answer.strip().lower() != "yes":
                    print("Nothing was changed.")
                    return 1
            options = dict(icons=args.icons, ps5x360=not args.no_ps5x360, companion=not args.no_helper)
            if target.practice:
                options.update(allow_download=args.download, build_editor=args.build_editor)
            installer = engine.Installer(target, result, options, log=None if args.json else print)
            outcome = installer.run()
            if args.json:
                print(json.dumps(outcome, indent=1))
            else:
                print("\n" + outcome["summary"])
                print("Log: " + outcome["log_file"])
            return 0 if outcome["ok"] else 1
    except (ValueError, OSError) as error:
        print("Ex-Box Setup: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
