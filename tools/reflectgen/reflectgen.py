#!/usr/bin/env python3
"""reflectgen v1 - scans C++ classes with libclang and emits registration
code against newui::reflection (see include/newui/reflection.h).

The header parsing lives in tools/reflectgen_lib (shared with scriptgen); this file is the
reflection-specific code generator on top of it.

Usage:
    python reflectgen.py <input.h/.cpp> [more inputs...] -o <output.cpp> [-- <extra clang args>]

See README.md for setup and details on what's (not yet) supported.
"""



import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The parse half is re-exported from here as well (reflectgen.collect_class, ...) - the unit tests and
# anything that used to import this module keep working.
from reflectgen_lib.parser import *
from reflectgen_lib.parser import (
    __version__, DEFAULT_REGISTER_FUNCTION_NAME, _CPP_IDENTIFIER_RE, _COLLECTION_ELEMENT_ARG_INDEX,
    _GETTER_PREFIXES, _SETTER_PREFIXES,
)
from reflectgen_lib.driver import enclosing_namespace, expand_inputs, include_spelling, parse_headers


def thief_tag(info, m):
    return "AccessThiefTag_" + re.sub(r"\W", "_", info.name) + "_" + m.name


def emit_class_access(info):
    private_members = [m for m in (info.fields + info.delegates) if m.scope != "Scope::Public"]
    if not private_members:
        return ""
    lines = []
    # Explicit instantiation ignores access checks - see detail::AccessThief.
    for m in private_members:
        if getattr(m, "via_thief", False):
            tag = thief_tag(info, m)
            # The tag must live in the same namespace as AccessThief's friend definition.
            lines.append(f"namespace newui::reflection::detail {{ struct {tag} {{ friend constexpr auto accessThiefGet({tag}); }}; }}")
            lines.append(f"template struct newui::reflection::detail::AccessThief<newui::reflection::detail::{tag}, &{info.name}::{m.name}>;")
    lines.append(f"template<> struct newui::reflection::detail::ClassAccess<{info.name}> {{")
    for m in private_members:
        if getattr(m, "via_thief", False):
            lines.append(f"    static constexpr auto {m.name}() {{ return accessThiefGet(newui::reflection::detail::{thief_tag(info, m)}{{}}); }}")
        else:
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


# A bare &Class::getter is ambiguous whenever getter is overloaded (almost
# always a const/non-const pair, e.g. ViewStyle& View::style() vs. const
# ViewStyle& View::style() const) - there's no target type at the point of
# '&' for the compiler to pick an overload against, since
# ClassBuilder::property()'s GetterT is a deduced template parameter, not
# a fixed one. selectOverload<Signature>() (reflection.h) supplies that
# missing target type explicitly - same idiom as Qt's qOverload<>() or
# RTTR's select_overload<>() (see reflection.h's own comment on it).
def emit_property_getter_expr(info, pa):
    target = f"&{info.name}::{pa.getter_name}"
    if not pa.ambiguous:
        return target
    const_suffix = " const" if pa.getter_is_const else ""
    return f"selectOverload<{pa.getter_return_type}({info.name}::*)(){const_suffix}>({target})"


# Same reasoning as emit_property_getter_expr() above, mirrored for the
# setter - most commonly needed for the "same name, overload on arity"
# pattern (cpp_naming_conventions.md's "Overloaded" style: `int foobar()
# const` / `void foobar(int)`), where setter_name == getter_name and a
# bare &Class::setter_name would be just as ambiguous as a bare
# &Class::getter_name is in that case.
def emit_property_setter_expr(info, pa):
    target = f"&{info.name}::{pa.setter_name}"
    if not pa.setter_ambiguous:
        return target
    const_suffix = " const" if pa.setter_is_const else ""
    return f"selectOverload<void ({info.name}::*)({pa.setter_arg_type}){const_suffix}>({target})"


