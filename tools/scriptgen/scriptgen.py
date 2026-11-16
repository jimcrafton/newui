#!/usr/bin/env python3
"""scriptgen - reads C++ classes marked "// @scriptable" with libclang and emits pybind11 bindings for
them (and a .pyi stub file), the way reflectgen emits newui::reflection registrations.

Usage:
    python scriptgen.py <headers or directories...> -o <bindings.cpp> [--stubs <module.pyi>]
        [--register-function NAME] [--marker-only] [--strict] [-- <extra clang args>]

The generated file defines `void NAME(pybind11::module_& m)`; call it from a PYBIND11_EMBEDDED_MODULE
or PYBIND11_MODULE body. See README.md.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from clang.cindex import TypeKind

from reflectgen_lib.driver import expand_inputs, include_spelling, translation_unit
from reflectgen_lib.parser import _CPP_IDENTIFIER_RE, qualified_name
from scriptcollect import Collector, MARKER, python_type, spell

DEFAULT_REGISTER_FUNCTION_NAME = "registerScripting"


def contains_marker(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            return MARKER in f.read()
    except OSError:
        return False


def is_user_record(clang_type):
    canonical = clang_type.get_canonical()
    return canonical.kind == TypeKind.RECORD and not qualified_name(canonical.get_declaration()).startswith("std::")


def policy_for(method):
    if method.policy:
        return f"py::return_value_policy::{method.policy}"
    if method.result is None:
        return None
    canonical = method.result.get_canonical()
    if canonical.kind in (TypeKind.POINTER, TypeKind.LVALUEREFERENCE) and is_user_record(canonical.get_pointee()):
        return "py::return_value_policy::reference" if method.is_static else "py::return_value_policy::reference_internal"
    return None


def extras(method, params):
    parts = [f'py::arg("{p.py_name}")' for p in params]
    if method.release_gil:
        parts.append("py::call_guard<py::gil_scoped_release>()")
    policy = policy_for(method)
    if policy:
        parts.append(policy)
    return "".join(f", {part}" for part in parts)


def method_pointer(method):
    owner = method.owner.name
    target = f"&{owner}::{method.name}"
    if not method.ambiguous:
        return target
    arguments = ", ".join(p.spelling for p in method.params)
    result = spell(method.result)
    if method.is_static:
        return f"static_cast<{result} (*)({arguments})>({target})"
    const = " const" if method.is_const else ""
    return f"static_cast<{result} ({owner}::*)({arguments}){const}>({target})"


def emit_method(method):
    """The .def(...) lines of one method: the full signature, then one lambda per shorter argument count
    that a default argument allows (so Python can leave those arguments out)."""
    lines = []
    lines.append(f'.def{"_static" if method.is_static else ""}("{method.py_name}", {method_pointer(method)}{extras(method, method.params)})')
    owner = method.owner.name
    total = len(method.params)
    for count in range(total - 1, total - method.default_count - 1, -1):
        used = method.params[:count]
        declared = ", ".join(f"{p.spelling} {p.name}" for p in used)
        passed = ", ".join(p.name for p in used)
        if method.is_static:
            body = f"[]({declared}) -> decltype(auto) {{ return {owner}::{method.name}({passed}); }}"
            lines.append(f'.def_static("{method.py_name}", {body}{extras(method, used)})')
        else:
            const = "const " if method.is_const else ""
            separator = ", " if used else ""
            body = (f"[]({const}{owner}& self_{separator}{declared}) -> decltype(auto) "
                    f"{{ return self_.{method.name}({passed}); }}")
            lines.append(f'.def("{method.py_name}", {body}{extras(method, used)})')
    return lines


def emit_constructor(constructor):
    lines = []
    total = len(constructor.params)
    for count in range(total, total - constructor.default_count - 1, -1):
        used = constructor.params[:count]
        types = ", ".join(p.spelling for p in used)
        arguments = "".join(f', py::arg("{p.py_name}")' for p in used)
        lines.append(f".def(py::init<{types}>(){arguments})")
    return lines


def emit_enum(enum_def, scope_variable):
    enclosing = enum_def.name.rsplit("::", 1)[0] if "::" in enum_def.name else ""
    lines = [f'py::enum_<{enum_def.name}>({scope_variable}, "{enum_def.py_name}")']
    for value in enum_def.values:
        if enum_def.is_scoped:
            expression = f"{enum_def.name}::{value}"
        else:
            expression = f"{enclosing}::{value}" if enclosing else f"::{value}"
        lines.append(f'    .value("{value}", {expression})')
    if not enum_def.is_scoped:
        lines.append("    .export_values()")
    lines[-1] += ";"
    return lines


def emit_class(class_def):
    out = []
    holder = ", std::shared_ptr<%s>" % class_def.name if class_def.holder == "shared" else ""
    bases = "".join(f", {b}" for b in class_def.bases)
    out.append("    {")
    out.append(f'        py::class_<{class_def.name}{bases}{holder}> cls(m, "{class_def.py_name}");')

    chain = []
    for constructor in class_def.ctors:
        if constructor.name == "":
            chain.append(".def(py::init<>())")
        else:
            chain.extend(emit_constructor(constructor))
    for method in class_def.methods:
        chain.extend(emit_method(method))
    for prop in class_def.properties:
        getter = prop.getter
        policy = policy_for(getter)
        policy_text = f", {policy}" if policy else ""
        if prop.setter:
            chain.append(f'.def_property("{prop.py_name}", &{class_def.name}::{getter.name}, &{class_def.name}::{prop.setter.name}{policy_text})')
        else:
            chain.append(f'.def_property_readonly("{prop.py_name}", &{class_def.name}::{getter.name}{policy_text})')
    for field in class_def.fields:
        suffix = "_static" if field.is_static else ""
        kind = "readonly" if field.is_const else "readwrite"
        chain.append(f'.def_{kind}{suffix}("{field.py_name}", &{class_def.name}::{field.name})')

    if chain:
        out.append("        cls")
        for index, entry in enumerate(chain):
            terminator = ";" if index == len(chain) - 1 else ""
            out.append(f"            {entry}{terminator}")
    for enum_def in class_def.nested_enums:
        for line in emit_enum(enum_def, "cls"):
            out.append(f"        {line}")
    out.append("    }")
    return out


def header_include(path, include_dirs):
    spelled = include_spelling(path, include_dirs)
    if spelled:
        return f"#include {spelled}"
    return f'#include "{path.replace(os.sep, "/")}"'


def generate_cpp(collector, classes, sources, extra_includes, function_name, include_dirs):
    out = []
    out.append("// Generated by scriptgen (tools/scriptgen) - see tools/scriptgen/README.md.")
    out.append("// Source: " + ", ".join(sources))
    out.append("")
    out.append("#include <pybind11/pybind11.h>")
    out.append("#include <pybind11/stl.h>")
    out.append("#include <pybind11/functional.h>")
    for include in extra_includes:
        out.append(f'#include "{include}"')
    headers = []
    for class_def in classes:
        if class_def.header_file and class_def.header_file not in headers:
            headers.append(class_def.header_file)
    for enum_def in collector.enums.values():
        if enum_def.header_file and enum_def.header_file not in headers:
            headers.append(enum_def.header_file)
    for header in headers:
        out.append(header_include(header, include_dirs))
    out.append("")
    out.append("namespace py = pybind11;")
    out.append("")
    out.append(f"void {function_name}(py::module_& m)")
    out.append("{")
    for enum_def in collector.top_level_enums():
        for line in emit_enum(enum_def, "m"):
            out.append(f"    {line}")
        out.append("")
    for class_def in classes:
        out.extend(emit_class(class_def))
        out.append("")
    out.append("}")
    return "\n".join(out) + "\n"


def stub_signature(method, collector):
    parts = []
    if not method.is_static:
        parts.append("self")
    for param in method.params:
        default = " = ..." if param.has_default else ""
        parts.append(f"{param.py_name}: {python_type(param.type, collector.py_name_of)}{default}")
    return ", ".join(parts)


def emit_stub_methods(class_def, collector):
    out = []
    groups = {}
    for constructor in class_def.ctors:
        groups.setdefault("__init__", []).append(constructor)
    for method in class_def.methods:
        groups.setdefault(method.py_name, []).append(method)
    for name, methods in groups.items():
        overloaded = len(methods) > 1
        for method in methods:
            if overloaded:
                out.append("    @overload")
            if method.is_static:
                out.append("    @staticmethod")
            if name == "__init__":
                if method.name == "":
                    out.append("    def __init__(self) -> None: ...")
                else:
                    out.append(f"    def __init__({stub_signature(method, collector)}) -> None: ...")
                continue
            result = python_type(method.result, collector.py_name_of)
            out.append(f"    def {name}({stub_signature(method, collector)}) -> {result}: ...")
    return out


def generate_stub(collector, classes):
    out = ["# Generated by scriptgen - do not edit.", "from __future__ import annotations",
           "from typing import Any, Callable, ClassVar, overload", ""]
    for enum_def in collector.top_level_enums():
        out.append(f"class {enum_def.py_name}:")
        for value in enum_def.values:
            out.append(f"    {value}: ClassVar[{enum_def.py_name}]")
        out.append("")
    for class_def in classes:
        bases = ", ".join(collector.classes[b].py_name for b in class_def.bases)
        out.append(f"class {class_def.py_name}({bases}):" if bases else f"class {class_def.py_name}:")
        body = []
        for enum_def in class_def.nested_enums:
            body.append(f"    class {enum_def.py_name}:")
            for value in enum_def.values:
                body.append(f"        {value}: ClassVar[{class_def.py_name}.{enum_def.py_name}]")
        for field in class_def.fields:
            annotation = python_type(field.type, collector.py_name_of)
            body.append(f"    {field.py_name}: ClassVar[{annotation}]" if field.is_static else f"    {field.py_name}: {annotation}")
        for prop in class_def.properties:
            body.append("    @property")
            body.append(f"    def {prop.py_name}(self) -> {python_type(prop.getter.result, collector.py_name_of)}: ...")
            if prop.setter:
                body.append(f"    @{prop.py_name}.setter")
                body.append(f"    def {prop.py_name}(self, value: {python_type(prop.setter.params[0].type, collector.py_name_of)}) -> None: ...")
        body.extend(emit_stub_methods(class_def, collector))
        out.extend(body or ["    ..."])
        out.append("")
    return "\n".join(out) + "\n"


def main():
    argv = sys.argv[1:]
    if "--" in argv:
        split = argv.index("--")
        argv, clang_args = argv[:split], argv[split + 1:]
    else:
        clang_args = []

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="header files and/or directories to scan")
    parser.add_argument("-o", "--output", required=True, help="output .cpp path")
    parser.add_argument("--stubs", metavar="FILE", help="also write a .pyi stub file")
    parser.add_argument("--include", action="append", default=[], metavar="HEADER",
                        help="extra #include line to add to the generated output (repeatable)")
    parser.add_argument("--prelude", action="append", default=[], metavar="HEADER",
                        help="header #included before the inputs when parsing (repeatable)")
    parser.add_argument("--ext", action="append", default=[], metavar=".EXT",
                        help="file extension to scan for when an input is a directory (repeatable; default .h)")
    parser.add_argument("--no-recursive", action="store_true", help="do not walk subdirectories of a directory input")
    parser.add_argument("--marker-only", action="store_true",
                        help=f"only read the input files that contain '{MARKER}' (the others are still seen through includes)")
    parser.add_argument("--strict", action="store_true", help="a warning makes the run fail")
    parser.add_argument("--register-function", default=DEFAULT_REGISTER_FUNCTION_NAME, metavar="NAME",
                        help=f"name of the generated function (default: {DEFAULT_REGISTER_FUNCTION_NAME})")
    args = parser.parse_args(argv)

    if not _CPP_IDENTIFIER_RE.match(args.register_function):
        sys.stderr.write(f"scriptgen: --register-function '{args.register_function}' isn't a valid C++ identifier\n")
        sys.exit(1)

    extensions = {e.lower() if e.startswith(".") else f".{e.lower()}" for e in (args.ext or [".h"])}
    input_files = expand_inputs([os.path.abspath(p) for p in args.inputs], extensions, recursive=not args.no_recursive)
    if args.marker_only:
        input_files = [p for p in input_files if contains_marker(p)]

    run_start = time.perf_counter()
    for path in (args.output, args.stubs):
        if path:
            os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)

    if not input_files:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write("// Generated by scriptgen - no headers with " + MARKER + " found.\n"
                    "#include <pybind11/pybind11.h>\n"
                    f"void {args.register_function}(pybind11::module_&) {{}}\n")
        if args.stubs:
            with open(args.stubs, "w", encoding="utf-8") as f:
                f.write("# Generated by scriptgen - nothing to bind.\n")
        print(f"scriptgen: no input headers - wrote an empty {args.output}")
        return

    collector = Collector()
    with translation_unit(input_files, clang_args, tool="scriptgen", prelude_includes=tuple(args.prelude)) as unit:
        collector.index_declarations(unit.tu.cursor)
        classes = collector.collect(unit.valid_paths)
        output = generate_cpp(collector, classes, args.inputs, args.include, args.register_function, unit.include_dirs)
        stub = generate_stub(collector, classes) if args.stubs else None

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(output)
    if stub is not None:
        with open(args.stubs, "w", encoding="utf-8") as f:
            f.write(stub)

    marked = [c.name for c in classes if c.marked]
    opaque = [c.name for c in classes if not c.marked]
    print(f"scriptgen: wrote {args.output} in {time.perf_counter() - run_start:.2f}s "
          f"({len(marked)} class(es): {', '.join(marked)}; {len(opaque)} opaque: {', '.join(opaque)}; "
          f"{len(collector.enums)} enum(s))")
    if args.strict and collector.warnings:
        sys.stderr.write(f"scriptgen: {collector.warnings} warning(s) and --strict is on\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
