"""scriptcollect - reads the classes marked "// @scriptable" (and what they depend on) out of a libclang
translation unit into the small model scriptgen turns into pybind11 bindings and .pyi stubs.

A marked class exposes every public member unless the member says "// @scriptable ignore=true". What a
marked class needs to be usable from Python (its bases, and the classes and enums its signatures name)
is bound too: as an empty class ("opaque") when it is not marked itself. A member whose signature names
something that cannot be bound is skipped with a warning."""

import keyword
import os
import re
import sys

import clang.cindex as cindex
from clang.cindex import AccessSpecifier, CursorKind, TypeKind

from reflectgen_lib.parser import (
    has_default_argument, is_copy_constructible, is_deleted, marker_annotations, qualified_name,
)

MARKER = "@scriptable"

# std templates pybind11 converts (with stl.h and functional.h), and how many leading template
# arguments name types that must be bound (-1: all of them). Allocators, hashes and comparators are
# not looked at.
SUPPORTED_STD = {
    "std::basic_string": 0,
    "std::basic_string_view": 0,
    "std::vector": 1,
    "std::array": 1,
    "std::set": 1,
    "std::unordered_set": 1,
    "std::map": 2,
    "std::unordered_map": 2,
    "std::pair": 2,
    "std::tuple": -1,
    "std::variant": -1,
    "std::optional": 1,
    "std::function": 1,
    "std::shared_ptr": 1,
    "std::unique_ptr": 1,
}

BUILTIN_KINDS = {
    getattr(TypeKind, name) for name in (
        "BOOL", "CHAR_U", "UCHAR", "CHAR16", "CHAR32", "CHAR_S", "SCHAR", "WCHAR", "USHORT", "UINT", "ULONG",
        "ULONGLONG", "SHORT", "INT", "LONG", "LONGLONG", "FLOAT", "DOUBLE", "LONGDOUBLE",
    ) if hasattr(TypeKind, name)
}
CHAR_KINDS = {
    getattr(TypeKind, name) for name in ("CHAR_U", "UCHAR", "CHAR_S", "SCHAR") if hasattr(TypeKind, name)
}


def script_annotations(cursor):
    """The keys of the cursor's "// @scriptable ..." comment; {} when it has none. A bare marker gives {"scriptable": "true"}."""
    annotations = marker_annotations(cursor, MARKER)
    if not annotations and cursor.raw_comment and MARKER in cursor.raw_comment:
        annotations["scriptable"] = "true"
    return annotations


def is_set(annotations, key):
    return annotations.get(key, "").lower() == "true"


def snake_case(name):
    spaced = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    spaced = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", spaced)
    return spaced.lower()


def python_identifier(name):
    if keyword.iskeyword(name):
        return name + "_"
    return name


def spell(clang_type):
    return clang_type.get_canonical().spelling


class Ref:
    """A class or enum a signature names. holder is "shared" or "unique" when it was named inside a
    std::shared_ptr / std::unique_ptr."""

    def __init__(self, decl, holder=None):
        self.decl = decl
        self.name = qualified_name(decl)
        self.is_enum = decl.kind == CursorKind.ENUM_DECL
        self.holder = holder


