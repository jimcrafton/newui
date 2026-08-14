#!/usr/bin/env python3
"""bundlegen - scaffolds the on-disk resource-bundle layout newui::Bundle
(include/newui/bundle.h) expects to find next to an app's .exe:

    <target_dir>/
        Info.json          ("name"/"version", read by Bundle::appName()/appVersion())
        Resources/
            Fonts/
            Images/
            UIs/           (saveViewTree()/saveViewTreeToFile() output)

Usage:
    python bundlegen.py <target_dir> --name "My App" [--version 1.0.0] [--force]

target_dir is wherever you want this layout to live - point it straight at
a build output directory for quick manual testing, or at a source-tree
folder you wire into CMake via
newui_add_resources(target "<folder>/Resources") (see the top-level
CMakeLists.txt) so Resources/ gets copied next to the built .exe
automatically. Info.json itself isn't covered by newui_add_resources - it
has to end up next to the .exe some other way (a packaging step, or a
second, similar copy rule), same as this script just scaffolds it rather
than wiring that up itself.

Re-running is safe: directory creation is idempotent, and each empty
directory gets a .gitkeep placeholder (git doesn't track empty
directories, and a freshly scaffolded Fonts/Images/UIs folder has nothing
else in it yet) that's only written if not already present. Info.json is
only written if it doesn't already exist, unless --force is given - so
this won't clobber a hand-edited file by accident.
"""

import argparse
import json
import os
import sys


def ensure_dir_with_gitkeep(path: str) -> bool:
    """Creates path (and any missing parents) if needed, plus a .gitkeep
    placeholder inside it. Returns True if the directory itself was newly
    created (not just already there)."""
    created = not os.path.isdir(path)
    os.makedirs(path, exist_ok=True)

    gitkeep = os.path.join(path, ".gitkeep")
    if not os.path.isfile(gitkeep):
        with open(gitkeep, "w", encoding="utf-8"):
            pass

    return created


def write_info_json(target_dir: str, name: str, version: str, force: bool) -> bool:
    """Writes Info.json (see newui::Bundle::ensureInfoLoaded()). Returns
    True if it was written, False if left alone (already exists, no
    --force)."""
    path = os.path.join(target_dir, "Info.json")
    if os.path.isfile(path) and not force:
        return False

    info = {"name": name}
    if version:
        info["version"] = version

    with open(path, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
        f.write("\n")

    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Scaffolds a newui::Bundle resource layout (Info.json + Resources/Fonts,Images,UIs).",
    )
    parser.add_argument("target_dir", help="Directory to create the bundle layout in (see module docstring).")
    parser.add_argument("--name", required=True, help='App name written to Info.json\'s "name" field.')
    parser.add_argument("--version", default="",
        help='App version written to Info.json\'s "version" field (field is omitted entirely if not given).')
    parser.add_argument("--force", action="store_true",
        help="Overwrite an existing Info.json instead of leaving it alone.")
    args = parser.parse_args()

    resources_dir = os.path.join(args.target_dir, "Resources")
    subdirs = [os.path.join(resources_dir, name) for name in ("Fonts", "Images", "UIs")]

    for subdir in subdirs:
        created = ensure_dir_with_gitkeep(subdir)
        print(f"{'created' if created else 'exists '}  {subdir}")

    info_path = os.path.join(args.target_dir, "Info.json")
    if write_info_json(args.target_dir, args.name, args.version, args.force):
        print(f"created  {info_path}")
    else:
        print(f"skipped  {info_path} (already exists - pass --force to overwrite)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