# Reorders `entries` (fn_name, class_name, base_name) tuples - see
# emit_register_function()'s/emit_register_enum_function()'s own comments
# on why base_name is carried alongside each fn_name - so every class's
# registration function runs after its base's, transitively (grandparent
# before parent before derived). A plain DFS post-order topo sort:
# visiting a class first visits its (reflected) base, so the base always
# lands earlier in `ordered`; everything with no base dependency (enums,
# and classes with no reflected public base) just keeps its original
# discovery-order position relative to whatever it doesn't depend on.
# base_name only ever names a *public* base that was itself reflected in
# this same run (see choose_base()'s own comment) - a base outside that
# set, or a non-public-only base (".derived(true)", no base<BaseT>() call
# at all), was never a real ClassBuilder<T>::base<BaseT>() dependency to
# order for in the first place, so `by_class.get()` below simply won't
# find it and visit() falls through with no base to recurse into.
def order_registration_calls(entries):
    by_class = {
        class_name: (fn_name, base_name)
        for fn_name, class_name, base_name in entries
        if class_name is not None
    }

    ordered = []
    state = {}  # fn_name -> 0 while visiting (cycle guard), 1 once emitted

    def visit(fn_name, base_name):
        if state.get(fn_name) is not None:
            return
        state[fn_name] = 0
        if base_name is not None and base_name in by_class:
            base_fn_name, base_base_name = by_class[base_name]
            visit(base_fn_name, base_base_name)
        state[fn_name] = 1
        ordered.append(fn_name)

    for fn_name, _class_name, base_name in entries:
        visit(fn_name, base_name)

    return ordered