def type_refs(clang_type, refs, holder=None):
    """Adds to refs what binding clang_type needs; returns None when it can be bound, otherwise why not."""
    canonical = clang_type.get_canonical()
    kind = canonical.kind

    if kind in (TypeKind.POINTER, TypeKind.LVALUEREFERENCE, TypeKind.RVALUEREFERENCE):
        pointee = canonical.get_pointee().get_canonical()
        if pointee.kind == TypeKind.POINTER:
            return "pointer to pointer"
        if pointee.kind == TypeKind.VOID:
            return "void pointer"
        if kind == TypeKind.POINTER and pointee.kind in BUILTIN_KINDS and pointee.kind not in CHAR_KINDS:
            return "pointer to a number"
        return type_refs(pointee, refs, holder)

    if kind in BUILTIN_KINDS or kind == TypeKind.VOID:
        return None

    if kind == TypeKind.ENUM:
        refs.append(Ref(canonical.get_declaration()))
        return None

    if kind == TypeKind.FUNCTIONPROTO:
        for sub in [canonical.get_result()] + list(canonical.argument_types()):
            why = type_refs(sub, refs)
            if why:
                return why
        return None

    if kind == TypeKind.RECORD:
        decl = canonical.get_declaration()
        name = qualified_name(decl)
        if name.startswith("std::"):
            if name not in SUPPORTED_STD:
                return f"unsupported {name}"
            inner_holder = {"std::shared_ptr": "shared", "std::unique_ptr": "unique"}.get(name)
            count = SUPPORTED_STD[name]
            total = canonical.get_num_template_arguments()
            if count < 0:
                count = total
            for index in range(min(count, total)):
                argument = canonical.get_template_argument_type(index)
                if argument.kind == TypeKind.INVALID:
                    continue
                why = type_refs(argument, refs, inner_holder)
                if why:
                    return why
            return None
        refs.append(Ref(decl, holder))
        return None

    return f"unsupported type '{clang_type.spelling}'"


PY_NUMBER_KINDS = {
    "int": {"USHORT", "UINT", "ULONG", "ULONGLONG", "SHORT", "INT", "LONG", "LONGLONG", "SCHAR", "UCHAR",
            "CHAR_U", "CHAR_S", "WCHAR", "CHAR16", "CHAR32"},
    "float": {"FLOAT", "DOUBLE", "LONGDOUBLE"},
}


def python_type(clang_type, name_of):
    """The Python spelling of clang_type for a .pyi stub; name_of maps a qualified C++ name to the bound Python name or None."""
    canonical = clang_type.get_canonical()
    kind = canonical.kind
    if kind == TypeKind.VOID:
        return "None"
    if kind in (TypeKind.POINTER, TypeKind.LVALUEREFERENCE, TypeKind.RVALUEREFERENCE):
        pointee = canonical.get_pointee().get_canonical()
        if kind == TypeKind.POINTER and pointee.kind in CHAR_KINDS:
            return "str"
        return python_type(pointee, name_of)
    if kind == TypeKind.BOOL:
        return "bool"
    for python_name, kinds in PY_NUMBER_KINDS.items():
        if any(getattr(TypeKind, k, None) == kind for k in kinds):
            return python_name
    if kind == TypeKind.ENUM:
        return name_of(qualified_name(canonical.get_declaration())) or "Any"
    if kind == TypeKind.FUNCTIONPROTO:
        return "Callable[..., Any]"
    if kind == TypeKind.RECORD:
        decl = canonical.get_declaration()
        name = qualified_name(decl)
        if not name.startswith("std::"):
            return name_of(name) or "Any"
        arguments = []
        for index in range(canonical.get_num_template_arguments()):
            argument = canonical.get_template_argument_type(index)
            if argument.kind != TypeKind.INVALID:
                arguments.append(argument)
        if name in ("std::basic_string", "std::basic_string_view"):
            return "str"
        if name in ("std::vector", "std::array"):
            return f"list[{python_type(arguments[0], name_of)}]" if arguments else "list"
        if name in ("std::set", "std::unordered_set"):
            return f"set[{python_type(arguments[0], name_of)}]" if arguments else "set"
        if name in ("std::map", "std::unordered_map"):
            if len(arguments) >= 2:
                return f"dict[{python_type(arguments[0], name_of)}, {python_type(arguments[1], name_of)}]"
            return "dict"
        if name in ("std::pair", "std::tuple"):
            return "tuple[" + ", ".join(python_type(a, name_of) for a in arguments[:2 if name == "std::pair" else None]) + "]"
        if name == "std::optional":
            return f"{python_type(arguments[0], name_of)} | None" if arguments else "Any"
        if name == "std::variant":
            return " | ".join(python_type(a, name_of) for a in arguments) or "Any"
        if name in ("std::shared_ptr", "std::unique_ptr"):
            return python_type(arguments[0], name_of) if arguments else "Any"
        if name == "std::function":
            return "Callable[..., Any]"
    return "Any"


