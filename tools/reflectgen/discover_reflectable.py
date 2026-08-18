#!/usr/bin/env python3
"""discover_reflectable.py - scans candidate header files and writes the
result out as a CMake variable, so CMakeLists.txt can build reflectgen.py's
own input list without hand-maintaining it - see cmake/ReflectGen.cmake
for how this fits into the two-stage build integration (this script runs
at CMake *configure* time; reflectgen.py's own generation step runs at
*build* time, incrementally, off this script's output).

Default mode scans every candidate header unconditionally - reflectgen.py
no longer needs a per-getter/setter annotation to recognize a property
(see collect_property_accessors() in reflectgen.py - naming-convention
heuristics do that now), so there's nothing left for a *file-level*
marker to gate here either; anything that genuinely shouldn't be
reflected is excluded per-*class* via reflectgen.py's own
"@reflect ignore=true", not by a human remembering to mark every file
that should participate.

--require-marker switches to the stricter, opt-in mode this script
originally shipped with: only a file containing an "@reflect" annotation
or NEWUI_REFLECT_PRIVATE() anywhere is included. This is a plain
substring scan in that mode, not a real C++ parse (unlike reflectgen.py
itself) - deliberately cheap, since its only job would be "is this file
even a candidate," not "what exactly is in it." A false positive (a file
that merely mentions the marker text without meaning it - a comment
quoting this very docstring, say) just means reflectgen.py gets pointed
at one extra file and finds nothing to generate from it; a false negative
would silently drop a real class from the build without any error at
all, which is why the substring check (when used) matches anywhere in
the file rather than a stricter, more easily wrong per-line/per-class
pattern.

Usage:
    python discover_reflectable.py <header_or_dir...> -o <output.cmake> [--var NAME] [--require-marker]
"""

import argparse
import os
import sys
import time

MARKERS = ("@reflect", "NEWUI_REFLECT_PRIVATE")

# reflection.h/reflectionio.h define the reflection system itself, not
# application classes meant to be auto-registered - every internal class
# in reflection.h is already marked "@reflect ignore=true" for exactly
# this reason (see reflection.h's own comments), but excluding both files
# by name here means that stays true even if a future internal class
# there is ever added without remembering the annotation, rather than
# relying on every one of them staying correctly ignore-annotated forever.
EXCLUDED_BASENAMES = {"reflection.h", "reflectionio.h"}


def is_reflectable(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
    except OSError:
        return False
    return any(marker in text for marker in MARKERS)


def expand_inputs(inputs, extensions):
    files = []
    for path in inputs:
        if os.path.isdir(path):
            for root, _dirs, filenames in os.walk(path):
                for filename in filenames:
                    if os.path.splitext(filename)[1].lower() in extensions:
                        files.append(os.path.join(root, filename))
        else:
            files.append(path)
    # abspath + forward slashes: CMake accepts both slash directions in a
    # string, but backslash is also CMake's own escape character - safer
    # to just never emit one, same reasoning any generated-for-CMake path
    # list should follow.
    return sorted({os.path.abspath(f).replace("\\", "/") for f in files})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="header files and/or directories to scan")
    parser.add_argument("-o", "--output", required=True, help="output .cmake path")
    parser.add_argument("--var", default="REFLECTGEN_HEADERS",
                         help="CMake variable name to set in the output file (default: REFLECTGEN_HEADERS)")
    parser.add_argument("--ext", action="append", default=[], metavar=".EXT",
                         help="file extension to scan for when an input is a directory "
                              "(repeatable; default .h)")
    parser.add_argument("--require-marker", action="store_true",
                         help="only include a file that contains an '@reflect' annotation or "
                              "NEWUI_REFLECT_PRIVATE() somewhere (the original, stricter "
                              "opt-in behavior) - default is to include every scanned file "
                              "unconditionally, see this script's own docstring")
    parser.add_argument("--report", metavar="PATH",
                         help="also write a human-readable, one-line-per-candidate scan report "
                              "here (include/exclude + why for every file considered) - default "
                              "is '<output>.report.txt' (see below); pass an empty string to skip it")
    args = parser.parse_args()

    run_start = time.perf_counter()

    extensions = {e.lower() if e.startswith(".") else f".{e.lower()}" for e in (args.ext or [".h"])}
    candidates = expand_inputs(args.inputs, extensions)

    # (path, included, reason) for every single candidate, not just the
    # ones that made it in - this is what a "0 found" run (every header
    # scanned, none reflectable) actually looked like file-by-file, since
    # that count alone doesn't say whether "no markers anywhere" or "every
    # file got excluded for some other reason" was the actual cause.
    results = []
    for path in candidates:
        if os.path.basename(path) in EXCLUDED_BASENAMES:
            results.append((path, False, "excluded - reflection system's own header"))
        elif args.require_marker and not is_reflectable(path):
            results.append((path, False, "excluded - no @reflect/NEWUI_REFLECT_PRIVATE marker found (marker-required mode)"))
        else:
            results.append((path, True, "included"))

    reflectable = [path for path, included, _reason in results if included]

    # Plain space-separated quoted arguments - CMake's set(VAR a b c)
    # already builds a semicolon-separated list internally from that; a
    # literal semicolon-joined string here would need its own escaping to
    # survive a set() call unchanged, so this is the simpler, standard way
    # to emit a generated CMake list.
    cmake_args = " ".join(f'"{f}"' for f in reflectable)

    output_dir = os.path.dirname(os.path.abspath(args.output))
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(
            "# Generated by tools/reflectgen/discover_reflectable.py - do not edit by hand.\n"
            "# Re-run automatically by CMake's configure step - see cmake/ReflectGen.cmake.\n"
            f"set({args.var} {cmake_args})\n"
        )

    total_seconds = time.perf_counter() - run_start
    avg_seconds = total_seconds / len(candidates) if candidates else 0.0
    mode = "marker-required" if args.require_marker else "scan-all"

    report_path = args.report if args.report is not None else args.output + ".report.txt"
    if report_path:
        with open(report_path, "w", encoding="utf-8") as f:
            f.write(f"discover_reflectable scan report - mode={mode}\n")
            f.write(f"{len(reflectable)} of {len(candidates)} candidate(s) included\n\n")
            for path, included, reason in results:
                status = "INCLUDED" if included else "excluded"
                f.write(f"{status:9s} {path}  ({reason})\n")

    print(f"discover_reflectable: found {len(reflectable)} reflectable header(s) "
          f"among {len(candidates)} scanned ({mode}) in {total_seconds:.2f}s total, "
          f"{avg_seconds:.4f}s avg/file -> {args.output}"
          + (f" (report: {report_path})" if report_path else ""))


if __name__ == "__main__":
    main()