def emit_register_function(info,function_listing):
    fn_name = f"register_{info.name.replace('::', '')}Reflection"
    lines = [f"void {fn_name}() {{"]
    lines.append(f"    ClassBuilder<{info.name}> builder;")
    lines.append("")
    lines.append("    builder.clazz()")

    chain = []

    # First in the chain, same position examples/reflection1.cpp's own
    # hand-written registerSuperWidgetReflection() uses - base<BaseT>()
    # requires BaseT to already be registered in ReflectionRegistry (see
    # its doc comment in reflection.h), which this generated function
    # itself doesn't (can't, in general) enforce; that's the caller's
    # responsibility, same as for hand-written registration.
    base_name = choose_base(info)
    if base_name:
        chain.append(f".base<{base_name}>()")
    elif info.bases:
        # Has a real C++ base, but not a (single) public one base<BaseT>()
        # could link to - still record isDerived() for introspection, same
        # as a hand-written registration function would via derived(true).
        chain.append(".derived(true)")

    if info.tags:
        tags_expr = "{" + ", ".join(f'"{t}"' for t in info.tags) + "}"
        chain.append(f".tags({tags_expr})")

    if info.categories:
        categories_expr = "{" + ", ".join(f'"{c}"' for c in info.categories) + "}"
        chain.append(f".categories({categories_expr})")

    if info.proxy:
        chain.append(f'.proxy("{info.proxy}")')
    if info.proxy_for:
        chain.append(f'.proxyFor("{info.proxy_for}")')
    if info.header:
        chain.append(f'.header("{info.header}")')

    if info.string_value:
        chain.append(".stringValue()")

    # A plain member variable with no accessor methods at all - static or
    # not - is a Field, never a Property (see Field's own "raw access,
    # never through a method" comment in reflection.h) - `&Class::name`
    # already resolves to the right shape either way (a plain ValueT* for
    # a static VAR_DECL, a ValueT T::* pointer-to-member for a non-static
    # FIELD_DECL - see ClassBuilder::field()'s own overloads), so static-
    # ness doesn't change which method reflectgen emits here at all
    # anymore, only is_static's value (kept on Field for anyone reading
    # collect_class()'s output, unused in codegen past this point).
    for f in info.fields:
        if f.defVal is None:
            if f.scope == "Scope::Public":
                chain.append(f'.field("{f.name}", {f.scope}, &{info.name}::{f.name})')
            else:
                chain.append(f'.field("{f.name}", {f.scope}, detail::ClassAccess<{info.name}>::{f.name}())')
        else:
            if f.scope == "Scope::Public":
                chain.append(f'.field("{f.name}", {f.scope}, &{info.name}::{f.name})')
            else:
                chain.append(f'.field("{f.name}", {f.scope}, detail::ClassAccess<{info.name}>::{f.name}())')
        if f.connect:
            chain.append(".connect()")

    for d in info.delegates:
        delegate_ref = f"&{info.name}::{d.name}" if d.scope == "Scope::Public" else f"detail::ClassAccess<{info.name}>::{d.name}()"
        if d.tags:
            tags_expr = "{" + ", ".join(f'"{t}"' for t in d.tags) + "}"
            chain.append(f'.delegate("{d.name}", {d.scope}, {delegate_ref}, {tags_expr})')
        else:
            chain.append(f'.delegate("{d.name}", {d.scope}, {delegate_ref})')

    for pa in info.property_accessors:
        getter_expr = emit_property_getter_expr(info, pa)
        # <false> forces reflection.h's ClassBuilder::property()/
        # TypedProperty AssumeCopyable override off - see PropertyAccessor.
        # assume_copyable's own comment for when/why.
        property_call = ".property" if pa.assume_copyable else ".property<false>"
        tags_expr = None
        if pa.tags:
            tags_expr = "{" + ", ".join(f'"{t}"' for t in pa.tags) + "}"
        if pa.setter_name:
            setter_expr = emit_property_setter_expr(info, pa)
            args = [f'"{pa.key}"', pa.scope, getter_expr, setter_expr]
        else:
            # tags needs the setter slot filled explicitly to reach its
            # own trailing position - ClassBuilder::property()'s setter
            # parameter comes before tags positionally (reflection.h).
            args = [f'"{pa.key}"', pa.scope, getter_expr] + (["nullptr"] if tags_expr else [])
        if tags_expr:
            args.append(tags_expr)
        chain.append(f'{property_call}({", ".join(args)})')

    # add_name without remove_name (or vice versa) is legal - a read-only
    # add, or an enumerate-only collection with neither - so args is built
    # up incrementally rather than assuming both are always present
    # together; ClassBuilder::propertyCollection()'s own AddFnT/RemoveFnT
    # default to nullptr independently (reflection.h), which is exactly
    # what an omitted trailing argument here falls back to.
    for ca in info.collection_accessors:
        getter_expr = emit_property_getter_expr(info, ca)
        args = [f'"{ca.key}"', ca.scope, getter_expr]
        if ca.add_name:
            args.append(ca.add_expr or f"&{info.name}::{ca.add_name}")
        if ca.remove_name:
            if not ca.add_name:
                # ClassBuilder::propertyCollection()'s remove parameter
                # comes after add positionally - a remove-only collection
                # (no add_name) still has to fill that slot with an
                # explicit nullptr to reach remove.
                args.append("nullptr")
            args.append(ca.remove_expr or f"&{info.name}::{ca.remove_name}")
        chain.append(f".propertyCollection({', '.join(args)})")

    for m in info.methods:
        chain.append(f'.method("{m.name}", {m.scope}, {emit_method_expr(info, m)})')

    for c in info.ctors:
        args = ", ".join(c.arg_types)
        chain.append(f".constructor<{args}>()")

    if chain:
        for i, entry in enumerate(chain):
            terminator = ";" if i == len(chain) - 1 else ""
            lines.append(f"        {entry}{terminator}")
    else:
        # A class with no fields/properties/delegates/methods/ctors found
        # (e.g. an empty tag type, or everything private with no
        # NEWUI_REFLECT_PRIVATE) - `builder.clazz()` alone is still a
        # complete, valid statement, but needs its own terminating ';'
        # since the loop above (which normally supplies it on the *last*
        # chained call) never runs.
        lines[-1] += ";"

    lines.append("")
    lines.append("    ReflectionRegistry::registerClass(builder);")
    lines.append("}")

    # base_name (None if this class has no reflected public base) drives
    # order_registration_calls()'s dependency sort below - base<BaseT>()
    # (reflection.h) throws std::logic_error if BaseT's own registration
    # function hasn't run yet, so registerReflectionData() can't just call
    # every function in discovery order.
    function_listing.append((fn_name, info.name, base_name))

    return "\n".join(lines)