class Param:
    def __init__(self, name, clang_type, has_default):
        self.name = name  # as written in C++ (used for the lambda parameters)
        self.py_name = python_identifier(snake_case(name))  # the keyword name Python callers use
        self.type = clang_type
        self.has_default = has_default

    @property
    def spelling(self):
        return spell(self.type)


class Method:
    def __init__(self, name, py_name, is_static, is_const):
        self.name = name
        self.py_name = py_name
        self.is_static = is_static
        self.is_const = is_const
        self.result = None  # clang type; None for a constructor
        self.params = []
        self.ambiguous = False
        self.release_gil = False
        self.policy = None  # explicit "policy=" annotation, a pybind11 return_value_policy name
        self.owner = None

    @property
    def default_count(self):
        count = 0
        for param in reversed(self.params):
            if not param.has_default:
                break
            count += 1
        return count


class Field:
    def __init__(self, name, py_name, clang_type, is_static, is_const):
        self.name = name
        self.py_name = py_name
        self.type = clang_type
        self.is_static = is_static
        self.is_const = is_const


class Property:
    def __init__(self, py_name, getter, setter):
        self.py_name = py_name
        self.getter = getter
        self.setter = setter


class EnumDef:
    def __init__(self, name, py_name, values, is_scoped, parent, header_file):
        self.name = name
        self.py_name = py_name
        self.values = values
        self.is_scoped = is_scoped
        self.parent = parent  # qualified name of the marked class it is nested in, or None
        self.header_file = header_file


class ClassDef:
    def __init__(self, name, py_name, header_file, marked):
        self.name = name
        self.py_name = py_name
        self.header_file = header_file
        self.marked = marked
        self.bases = []
        self.holder = None  # "shared" or "unique"
        self.holder_forced = False
        self.abstract = False
        self.ctors = []
        self.methods = []
        self.fields = []
        self.properties = []
        self.refs = []  # Refs of the members that were kept
        self.nested_enums = []


def declaration_file(cursor):
    if cursor.location.file is None:
        return ""
    return cursor.location.file.name


