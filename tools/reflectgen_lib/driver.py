"""The parts of a generator run that are the same for every tool built on reflectgen_lib (reflectgen,
scriptgen): turning command line inputs into header files, normalizing the clang arguments, and
parsing every header once into ClassInfo/EnumInfo objects."""

import contextlib
import os
import sys
import tempfile
import time

import clang.cindex as cindex

from reflectgen_lib.parser import configure_libclang, find_declarations


def expand_inputs(inputs, extensions, recursive):
    files = []
    for path in inputs:
        if os.path.isdir(path):
            if recursive:
                for root, _dirs, filenames in os.walk(path):
                    for filename in filenames:
                        if os.path.splitext(filename)[1].lower() in extensions:
                            files.append(os.path.join(root, filename))
            else:
                for filename in os.listdir(path):
                    full = os.path.join(path, filename)
                    if os.path.isfile(full) and os.path.splitext(filename)[1].lower() in extensions:
                        files.append(full)
        else:
            files.append(path)
    # Sorted for a deterministic, reviewable diff between runs - os.walk()'s
    # order isn't guaranteed, and a directory input can expand to many
    # files.
    return sorted(set(files))


# The namespace a fully-qualified name (info.name, e.g. "newui::gfx::Image")
# was declared in ("newui::gfx"), or None for one at global scope. Used by
# a generator to bring every involved namespace into scope.
def enclosing_namespace(qualified_name_str):
    if "::" not in qualified_name_str:
        return None
    return qualified_name_str.rsplit("::", 1)[0]


def include_spelling(header_file, include_dirs):
    """How a file includes header_file: its path relative to the deepest of include_dirs (the -I
    directories) that contains it, in angle brackets - "<newui/controls.h>". "" if it is under none of
    them (no honest spelling exists then). The path keeps its own case; only the comparison ignores it."""
    if not header_file:
        return ""
    target = os.path.abspath(header_file)
    target_key = os.path.normcase(target)
    best = None
    for directory in include_dirs:
        base_key = os.path.normcase(os.path.abspath(directory)) + os.sep
        if target_key.startswith(base_key):
            relative = target[len(base_key):]
            if best is None or len(relative) < len(best):
                best = relative
    if best is None:
        return ""
    return "<" + best.replace(os.sep, "/") + ">"


def prepare_clang_args(clang_args):
    if "-std=c++17" not in clang_args and not any(a.startswith("-std=") for a in clang_args):
        clang_args = clang_args + ["-std=c++17"]
    if "-x" not in clang_args:
        clang_args = clang_args + ["-x", "c++"]
    if not any("_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH" in a for a in clang_args):
        # This machine's MSVC STL (yvals_core.h) hard-errors under
        # Clang < 20 via a static_assert unless this is defined - our
        # system libclang is 19.1.0. See reflectgen's README for the
        # LLVM-upgrade alternative to this bypass.
        clang_args = clang_args + ["-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"]
    if not any("_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION" in a for a in clang_args):
        # Newer MSVC STLs (14.51+, __msvc_doom_core.hpp) define their
        # "doom function" as __builtin_verbose_trap whenever __clang__ is
        # set - a libclang too old to know that builtin (e.g. the PyPI
        # fallback) then fails to parse <xmemory>/<xcall_once.h>. We only
        # parse, never compile, so route it through abort() instead.
        clang_args = clang_args + ["-D_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION"]
    if "-fparse-all-comments" not in clang_args:
        # Without this, libclang only attaches a "doxygen-style" comment
        # (///, /** */, //!) to a cursor's raw_comment - a plain "//"
        # comment (what "@reflect ignore=true" is written as, see
        # is_reflect_ignored()) is otherwise invisible to it entirely.
        clang_args = clang_args + ["-fparse-all-comments"]
    return clang_args


class ParseResult:
    def __init__(self, classes, enums, valid_paths, parse_seconds, clang_args):
        self.classes = classes
        self.enums = enums
        self.valid_paths = valid_paths
        self.parse_seconds = parse_seconds
        self.clang_args = clang_args

    @property
    def include_dirs(self):
        return [a[2:] for a in self.clang_args if a.startswith("-I") and len(a) > 2]


class ParsedUnit:
    def __init__(self, tu, valid_paths, parse_seconds, clang_args):
        self.tu = tu
        self.valid_paths = valid_paths
        self.parse_seconds = parse_seconds
        self.clang_args = clang_args

    @property
    def include_dirs(self):
        return [a[2:] for a in self.clang_args if a.startswith("-I") and len(a) > 2]