def emit_register_enum_function(info, function_listing):
    fn_name = f"register_{info.name.replace('::', '')}Enum"
    lines = [f"void {fn_name}() {{"]
    lines.append("    ReflectionRegistry::registerEnum(")
    lines.append(f'        EnumBuilder<{info.name}>("{info.bare_name}")')
    for v in info.values:
        lines.append(f'            .addValue("{v.name}", {v.value}ull)')
    if info.is_flags:
        lines.append("            .flags(true)")
    lines[-1] += "\n            .build());"
    lines.append("}")

    # No base dependency - an enum's own registration never calls base<>().
    function_listing.append((fn_name, None, None))

    return "\n".join(lines)


def generate(classes, enums, sources, extra_includes, register_function_name=DEFAULT_REGISTER_FUNCTION_NAME,
             include_dirs=()):
    for info in classes:
        info.header = include_spelling(info.header_file, include_dirs)
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

    # Every registration function lives at *global* scope, but a method's
    # return/argument type spelling (used verbatim for e.g.
    # selectOverload<...>() - see emit_property_getter_expr()/
    # emit_property_setter_expr()) comes back from clang UNqualified
    # whenever that type is visible without qualification from *where the
    # method itself is declared* - i.e. relative to the class's own
    # namespace, not global scope. `RootView &(newui::Dialog::*)()` (both
    # Dialog and RootView living in namespace newui) is exactly this - a
    # real, previously-unexercised bug (every earlier smoke-test class in
    # this session happened to be at global scope, where "unqualified" and
    # "fully qualified" are the same thing). Bringing every namespace any
    # registered class/enum actually lives in into scope here - not just
    # the fixed "newui::reflection" this always had - means those
    # unqualified spellings resolve exactly the way they did at their own
    # declaration site. Doesn't help a type from a namespace *nothing here
    # registers* (a third-party dependency's own type, say) - narrower
    # fix than a real qualified-name rewrite of every emitted type
    # spelling would be, but real and correct for the overwhelmingly
    # common case (a class referencing another type from the same
    # project), and enough to resolve every failure actually found
    # scanning newui's own real headers.
    #
    # enclosing_namespace()'s own name is a slight misnomer for a *nested*
    # enum/class (e.g. "newui::Control::StateFlags" -> "newui::Control") -
    # that's a *class*, not a namespace, and "using namespace" on a class
    # name doesn't compile (MSVC C2867) - excluded here by checking
    # against every registered class's own qualified name (the class
    # itself was necessarily also scanned as a top-level entry in
    # `classes` for this to ever come up in practice - see this file's own
    # comment on find_declarations() only recursing into NAMESPACE/
    # TRANSLATION_UNIT, so a nested enum's *outer class* was found the
    # same pass a namespace-scoped one would be). Real gap only if that
    # outer class was itself excluded from this run somehow (e.g. its own
    # "@reflect ignore=true") while one of its nested enums wasn't -
    # unusual enough not to chase further here.
    class_names = {info.name for info in classes}
    function_listing = []

    namespaces = {"newui::reflection"}
    for info in classes:
        ns = enclosing_namespace(info.name)
        if ns and ns not in class_names:
            namespaces.add(ns)
    for info in enums:
        ns = enclosing_namespace(info.name)
        if ns and ns not in class_names:
            namespaces.add(ns)
    for ns in sorted(namespaces):
        out.append(f"using namespace {ns};")
    out.append("")

    for info in classes:
        out.append(emit_class_access(info))
        out.append(emit_register_function(info, function_listing))
        out.append("")

    for info in enums:
        out.append(emit_register_enum_function(info, function_listing))
        out.append("")

    out.append("")

    out.append(f"void {register_function_name}Impl()")
    out.append("{\n")

    for funcname in order_registration_calls(function_listing):
        out.append(f"\t{funcname}();\n")

    #out.append("\n")
    #out.append(f"\tReflectionRegistry::addInitFunction(&{register_function_name});")

    out.append("}\n\n")

    # Guards against being called more than once per process -
    # ReflectionRegistry::registerClass() replaces (deletes) an already-
    # registered Class on a second call, which would dangle any const
    # Class* a caller captured from the first registration (real bug: two
    # independent call sites in cpp_codetools each called this directly,
    # and the second replaced Class objects CodeToolsVsix::
    # ToolboxRegistry::categories() had already cached pointers into). A
    # function-local static's initializer is guaranteed exactly-once and
    # thread-safe by the C++11 standard itself - no separate mutex needed,
    # unlike a hand-rolled "if (!flag) { flag = true; ... }" guard, which
    # would race under concurrent first calls.
    out.append(f"void {register_function_name}()")
    out.append("{")
    out.append("\tstatic bool registered = []() {")
    out.append(f"\t\t{register_function_name}Impl();")
    out.append("\t\treturn true;")
    out.append("\t}();")
    out.append("\t(void)registered;")
    out.append("}\n\n")
    return "\n".join(out)


