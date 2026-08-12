#!/usr/bin/env python3
"""reflectgen v1 - scans C++ classes with libclang and emits registration
code against newui::reflection (see include/newui/reflection.h).

Usage:
    python reflectgen.py <input.h/.cpp> [more inputs...] -o <output.cpp> [-- <extra clang args>]

See README.md for setup and details on what's (not yet) supported.
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
import zipfile

import clang.cindex as cindex
from clang.cindex import AccessSpecifier, CursorKind

# The `clang` PyPI package only ships bindings, not a native library - point
# it at the system LLVM install (LIBCLANG_PATH env var overrides). The
# bindings' C API is stable across libclang versions, so a newer bindings
# release against an older installed libclang.dll (as here: bindings 21.x,
# system LLVM 19.1.0) works fine.
DEFAULT_LIBCLANG_PATH = r"C:\Program Files\LLVM\bin\libclang.dll"

# Where a downloaded fallback native library (see fetch_fallback_libclang()
# below) is cached, so it's only ever downloaded once per venv. Lives next
# to .venv, not inside site-packages - installing the `libclang` PyPI
# package properly would collide with the `clang` bindings package already
# installed (both occupy the same clang/ import path), so instead this
# pulls just the native library out of libclang's wheel by hand.
FALLBACK_CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".venv", "libclang-fallback")
FALLBACK_LIB_NAME = "libclang.dll" if os.name == "nt" else "libclang.so"


def fetch_fallback_libclang():
    cached = os.path.join(FALLBACK_CACHE_DIR, FALLBACK_LIB_NAME)
    if os.path.isfile(cached):
        return cached

    sys.stderr.write(
        "reflectgen: no system LLVM/libclang found - downloading a prebuilt "
        "libclang via pip (one-time, cached under tools/reflectgen/.venv/libclang-fallback)...\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        try:
            # `pip download` (not `install`) so this never touches the
            # already-installed `clang` bindings package - it just fetches
            # the wheel for inspection. No version pin: the `libclang`
            # PyPI package's own releases lag real LLVM versions (latest
            # is 18.1.1 as of writing) - "latest compatible" in practice
            # just means "latest published", since libclang's C ABI is
            # stable enough that the exact version rarely matters (this
            # tool already runs bindings 21.x against a system libclang
            # 19.1.0 for the same reason).
            subprocess.run(
                [sys.executable, "-m", "pip", "download", "--no-deps", "-d", tmp, "libclang"],
                check=True, capture_output=True, text=True,
            )
        except subprocess.CalledProcessError as e:
            sys.stderr.write(f"reflectgen: pip download failed:\n{e.stderr}\n")
            return None
        except FileNotFoundError:
            sys.stderr.write("reflectgen: no network access to download a fallback libclang\n")
            return None

        wheels = glob.glob(os.path.join(tmp, "libclang-*.whl"))
        if not wheels:
            sys.stderr.write("reflectgen: pip download produced no wheel for libclang\n")
            return None

        with zipfile.ZipFile(wheels[0]) as zf:
            member = next((n for n in zf.namelist() if n.endswith(f"native/{FALLBACK_LIB_NAME}")), None)
            if member is None:
                sys.stderr.write(f"reflectgen: {FALLBACK_LIB_NAME} not found inside {os.path.basename(wheels[0])}\n")
                return None
            os.makedirs(FALLBACK_CACHE_DIR, exist_ok=True)
            with zf.open(member) as src, open(cached, "wb") as dst:
                dst.write(src.read())

    return cached if os.path.isfile(cached) else None


def configure_libclang():
    path = os.environ.get("LIBCLANG_PATH", DEFAULT_LIBCLANG_PATH)
    if not os.path.isfile(path):
        path = fetch_fallback_libclang()

    if path and os.path.isfile(path):
        cindex.Config.set_library_file(path)
        # Bindings (21.x) may well be newer than whatever libclang.dll
        # ends up loaded (system install, or the downloaded fallback) - a
        # handful of C API functions the bindings know about (e.g.
        # clang_getOffsetOfBase, used for base-class layout queries
        # reflectgen never calls) might not exist in an older library.
        # compatibility_check=False makes missing functions raise only if
        # actually invoked, instead of failing eagerly at Index.create().
        cindex.Config.compatibility_check = False
    else:
        sys.stderr.write(
            f"reflectgen: warning - no usable libclang found (checked '{path}', "
            "and the download fallback failed); set the LIBCLANG_PATH environment "
            "variable to its location.\n"
        )


SCOPE_NAMES = {
    AccessSpecifier.PUBLIC: "Scope::Public",
    AccessSpecifier.PROTECTED: "Scope::Protected",
    AccessSpecifier.PRIVATE: "Scope::Private",
}


def same_file(cursor_file, path):
    if cursor_file is None:
        return False
    return os.path.normcase(os.path.abspath(cursor_file.name)) == os.path.normcase(os.path.abspath(path))


def qualified_name(cursor):
    parts = []
    node = cursor
    while node is not None and node.kind != CursorKind.TRANSLATION_UNIT:
        if node.kind in (CursorKind.NAMESPACE, CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL, CursorKind.ENUM_DECL):
            parts.append(node.spelling)
        node = node.semantic_parent
    return "::".join(reversed(parts))


def is_delegate_field_type(clang_type):
    # A newui::Delegate<SenderT, Args...> member is always declared through
    # a typedef (see delegate.h's own doc comment / frame.h's
    # TitleChangedDelegate etc.) - get_canonical() resolves through that to
    # the real template specialization so this doesn't need to special-case
    # every project-specific typedef name.
    canon = clang_type.get_canonical()
    decl = canon.get_declaration()
    if decl.spelling != "Delegate":
        return False
    return qualified_name(decl) in ("newui::Delegate", "Delegate")


def is_deleted(cursor):
    tokens = [t.spelling for t in cursor.get_tokens()]
    return "delete" in tokens[-3:]


def has_reflect_friend(class_cursor):
    for child in class_cursor.get_children():
        if child.kind == CursorKind.FRIEND_DECL:
            return True
    return False


# Matches one "key=value" pair inside a "@reflect key=value[,key=value...]"
# annotation - see reflect_annotations() below.
REFLECT_ANNOTATION_PAIR_RE = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)\s*=\s*([A-Za-z_][A-Za-z_0-9]*)")


def reflect_annotations(cursor):
    # cursor.raw_comment is the comment text immediately preceding cursor
    # (line "//" or block "/* */", no doxygen markers required), or None if
    # there isn't one - which is the common case, so this has to handle
    # that before anything else touches it.
    raw_comment = cursor.raw_comment
    if not raw_comment:
        return {}

    marker = "@reflect"
    pos = raw_comment.find(marker)
    if pos < 0:
        return {}

    annotation_text = raw_comment[pos + len(marker):]
    return {m.group(1): m.group(2) for m in REFLECT_ANNOTATION_PAIR_RE.finditer(annotation_text)}


def is_reflect_ignored(cursor):
    # "@reflect ignore=true" right above a class/struct excludes it from
    # generation entirely - e.g. for a type that's hand-registered
    # elsewhere, or isn't meant to be reflectable at all.
    return reflect_annotations(cursor).get("ignore", "").lower() == "true"


class Field:
    def __init__(self, name, scope, is_static):
        self.name = name
        self.scope = scope
        self.is_static = is_static


class DelegateField:
    def __init__(self, name, scope):
        self.name = name
        self.scope = scope


class Method:
    def __init__(self, name, scope, is_const, return_type, arg_types, ambiguous):
        self.name = name
        self.scope = scope
        self.is_const = is_const
        self.return_type = return_type
        self.arg_types = arg_types
        self.ambiguous = ambiguous


class Ctor:
    def __init__(self, arg_types):
        self.arg_types = arg_types


class ClassInfo:
    def __init__(self, name, has_friend):
        self.name = name
        self.has_friend = has_friend
        self.fields = []
        self.delegates = []
        self.methods = []
        self.ctors = []


class EnumValue:
    def __init__(self, name, value):
        self.name = name
        self.value = value


class EnumInfo:
    def __init__(self, name):
        self.name = name  # fully qualified, e.g. "newui::SyncReturn::ReturnCode"
        self.values = []

    @property
    def bare_name(self):
        # Matches Class::name()'s own convention (demangleTypeName() strips
        # namespace, ClassBuilder keeps it in a separate namespaceName() -
        # Enum has no such second field, so this just keeps the registry
        # key consistent with how classes are already named/looked-up).
        return self.name.rsplit("::", 1)[-1]


def collect_class(cursor):
    name = qualified_name(cursor)
    has_friend = has_reflect_friend(cursor)
    info = ClassInfo(name, has_friend)

    method_name_counts = {}
    method_cursors = []

    for child in cursor.get_children():
        if child.kind == CursorKind.CXX_METHOD:
            if child.spelling.startswith("operator"):
                continue
            if child.is_static_method() or child.is_pure_virtual_method():
                continue
            if is_deleted(child):
                continue
            method_cursors.append(child)
            method_name_counts[child.spelling] = method_name_counts.get(child.spelling, 0) + 1

    for child in cursor.get_children():
        kind = child.kind
        access = child.access_specifier

        if kind == CursorKind.FIELD_DECL:
            if access != AccessSpecifier.PUBLIC and not info.has_friend:
                continue
            if is_delegate_field_type(child.type):
                # Never a Property, even though it's a member variable -
                # see Delegate's class comment in reflection.h.
                info.delegates.append(DelegateField(child.spelling, SCOPE_NAMES[access]))
            else:
                info.fields.append(Field(child.spelling, SCOPE_NAMES[access], is_static=False))

        elif kind == CursorKind.VAR_DECL and child.semantic_parent == cursor:
            if access == AccessSpecifier.PUBLIC or info.has_friend:
                info.fields.append(Field(child.spelling, SCOPE_NAMES[access], is_static=True))

        elif kind == CursorKind.CXX_METHOD and child in method_cursors:
            if access != AccessSpecifier.PUBLIC:
                # Only publicly-invocable methods get a real accessor in v1
                # (see Method::invoke()'s doc comment in reflection.h).
                continue
            arg_types = [a.type.spelling for a in child.get_arguments()]
            info.methods.append(Method(
                name=child.spelling,
                scope=SCOPE_NAMES[access],
                is_const=child.is_const_method(),
                return_type=child.result_type.spelling,
                arg_types=arg_types,
                ambiguous=method_name_counts[child.spelling] > 1,
            ))

        elif kind == CursorKind.CONSTRUCTOR:
            if access != AccessSpecifier.PUBLIC:
                continue
            if child.is_copy_constructor() or child.is_move_constructor():
                continue
            if is_deleted(child):
                continue
            arg_types = [a.type.spelling for a in child.get_arguments()]
            info.ctors.append(Ctor(arg_types))

    return info


def collect_enum(cursor):
    info = EnumInfo(qualified_name(cursor))
    for child in cursor.get_children():
        if child.kind == CursorKind.ENUM_CONSTANT_DECL:
            info.values.append(EnumValue(child.spelling, child.enum_value))
    return info


def find_declarations(tu, path):
    classes = []
    enums = []

    def collect_nested_enums(class_cursor):
        for child in class_cursor.get_children():
            if child.kind == CursorKind.ENUM_DECL and child.is_definition():
                # A private/protected nested enum's name isn't spellable
                # (typeid(Outer::PrivateEnum)) from the free registration
                # function reflectgen emits - only public nested enums are
                # supported in v1, same reasoning as Method skipping
                # non-public overloads.
                if child.access_specifier == AccessSpecifier.PUBLIC:
                    enums.append(collect_enum(child))

    def visit(cursor):
        for child in cursor.get_children():
            if child.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
                # get_num_template_arguments() != -1 means this is an
                # (explicit/partial) specialization of a class template -
                # e.g. a hand- or reflectgen-written
                # detail::ClassAccess<Widget> specialization sitting right
                # in the source file. That's reflection's own plumbing, not
                # a user type to register - skip it.
                if child.get_num_template_arguments() != -1:
                    continue

                # "@reflect ignore=true" in a comment directly above the
                # class/struct excludes it from generation - see
                # is_reflect_ignored()/reflect_annotations() above.
                if is_reflect_ignored(child):
                    continue

                if child.is_definition() and same_file(child.location.file, path):
                    classes.append(collect_class(child))
                    collect_nested_enums(child)
            elif child.kind == CursorKind.ENUM_DECL:
                if child.is_definition() and same_file(child.location.file, path):
                    enums.append(collect_enum(child))
            elif child.kind in (CursorKind.NAMESPACE, CursorKind.TRANSLATION_UNIT):
                visit(child)

    visit(tu.cursor)
    return classes, enums


def emit_class_access(info):
    private_members = [m for m in (info.fields + info.delegates) if m.scope != "Scope::Public"]
    if not private_members:
        return ""
    lines = [f"template<> struct newui::reflection::detail::ClassAccess<{info.name}> {{"]
    for m in private_members:
        lines.append(f"    static constexpr auto {m.name}() {{ return &{info.name}::{m.name}; }}")
    lines.append("};")
    return "\n".join(lines) + "\n\n"


def emit_method_expr(info, m):
    target = f"&{info.name}::{m.name}"
    if not m.ambiguous:
        return target
    args = ", ".join(m.arg_types)
    const_suffix = " const" if m.is_const else ""
    return f"static_cast<{m.return_type} ({info.name}::*)({args}){const_suffix}>({target})"


def emit_register_function(info):
    fn_name = f"register_{info.name.replace('::', '')}Reflection"
    lines = [f"void {fn_name}() {{"]
    lines.append(f"    ClassBuilder<{info.name}> builder;")
    lines.append("")
    lines.append("    builder.clazz()")

    chain = []
    for f in info.fields:
        if f.is_static:
            chain.append(f'.field("{f.name}", &{info.name}::{f.name})')
        elif f.scope == "Scope::Public":
            chain.append(f'.property("{f.name}", {f.scope}, &{info.name}::{f.name})')
        else:
            chain.append(f'.property("{f.name}", {f.scope}, detail::ClassAccess<{info.name}>::{f.name}())')

    for d in info.delegates:
        if d.scope == "Scope::Public":
            chain.append(f'.delegate("{d.name}", {d.scope}, &{info.name}::{d.name})')
        else:
            chain.append(f'.delegate("{d.name}", {d.scope}, detail::ClassAccess<{info.name}>::{d.name}())')

    for m in info.methods:
        chain.append(f'.method("{m.name}", {m.scope}, {emit_method_expr(info, m)})')

    for c in info.ctors:
        args = ", ".join(c.arg_types)
        chain.append(f".constructor<{args}>()")

    for i, entry in enumerate(chain):
        terminator = ";" if i == len(chain) - 1 else ""
        lines.append(f"        {entry}{terminator}")

    lines.append("")
    lines.append("    ReflectionRegistry::registerClass(builder);")
    lines.append("}")
    return "\n".join(lines)


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


def emit_register_enum_function(info):
    fn_name = f"register_{info.name.replace('::', '')}Enum"
    lines = [f"void {fn_name}() {{"]
    lines.append("    ReflectionRegistry::registerEnum(")
    lines.append(f'        EnumBuilder(typeid({info.name}), "{info.bare_name}")')
    for v in info.values:
        lines.append(f'            .addValue("{v.name}", {v.value})')
    lines[-1] += "\n            .build());"
    lines.append("}")
    return "\n".join(lines)


def generate(classes, enums, sources, extra_includes):
    out = []
    out.append("// Generated by reflectgen (tools/reflectgen) - see tools/reflectgen/README.md.")
    out.append("// Source: " + ", ".join(sources))
    out.append("//")
    out.append("// Not auto-regenerated by the build (v1) - re-run reflectgen.py by hand and")
    out.append("// diff/replace this file if the source class(es) change.")
    out.append("")
    out.append('#include "newui/reflection.h"')
    for inc in extra_includes:
        out.append(f'#include "{inc}"')
    out.append("")
    out.append("using namespace newui::reflection;")
    out.append("")

    for info in classes:
        out.append(emit_class_access(info))
        out.append(emit_register_function(info))
        out.append("")

    for info in enums:
        out.append(emit_register_enum_function(info))
        out.append("")

    return "\n".join(out)


def main():
    # Split "-- <clang args>" off manually before argparse ever sees it -
    # argparse.REMAINDER interacts badly with a preceding nargs="+"
    # positional (it can swallow -o/--output too), so this is more
    # reliable than declaring clang_args as a REMAINDER argument.
    argv = sys.argv[1:]
    if "--" in argv:
        split = argv.index("--")
        argv, clang_args = argv[:split], argv[split + 1:]
    else:
        clang_args = []

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="header/source files and/or directories to scan")
    parser.add_argument("-o", "--output", required=True, help="output .cpp path")
    parser.add_argument("--include", action="append", default=[], metavar="HEADER",
                         help="extra #include line to add to the generated output (repeatable)")
    parser.add_argument("--ext", action="append", default=[], metavar=".EXT",
                         help="file extension to scan for when an input is a directory "
                              "(repeatable; default .h)")
    parser.add_argument("--no-recursive", action="store_true",
                         help="when an input is a directory, only scan its top level "
                              "instead of walking subdirectories")
    args = parser.parse_args(argv)

    extensions = {e.lower() if e.startswith(".") else f".{e.lower()}" for e in (args.ext or [".h"])}
    input_files = expand_inputs(args.inputs, extensions, recursive=not args.no_recursive)
    if not input_files:
        sys.stderr.write(f"reflectgen: no files found among inputs {args.inputs} (extensions: {sorted(extensions)})\n")
        sys.exit(1)

    configure_libclang()

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
    if "-fparse-all-comments" not in clang_args:
        # Without this, libclang only attaches a "doxygen-style" comment
        # (///, /** */, //!) to a cursor's raw_comment - a plain "//"
        # comment (what "@reflect ignore=true" is written as, see
        # is_reflect_ignored()) is otherwise invisible to it entirely.
        clang_args = clang_args + ["-fparse-all-comments"]

    index = cindex.Index.create()
    all_classes = []
    all_enums = []

    for path in input_files:
        if not os.path.isfile(path):
            sys.stderr.write(f"reflectgen: '{path}' does not exist - skipping\n")
            continue

        try:
            tu = index.parse(path, args=clang_args, options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
        except cindex.TranslationUnitLoadError:
            # libclang couldn't produce a TU at all (as opposed to producing
            # one with diagnostics, handled below) - e.g. the file exists
            # but isn't valid source, or a clang_arg itself is malformed.
            # Report and move on to the rest of input_files instead of
            # letting a raw Python traceback abort the whole scan.
            sys.stderr.write(f"reflectgen: '{path}' could not be parsed at all - skipping\n")
            continue

        had_errors = False
        for diag in tu.diagnostics:
            if diag.severity >= diag.Error:
                had_errors = True
            sys.stderr.write(f"{diag}\n")
        if had_errors:
            sys.stderr.write(f"reflectgen: '{path}' had parse errors - output may be incomplete or wrong\n")

        classes, enums = find_declarations(tu, path)
        all_classes.extend(classes)
        all_enums.extend(enums)

    if not all_classes and not all_enums:
        sys.stderr.write("reflectgen: no reflectable classes or enums found\n")

    # Everything discovered across every input file lands in this one
    # generate() call, so -o always produces a single combined output file
    # regardless of how many inputs (files or directories) were scanned.
    # The "Source:" comment lists what was passed on the command line
    # (args.inputs), not the (possibly much longer) expanded file list.
    output = generate(all_classes, all_enums, args.inputs, args.include)

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(output)

    class_names = ", ".join(c.name for c in all_classes)
    enum_names = ", ".join(e.name for e in all_enums)
    print(f"reflectgen: wrote {args.output} "
          f"({len(all_classes)} class(es): {class_names}; {len(all_enums)} enum(s): {enum_names})")


if __name__ == "__main__":
    main()