class Collector:
    def __init__(self, tool="scriptgen"):
        self.tool = tool
        self.warnings = 0
        self.class_cursors = {}
        self.enum_cursors = {}
        self.classes = {}
        self.enums = {}
        self.hidden = set()  # classes and enums marked "@scriptable ignore=true": never bound, and members naming them are left out silently

    def warn(self, message):
        self.warnings += 1
        sys.stderr.write(f"{self.tool}: {message}\n")

    def index_declarations(self, translation_unit_cursor):
        def visit(cursor):
            for child in cursor.get_children():
                kind = child.kind
                if kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
                    if not child.is_definition() or child.get_num_template_arguments() != -1:
                        continue
                    name = qualified_name(child)
                    if not child.spelling or "::::" in name or name.startswith("::"):
                        continue
                    self.class_cursors.setdefault(name, child)
                    if is_set(script_annotations(child), "ignore"):
                        self.hidden.add(name)
                    visit(child)
                elif kind == CursorKind.ENUM_DECL:
                    if not child.is_definition():
                        continue
                    name = qualified_name(child)
                    if not child.spelling or "::::" in name or name.startswith("::"):
                        continue
                    self.enum_cursors.setdefault(name, child)
                    if is_set(script_annotations(child), "ignore"):
                        self.hidden.add(name)
                elif kind == CursorKind.NAMESPACE:
                    if child.location.is_in_system_header:
                        continue
                    visit(child)

        visit(translation_unit_cursor)

    def collect(self, input_paths):
        """Collects every marked class found in input_paths plus what they depend on."""
        wanted = {os.path.normcase(os.path.abspath(p)) for p in input_paths}
        marked = []
        for name, cursor in self.class_cursors.items():
            file_name = declaration_file(cursor)
            if not file_name or os.path.normcase(os.path.abspath(file_name)) not in wanted:
                continue
            annotations = script_annotations(cursor)
            if not annotations or is_set(annotations, "ignore"):
                continue
            marked.append(name)

        for name in marked:
            self.build_class(name, True)

        for name, cursor in self.enum_cursors.items():
            file_name = declaration_file(cursor)
            if not file_name or os.path.normcase(os.path.abspath(file_name)) not in wanted:
                continue
            annotations = script_annotations(cursor)
            if annotations and not is_set(annotations, "ignore") and name not in self.enums:
                self.build_enum(name, None)

        # Bases and the classes and enums the kept members name. Opaque classes have no members, so
        # this reaches a fixed point after one pass over what the marked classes pulled in.
        pending = list(self.classes.values())
        while pending:
            current = pending.pop()
            names = [b for b in current.bases]
            names += [r.name for r in current.refs if not r.is_enum]
            for name in names:
                if name in self.classes:
                    continue
                if name in self.class_cursors:
                    pending.append(self.build_class(name, False))
            for ref in current.refs:
                if ref.is_enum and ref.name not in self.enums:
                    self.build_enum(ref.name, None)

        self.propagate_holders()
        return self.ordered_classes()

    def build_enum(self, name, parent):
        cursor = self.enum_cursors.get(name)
        if cursor is None:
            return None
        values = [c.spelling for c in cursor.get_children() if c.kind == CursorKind.ENUM_CONSTANT_DECL]
        py_name = script_annotations(cursor).get("name", cursor.spelling)
        enum_def = EnumDef(name, py_name, values, cursor.is_scoped_enum(), parent, declaration_file(cursor))
        self.enums[name] = enum_def
        return enum_def

    def build_class(self, name, marked):
        cursor = self.class_cursors[name]
        annotations = script_annotations(cursor)
        class_def = ClassDef(name, annotations.get("name", cursor.spelling), declaration_file(cursor), marked)
        self.classes[name] = class_def
        class_def.abstract = cursor.is_abstract_record()

        holder = annotations.get("holder")
        if holder:
            class_def.holder_forced = True
            class_def.holder = "shared" if holder.startswith("shared") else "unique"

        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_BASE_SPECIFIER:
                if child.access_specifier != AccessSpecifier.PUBLIC:
                    continue
                base_name = qualified_name(child.type.get_canonical().get_declaration())
                if base_name.startswith("std::"):
                    continue
                if base_name not in self.class_cursors:
                    self.warn(f"base class '{base_name}' of '{name}' is not in the scanned headers - left out of the binding")
                    continue
                class_def.bases.append(base_name)

        if not marked:
            return class_def

        same_name = {}
        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_METHOD:
                same_name[child.spelling] = same_name.get(child.spelling, 0) + 1

        consumed = set()
        method_cursors = {c.spelling: c for c in cursor.get_children() if c.kind == CursorKind.CXX_METHOD and same_name[c.spelling] == 1}
        for child in cursor.get_children():
            if child.kind != CursorKind.CXX_METHOD or child.access_specifier != AccessSpecifier.PUBLIC:
                continue
            member_annotations = script_annotations(child)
            property_name = member_annotations.get("property")
            if not property_name or is_set(member_annotations, "ignore"):
                continue
            if property_name == "true":
                property_name = snake_case(child.spelling)
            getter = self.build_method(class_def, child, False)
            if getter is None:
                continue
            setter = None
            setter_name = member_annotations.get("setter")
            if setter_name and setter_name in method_cursors:
                setter = self.build_method(class_def, method_cursors[setter_name], False)
                consumed.add(setter_name)
            consumed.add(child.spelling)
            class_def.properties.append(Property(property_name, getter, setter))

        for child in cursor.get_children():
            kind = child.kind
            if child.access_specifier != AccessSpecifier.PUBLIC:
                continue
            member_annotations = script_annotations(child)
            if is_set(member_annotations, "ignore"):
                continue
            if kind == CursorKind.CONSTRUCTOR:
                if child.is_copy_constructor() or child.is_move_constructor() or is_deleted(child):
                    continue
                constructor = self.build_method(class_def, child, True)
                if constructor is not None:
                    class_def.ctors.append(constructor)
            elif kind == CursorKind.CXX_METHOD:
                if child.spelling in consumed or child.spelling.startswith("operator") or is_deleted(child):
                    continue
                method = self.build_method(class_def, child, False)
                if method is not None:
                    method.ambiguous = same_name[child.spelling] > 1
                    class_def.methods.append(method)
            elif kind in (CursorKind.FIELD_DECL, CursorKind.VAR_DECL):
                field = self.build_field(class_def, child)
                if field is not None:
                    class_def.fields.append(field)
            elif kind == CursorKind.ENUM_DECL and child.is_definition():
                enum_def = self.build_enum(qualified_name(child), name)
                if enum_def is not None:
                    class_def.nested_enums.append(enum_def)

        if class_def.abstract:
            class_def.ctors = []
        elif not any(c.kind == CursorKind.CONSTRUCTOR for c in cursor.get_children()):
            class_def.ctors.append(self.default_constructor(class_def))
        self.drop_const_twins(class_def)
        return class_def

    def drop_const_twins(self, class_def):
        """Python has no const: when a method exists as both a const and a non-const overload with the same
        parameters, only the non-const one is bound."""
        keys = {}
        for method in class_def.methods:
            key = (method.py_name, method.is_static, tuple(p.spelling for p in method.params))
            keys.setdefault(key, []).append(method)
        dropped = set()
        for twins in keys.values():
            if len(twins) == 2 and twins[0].is_const != twins[1].is_const:
                dropped.add(id(twins[0] if twins[0].is_const else twins[1]))
        class_def.methods = [m for m in class_def.methods if id(m) not in dropped]

    def default_constructor(self, class_def):
        constructor = Method("", "__init__", False, False)
        constructor.owner = class_def
        return constructor

    def signature_problem(self, cursor, is_constructor, refs, scope):
        """Fills refs from the signature of cursor; returns why it cannot be bound, or None."""
        if not is_constructor:
            result = cursor.result_type.get_canonical()
            if result.kind != TypeKind.VOID:
                why = type_refs(result, refs)
                if why:
                    return why
                if result.kind == TypeKind.RECORD and not qualified_name(result.get_declaration()).startswith("std::"):
                    if not is_copy_constructible(result):
                        return f"returns '{result.spelling}' by value and it cannot be copied"
        for argument in cursor.get_arguments():
            argument_type = argument.type.get_canonical()
            if argument_type.kind == TypeKind.RVALUEREFERENCE:
                return "takes an rvalue reference"
            if argument_type.kind == TypeKind.LVALUEREFERENCE and not argument_type.get_pointee().is_const_qualified():
                pointee = argument_type.get_pointee().get_canonical()
                if pointee.kind != TypeKind.RECORD or qualified_name(pointee.get_declaration()).startswith("std::"):
                    return "takes a non-const reference (an out parameter)"
            if argument_type.kind == TypeKind.RECORD and not is_copy_constructible(argument_type):
                return f"takes '{argument_type.spelling}' by value and it cannot be copied"
            why = type_refs(argument_type, refs)
            if why:
                return why
        return None

    def build_method(self, class_def, cursor, is_constructor):
        annotations = script_annotations(cursor)
        refs = []
        why = self.signature_problem(cursor, is_constructor, refs, class_def.name)
        label = f"{class_def.name}::{cursor.spelling}"
        if any(ref.name in self.hidden for ref in refs):
            return None
        if why:
            self.warn(f"'{label}' not bound: {why}")
            return None
        unresolved = self.unresolved(refs)
        if unresolved:
            self.warn(f"'{label}' not bound: {unresolved} is not in the scanned headers")
            return None

        if is_constructor:
            py_name = "__init__"
        else:
            py_name = annotations.get("name", snake_case(cursor.spelling))
        method = Method(cursor.spelling, py_name, bool(not is_constructor and cursor.is_static_method()),
                        bool(not is_constructor and cursor.is_const_method()))
        method.owner = class_def
        if not is_constructor:
            method.result = cursor.result_type
        for index, argument in enumerate(cursor.get_arguments()):
            name = argument.spelling or f"arg{index}"
            method.params.append(Param(name, argument.type, has_default_argument(argument)))
        method.release_gil = is_set(annotations, "release_gil")
        method.policy = annotations.get("policy")
        class_def.refs.extend(refs)
        return method

    def build_field(self, class_def, cursor):
        annotations = script_annotations(cursor)
        is_static = cursor.kind == CursorKind.VAR_DECL
        field_type = cursor.type
        canonical = field_type.get_canonical()
        refs = []
        why = type_refs(canonical, refs)
        if any(ref.name in self.hidden for ref in refs):
            return None
        if not why and canonical.kind == TypeKind.RECORD:
            if not qualified_name(canonical.get_declaration()).startswith("std::") and not is_copy_constructible(canonical):
                why = "it cannot be copied"
        if not why and canonical.kind in (TypeKind.POINTER, TypeKind.LVALUEREFERENCE, TypeKind.RVALUEREFERENCE):
            why = "it is a pointer or reference"
        if not why and self.unresolved(refs):
            why = f"{self.unresolved(refs)} is not in the scanned headers"
        if why:
            self.warn(f"field '{class_def.name}::{cursor.spelling}' not bound: {why}")
            return None
        class_def.refs.extend(refs)
        return Field(cursor.spelling, annotations.get("name", snake_case(cursor.spelling)), field_type, is_static,
                     canonical.is_const_qualified())

    def unresolved(self, refs):
        for ref in refs:
            if ref.is_enum:
                if ref.name not in self.enum_cursors:
                    return ref.name
            elif ref.name not in self.class_cursors:
                return ref.name
        return None

    def propagate_holders(self):
        """A class is bound with one holder; classes related by inheritance must agree, and a class named
        inside std::shared_ptr needs the shared one."""
        for class_def in self.classes.values():
            for ref in class_def.refs:
                if ref.holder and not ref.is_enum and ref.name in self.classes:
                    target = self.classes[ref.name]
                    if not target.holder_forced and (target.holder is None or ref.holder == "shared"):
                        target.holder = ref.holder
        changed = True
        while changed:
            changed = False
            for class_def in self.classes.values():
                for base_name in class_def.bases:
                    base = self.classes[base_name]
                    if class_def.holder == "shared" and base.holder != "shared" and not base.holder_forced:
                        base.holder = "shared"
                        changed = True
                    if base.holder == "shared" and class_def.holder != "shared" and not class_def.holder_forced:
                        class_def.holder = "shared"
                        changed = True
        for class_def in self.classes.values():
            if class_def.holder is None:
                class_def.holder = "unique"

    def ordered_classes(self):
        ordered = []
        seen = set()

        def visit(class_def):
            if class_def.name in seen:
                return
            seen.add(class_def.name)
            for base_name in class_def.bases:
                visit(self.classes[base_name])
            ordered.append(class_def)

        for class_def in list(self.classes.values()):
            visit(class_def)
        return ordered

    def top_level_enums(self):
        return [e for e in self.enums.values() if e.parent is None]

    def py_name_of(self, qualified):
        if qualified in self.classes:
            return self.classes[qualified].py_name
        if qualified in self.enums:
            enum_def = self.enums[qualified]
            if enum_def.parent in self.classes:
                return f"{self.classes[enum_def.parent].py_name}.{enum_def.py_name}"
            return enum_def.py_name
        return None