# MSVC's std::string small-string-optimization capacity - a name this
# long or shorter lives entirely inside the string object itself (already
# counted in the owning struct's own sizeof() from the probe), never a
# separate heap allocation for its character buffer; longer needs one.
# Rough numbers used for that separate allocation's own cost, not
# measured per-machine the way the probe's own sizeof()s are: a typical
# small-allocation rounding granularity, and a typical per-allocation
# bookkeeping overhead on Windows' default process heap (real overhead
# varies by allocator/build - a debug heap's headers/footers/guard bytes
# run larger still) - both deliberately round, conservative-ish numbers,
# not measured facts the way everything from the probe is.
_STRING_SSO_CAPACITY = 15
_ALLOCATOR_GRANULARITY = 16
_HEAP_ALLOC_OVERHEAD = 16


def run_sizeof_probe(probe_path):
    # Parses reflectgen_sizeof_probe's own fixed "Name=bytes" stdout
    # format (tools/reflectgen/src/sizeof_probe.cpp) into the dict
    # estimate_memory_footprint() needs - real, current numbers off
    # whatever include/newui/reflection.h presently says, not anything
    # hardcoded here.
    result = subprocess.run([probe_path], capture_output=True, text=True, check=True)
    sizes = {}
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or "=" not in line:
            continue
        name, _, value = line.partition("=")
        sizes[name] = int(value)
    return sizes