def parse_headers(input_files, clang_args, tool="reflectgen", prelude_includes=("newui/newui.h",)):
    """Parses every input file in one combined translation unit and returns a ParseResult (exits the
    process on a parse error, like the tools always did). prelude_includes are #included first, so
    every input sees the same types a real translation unit would."""
    all_classes = []
    all_enums = []
    with translation_unit(input_files, clang_args, tool, prelude_includes) as unit:
        declarations_by_path = find_declarations(unit.tu, unit.valid_paths)
        for path in unit.valid_paths:
            classes, enums = declarations_by_path[path]
            all_classes.extend(classes)
            all_enums.extend(enums)
    return ParseResult(all_classes, all_enums, unit.valid_paths, unit.parse_seconds, unit.clang_args)


@contextlib.contextmanager
def translation_unit(input_files, clang_args, tool="reflectgen", prelude_includes=("newui/newui.h",)):
    """Parses every input file in one combined translation unit and yields a ParsedUnit while the
    temporary prelude still exists (exits the process on a parse error). For a tool that walks the
    libclang AST itself instead of going through find_declarations()."""
    configure_libclang()
    clang_args = prepare_clang_args(clang_args)

    index = cindex.Index.create()

    valid_paths = []
    for path in input_files:
        if not os.path.isfile(path):
            sys.stderr.write(f"{tool}: '{path}' does not exist - skipping\n")
            continue
        valid_paths.append(path)

    # One combined synthetic translation unit - the prelude includes plus
    # every input file, in one temp .cpp - parsed ONCE, not once per
    # file. Real per-file parsing (this function's original design) fails
    # for a real fraction of newui's own headers: a header using a type
    # declared in a *different* newui header (RootView, Rect, Orientation,
    # Anchor, ... all showed up as "undeclared identifier" this way) relies
    # - same as any ordinary .cpp compiling it normally would - on
    # something else having already #include'd that other header first;
    # nothing is "wrong" with those headers for real compilation, only for
    # being parsed as their own standalone translation unit the way this
    # function used to. Combining everything into one prelude gives every
    # type the same visibility a real translation unit already has -
    # find_declarations()'s own per-declaration file check still correctly
    # attributes each declaration back to its own real source file
    # afterward, so per-file output is unaffected, only *how* parsing gets
    # there. (Also meaningfully faster in practice - libclang parses the
    # shared standard/Windows-header preamble once instead of once per
    # input file.)
    tmp_dir = tempfile.mkdtemp(prefix=f"{tool}_")
    prelude_path = os.path.join(tmp_dir, f"{tool}_prelude.cpp")
    parse_seconds = 0.0
    try:
        with open(prelude_path, "w", encoding="utf-8") as f:
            for include in prelude_includes:
                f.write(f'#include "{include}"\n')
            for path in valid_paths:
                f.write(f'#include "{path.replace(os.sep, "/")}"\n')

        parse_start = time.perf_counter()
        try:
            tu = index.parse(prelude_path, args=clang_args, options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
        except cindex.TranslationUnitLoadError:
            # libclang couldn't produce a TU at all (as opposed to producing
            # one with diagnostics, handled below) - a clang_arg itself is
            # malformed, or something equally fatal to the whole batch (not
            # just one input file, now that everything's parsed together).
            sys.stderr.write(f"{tool}: combined scan could not be parsed at all - aborting\n")
            sys.exit(1)
        parse_seconds = time.perf_counter() - parse_start

        had_errors = False
        for diag in tu.diagnostics:
            if diag.severity >= diag.Error:
                had_errors = True
            sys.stderr.write(f"{diag}\n")
        if had_errors:
            # A real parse error means find_declarations() below is working
            # from an incomplete/wrong AST for every input file, not just
            # the one the diagnostic points at (everything shares the one
            # combined prelude TU) - continuing on to emit a .cpp from that
            # produces confusing, unrelated-looking C2xxx errors several
            # steps removed from the actual cause (a missing include, a bad
            # --clang-arg, ...) once the *generated* file fails to compile.
            # Failing here instead, loudly and at the source, is strictly
            # more useful than a silent "may be incomplete or wrong" that
            # still wrote output and exited 0.
            sys.stderr.write(
                f"{tool}: combined scan had parse errors (see above) - aborting, no output written\n"
            )
            sys.exit(1)

        yield ParsedUnit(tu, valid_paths, parse_seconds, clang_args)
    finally:
        try:
            os.remove(prelude_path)
            os.rmdir(tmp_dir)
        except OSError:
            pass