def _string_extra_storage_cost(name):
    # (extra_allocations, extra_bytes) an out-of-line std::string buffer
    # costs beyond what the owning struct's own sizeof() already counts -
    # (0, 0) for a name that fits in std::string's own inline SSO buffer.
    if len(name) <= _STRING_SSO_CAPACITY:
        return 0, 0
    size = len(name) + 1  # + null terminator
    rounded = -(-size // _ALLOCATOR_GRANULARITY) * _ALLOCATOR_GRANULARITY
    return 1, rounded + _HEAP_ALLOC_OVERHEAD


# Every Field/Property/Method/Delegate/Constructor/Class/Enum instance
# reflectgen's generated code causes is a separate `new` (ClassBuilder's
# own field()/property()/method()/delegate()/constructor() all push_back a
# freshly-`new`'d object - see reflection.h) that's never freed - real,
# permanent, process-lifetime heap cost, not something a Release build
# optimizes away. This estimates that cost from what reflectgen itself
# already knows it emitted (classes/enums, each one's fields/properties/
# methods/delegates/constructors, every name's real length) combined with
# `sizes` (real, freshly-measured struct sizes from
# reflectgen_sizeof_probe - see run_sizeof_probe()'s own comment) -
# deliberately labeled an estimate, not a promise, in its own printed
# output: Windows heap-allocation bookkeeping and allocator rounding are
# round numbers, not measured; nothing here accounts for allocator
# fragmentation.
# A quick, string-only echo of reflectable_collection_element_type()'s own
# std::vector/std::array/std::map check (that function needs a live clang
# Type; by the time estimate_memory_footprint() runs, a PropertyAccessor
# only has the type's *spelling* left - qualify_type_spelling() already
# ran, so it's namespace-qualified where needed) - good enough for sizing
# purposes: a getter shaped like this is what makes ClassBuilder::
# property() auto-detect TypedPropertyCollection instead of TypedProperty
# (see _COLLECTION_ELEMENT_ARG_INDEX's own comment for the exact
# container list this mirrors, and why nothing further-removed than
# vector/array/map needs to be recognized here either).
def _looks_like_reflectable_collection(type_spelling):
    normalized = normalize_type_spelling(type_spelling)
    return any(normalized.startswith(f"std::{c}<") for c in _COLLECTION_ELEMENT_ARG_INDEX)


def estimate_memory_footprint(classes, enums, sizes):
    total_allocations = 0
    total_bytes = 0

    def add(size_key, name=None):
        nonlocal total_allocations, total_bytes
        total_allocations += 1
        total_bytes += sizes[size_key] + _HEAP_ALLOC_OVERHEAD
        if name is not None:
            extra_allocs, extra_bytes = _string_extra_storage_cost(name)
            total_allocations += extra_allocs
            total_bytes += extra_bytes

    def add_argument_buffer(arg_count):
        nonlocal total_allocations, total_bytes
        if arg_count:
            total_allocations += 1
            total_bytes += arg_count * sizes["Argument"] + _HEAP_ALLOC_OVERHEAD

    num_fields = num_properties = num_methods = num_delegates = num_ctors = 0

    for info in classes:
        add("Class", info.name.rsplit("::", 1)[-1])
        for f in info.fields:
            # A collection-shaped field (TypedFieldCollection) measures
            # identically to a plain one (TypedMemberField/TypedField) on
            # this platform - both probed separately (reflectgen_sizeof_probe's
            # own "FieldCollection"/"Field" lines) and confirmed equal
            # rather than assumed so; "Field" stands in for both without
            # needing reflectgen's own Field dataclass to track the
            # field's type just for this estimate.
            add("Field", f.name)
            num_fields += 1
        for d in info.delegates:
            add("Delegate", d.name)
            num_delegates += 1
        for pa in info.property_accessors:
            size_key = "PropertyCollection" if _looks_like_reflectable_collection(pa.getter_return_type) else "Property"
            add(size_key, pa.key)
            num_properties += 1
        for ca in info.collection_accessors:
            # Always a TypedPropertyCollection - that's the whole point of
            # @reflect collection/ClassBuilder::propertyCollection(), not
            # something detected from the getter's return type the way
            # property_accessors above is.
            add("PropertyCollection", ca.key)
            num_properties += 1
        for m in info.methods:
            add("Method", m.name)
            add_argument_buffer(len(m.arg_types))
            num_methods += 1
        for c in info.ctors:
            add("Constructor")
            add_argument_buffer(len(c.arg_types))
            num_ctors += 1

    num_enum_values = 0
    for info in enums:
        add("Enum", info.bare_name)
        if info.values:
            total_allocations += 1
            total_bytes += len(info.values) * sizes["EnumValue"] + _HEAP_ALLOC_OVERHEAD
            for v in info.values:
                extra_allocs, extra_bytes = _string_extra_storage_cost(v.name)
                total_allocations += extra_allocs
                total_bytes += extra_bytes
        num_enum_values += len(info.values)

    lines = [
        f"reflectgen: estimated reflection-registry memory footprint: "
        f"~{total_bytes / 1024:.1f} KiB across ~{total_allocations} heap allocation(s) "
        f"({len(classes)} class(es), {num_fields} field(s), {num_properties} "
        f"propert{'y' if num_properties == 1 else 'ies'}, {num_methods} method(s), "
        f"{num_delegates} delegate(s), {num_ctors} constructor(s), {len(enums)} "
        f"enum(s)/{num_enum_values} enum value(s))",
        "reflectgen: estimate only - real, current struct sizes (reflectgen_sizeof_probe) "
        "but rough heap-allocation-overhead/std::string-SSO assumptions; this is the "
        "*runtime registry's* own memory, permanent for the process lifetime (nothing "
        "here is ever freed) - it does not include the generated .cpp's own code size.",
    ]
    return "\n".join(lines)


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
    parser.add_argument("--sizeof-probe", metavar="EXE",
                         help="path to a built reflectgen_sizeof_probe executable "
                              "(tools/reflectgen/src) - when given, prints an estimated "
                              "memory footprint for the generated registry using real, "
                              "freshly-measured sizeof() values; omitted entirely otherwise")
    parser.add_argument("--register-function", default=DEFAULT_REGISTER_FUNCTION_NAME, metavar="NAME",
                         help="name of the generated master function that calls every "
                              "individual register_*Reflection()/register_*Enum() function "
                              "once, base classes before their derived classes "
                              f"(default: {DEFAULT_REGISTER_FUNCTION_NAME})")

    parser.add_argument( "-v", "--version",
                        action="version",
                        version=f"%(prog)s {__version__}" )


    args = parser.parse_args(argv)

    if not _CPP_IDENTIFIER_RE.match(args.register_function):
        sys.stderr.write(
            f"reflectgen: --register-function '{args.register_function}' isn't a valid C++ "
            "identifier\n"
        )
        sys.exit(1)

    extensions = {e.lower() if e.startswith(".") else f".{e.lower()}" for e in (args.ext or [".h"])}
    input_files = expand_inputs(args.inputs, extensions, recursive=not args.no_recursive)
    if not input_files:
        sys.stderr.write(f"reflectgen: no files found among inputs {args.inputs} (extensions: {sorted(extensions)})\n")
        sys.exit(1)

    run_start = time.perf_counter()

    parsed = parse_headers(input_files, clang_args, tool="reflectgen")
    all_classes = parsed.classes
    all_enums = parsed.enums
    valid_paths = parsed.valid_paths
    parse_seconds = parsed.parse_seconds

    if not all_classes and not all_enums:
        sys.stderr.write("reflectgen: no reflectable classes or enums found\n")

    # Everything discovered across every input file lands in this one
    # generate() call, so -o always produces a single combined output file
    # regardless of how many inputs (files or directories) were scanned.
    # The "Source:" comment lists what was passed on the command line
    # (args.inputs), not the (possibly much longer) expanded file list.
    output = generate(all_classes, all_enums, args.inputs, args.include, args.register_function, parsed.include_dirs)

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(output)

    total_seconds = time.perf_counter() - run_start

    if valid_paths:
        parse_avg = parse_seconds / len(valid_paths)
        print(f"reflectgen: parsed {len(valid_paths)} file(s) in one combined pass, "
              f"{parse_seconds:.2f}s total, {parse_avg:.4f}s avg/file")

    class_names = ", ".join(c.name for c in all_classes)
    enum_names = ", ".join(e.name for e in all_enums)
    print(f"reflectgen: wrote {args.output} in {total_seconds:.2f}s "
          f"({len(all_classes)} class(es): {class_names}; {len(all_enums)} enum(s): {enum_names})")

    if args.sizeof_probe:
        try:
            sizes = run_sizeof_probe(args.sizeof_probe)
            print(estimate_memory_footprint(all_classes, all_enums, sizes))
        except (subprocess.CalledProcessError, OSError, KeyError) as exc:
            # Not worth failing the whole generation step over - the
            # estimate is diagnostic, not load-bearing for the actual
            # generated .cpp, which is already written above by this point.
            sys.stderr.write(f"reflectgen: --sizeof-probe failed ({exc}) - skipping memory estimate\n")


if __name__ == "__main__":
    main()
