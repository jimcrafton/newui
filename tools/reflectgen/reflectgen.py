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
import time
import zipfile

import clang.cindex as cindex
from clang.cindex import AccessSpecifier, CursorKind

#use cmake to set a version number here, should match what is in VERSION.state
__version__ = "0.1.0.383"

# generate()'s own default for its master "call every register_*Reflection()/
# register_*Enum() function once" function - see generate()'s own comment and
# main()'s "--register-function" option (the CLI knob that overrides this).
DEFAULT_REGISTER_FUNCTION_NAME = "registerReflectionData"

# What main()'s "--register-function" accepts - the name is emitted verbatim
# as a C++ function definition (`void {name}()`), so anything that isn't a
# valid C++ identifier would either fail to compile or (worse) compile into
# something other than what was asked for; validated once at the CLI layer
# rather than leaving a malformed name to surface as a confusing compiler
# error several steps removed from here.
_CPP_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

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


def qualify_type_spelling(clang_type):
    # clang's own type spelling names a nested class/struct/enum
    # unqualified whenever it's visible without qualification from where
    # it was used (e.g. "ReturnCode" for newui::SyncReturn::ReturnCode,
    # spelled inside SyncReturn itself) - fine in the original source, but
    # wrong once reflectgen re-emits that same spelling verbatim into a
    # `.constructor<...>()`/selectOverload<...>() template argument at
    # global scope. generate()'s "using namespace X;" fixes the sibling
    # version of this problem (a type from another *namespace*), but
    # can't help here at all - a class isn't a namespace, so
    # "using namespace newui::SyncReturn;" doesn't compile (see that
    # function's own comment on excluding class scopes for exactly this
    # reason). Re-qualifying the spelling itself, in place, is the fix
    # that works for both cases.
    decl = clang_type.get_declaration()
    if decl.kind not in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL, CursorKind.ENUM_DECL):
        return clang_type.spelling
    qualified = qualified_name(decl)
    if not qualified:
        return clang_type.spelling
    spelling = clang_type.spelling

    # clang's spelling may already carry *some* of the qualification (e.g.
    # "ThemedTabItemStyle::TabAlignment" for a nested enum, only missing
    # the outer "newui::") rather than only ever the bare name
    # ("TabAlignment") - a naive bare-name substitution would then produce
    # "ThemedTabItemStyle::newui::ThemedTabItemStyle::TabAlignment".
    # Instead, walk from the fully-qualified chain down to the bare name
    # and prepend only whatever prefix isn't already present - clang only
    # ever omits a *leading* portion that's redundant from the point of
    # use, never a scope from the middle without the tail.
    parts = qualified.split("::")
    for i in range(len(parts)):
        suffix = "::".join(parts[i:])
        pattern = rf"\b{re.escape(suffix)}\b"
        if not re.search(pattern, spelling):
            continue
        if i == 0:
            return spelling  # already fully qualified
        missing_prefix = "::".join(parts[:i]) + "::"
        return re.sub(pattern, missing_prefix + suffix, spelling, count=1)
    return spelling


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


# ClassBuilder<T>::delegate()'s templated overload (reflection.h) only
# matches `newui::Delegate<T, Args...> T::* member` - the Delegate's own
# first template argument (its "Sender") has to be the exact class T
# being registered, same as the member-pointer's class does. That's not
# always true for a field a *derived* class redeclares using a typedef
# whose Sender is hardcoded to some base class - e.g. View::
# SizeChangedDelegate is `Delegate<View, const Size&>`, and RootView
# redeclares `SizeChangedDelegate onSizeChanged;` verbatim (its own,
# separate field - not inherited, genuinely shadows View's) without ever
# re-typedef'ing SizeChangedDelegate with Sender=RootView. Emitting
# `.delegate(...)` for that field against ClassBuilder<RootView> doesn't
# compile (deduction failure: Args... can't be deduced when the fixed
# first parameter doesn't match); this predicate lets collect_class()
# fall back to plain .field() instead (see its own call site), which has
# no such constraint - ValueT there is deduced freely off the member
# pointer, independent of T.
def delegate_sender_matches(clang_type, class_qualified_name):
    canon = clang_type.get_canonical()
    if canon.get_num_template_arguments() < 1:
        return False
    sender_decl = canon.get_template_argument_type(0).get_declaration()
    return qualified_name(sender_decl) == class_qualified_name


# ClassBuilder<T>::property()'s getter/setter overload (reflection.h)
# auto-detects a "reflectable collection" return type
# (detail::is_reflectable_collection_v<ValueT>) purely from ValueT itself
# and, if it matches, always instantiates TypedPropertyCollection<T,
# ElementT> - reflectgen has no say in that once it emits a bare
# `.property(...)` call for a getter shaped like one (see
# container_traits<>'s own specializations just below for exactly which
# container shapes qualify: std::vector<T>/std::array<T,N> at template
# arg 0, std::map<K,V,...> at arg 1 - nothing else). Mirrors
# reflection.h's own set of specializations one-for-one rather than
# trying to detect "anything container-shaped" generically - a
# specialization reflection.h doesn't have wouldn't compile as a
# TypedPropertyCollection anyway, so getting this list out of sync would
# just mean a missed case, not a wrong one.
_COLLECTION_ELEMENT_ARG_INDEX = {
    "std::vector": 0,
    "std::array": 0,
    "std::map": 1,
}


def reflectable_collection_element_type(clang_type):
    canon = clang_type.get_canonical()
    if canon.kind in (cindex.TypeKind.LVALUEREFERENCE, cindex.TypeKind.RVALUEREFERENCE):
        canon = canon.get_pointee().get_canonical()
    decl = canon.get_declaration()
    idx = _COLLECTION_ELEMENT_ARG_INDEX.get(qualified_name(decl))
    if idx is None or canon.get_num_template_arguments() <= idx:
        return None
    return canon.get_template_argument_type(idx)


def is_deleted(cursor):
    tokens = [t.spelling for t in cursor.get_tokens()]
    return "delete" in tokens[-3:]


# TypedMethod<T, RetT, Args...>::invoke() and TypedConstructor<T,
# Args...>::invoke() (reflection.h) both unpack every argument via
# `std::any_cast<Args>(args.at(I))` against a `const std::vector<std::any>&`
# - which only ever works for:
#   - a const-reference parameter (any_cast<const X&> binds directly to
#     the any's held object - always fine).
#   - a *by-value* parameter, but only when X is copy-constructible
#     (any_cast<X> has to copy the held object out) - fails hard
#     (static_assert C2338, then a cascade of C2440s) for a move-only
#     type, e.g. `void View::setCursor(Cursor)` (Cursor(const Cursor&) =
#     delete, cursor.h) - same is_copy_constructible() check already used
#     for a by-value *setter* pairing, just needed again here since a
#     bare .method() reaches the identical any_cast, not just .property().
#   - never a non-const reference (no live storage for any_cast to bind
#     one to - e.g. ThemedMenuBarItemStyle::paint(BLContext&, ..., Rect&),
#     viewstyle.h) or an rvalue reference (same reasoning).
# A codegen-side heuristic fix can't paper over any of these - only
# skipping registration can (still a real, directly-callable C++ method/
# constructor, just not through reflection - same fallback used
# throughout this file for every other shape ClassBuilder's templates
# don't support).
def invoke_arg_type_unsupported(t):
    if t.kind == cindex.TypeKind.LVALUEREFERENCE:
        return not t.get_pointee().is_const_qualified()
    if t.kind == cindex.TypeKind.RVALUEREFERENCE:
        return True
    return not is_copy_constructible(t)


def has_unsupported_invoke_arg(cursor):
    return any(invoke_arg_type_unsupported(arg.type) for arg in cursor.get_arguments())


# TypedMethod<T, RetT, Args...>::invokeImpl()'s non-void branch (reflection.h)
# unconditionally does `std::any((self->*fn_)(...))` for its return value -
# no `if constexpr(is_copy_constructible_v<RetT>)` guard the way
# TypedProperty's own get()/set() have (see the getter-addressability
# comment in collect_property_accessors() for the property side of this
# same distinction). A by-value RetT is always fine regardless (the
# function call is a prvalue; C++17 mandates it's constructed directly
# into the std::any's storage, no extra copy/move needed at all) and a
# pointer RetT is always fine too (std::any(ptr) just copies the pointer
# itself) - only a *reference*-returning method (T&/const T&) whose T
# isn't copy-constructible is a real problem: converting that live
# reference into the std::any's own storage is a genuine, non-elidable
# copy. Verified in practice: View::cursor() (returns `Cursor&`/`const
# Cursor&`, cursor.h) got excluded from Property registration by an
# unrelated ambiguity and fell back to plain .method() registration,
# where this exact case isn't guarded.
def method_return_type_unsupported(clang_type):
    if clang_type.kind not in (cindex.TypeKind.LVALUEREFERENCE, cindex.TypeKind.RVALUEREFERENCE):
        return False
    return not is_copy_constructible(clang_type.get_pointee())


# TypedDelegate<T, Args...>::invoke() (reflection.h) unpacks its Args the
# same std::any_cast<Args>(args.at(I))... way TypedMethod/TypedConstructor
# do (see has_unsupported_invoke_arg()'s own comment for the full
# reasoning) - just reached from a Delegate<Sender, Args...> field's own
# template arguments (index 0 is always Sender, never one of the
# invoke-time Args - see is_delegate_field_type()) rather than a cursor's
# get_arguments(), e.g. RunLoop's `Delegate<RunLoop, bool&> onIdle;`
# (runloop.h - a real "should I keep going" out-parameter) hits the exact
# same non-const-reference case a method parameter would.
def delegate_args_unsupported(clang_type):
    canon = clang_type.get_canonical()
    return any(
        invoke_arg_type_unsupported(canon.get_template_argument_type(i))
        for i in range(1, canon.get_num_template_arguments())
    )


# True unless `clang_type` is a class/struct that can't be copy-
# constructed - either directly (e.g. Cursor(const Cursor&) = delete;,
# cursor.h) or *implicitly*, the ordinary C++ way: a class with no
# user-declared copy constructor at all still doesn't get one if any of
# its own members/bases aren't copy-constructible (e.g. MenuItem,
# menus.h, never declares a copy constructor itself, but holds a
# `std::vector<std::unique_ptr<MenuItem>> children_` - MenuItem is just
# as uncopyable as unique_ptr is, only implicitly). Verified in practice:
# MenuItem::root() (a bare `MenuItem&` getter, ContextMenu) got
# auto-detected as a Property and its RefGetter shape's get() copies the
# referent into a std::any - instantiating MenuItem's own (deleted, but
# only diagnosed once actually odr-used) implicit copy constructor,
# which cascades into copying children_'s whole vector.
#
# Class *template* instantiations (std::unique_ptr<Key>, not a concrete
# project class like Cursor or MenuItem) are a separate, also verified-
# in-practice blind spot for a direct constructor walk: clang only
# lazily instantiates a template's member declarations when something in
# the TU actually odr-uses them, so get_children() on an instantiation
# nobody's copy-constructed yet comes back with *no* CONSTRUCTOR cursors
# at all (not even an implicit one). std::unique_ptr's copy constructor
# is deleted by the language standard itself (not an implementation
# detail that could vary), so it's special-cased directly. The other
# container_traits<>-supported containers (std::vector/std::array/
# std::map - see reflectable_collection_element_type()) hit the same
# lazy-instantiation blind spot *and* would give a wrong answer even if
# they didn't: their own fields are just an implementation-detail buffer/
# pointer (always trivially copyable), unrelated to whether their
# element type actually is - so those recurse into the element type
# specifically instead of falling through to the generic member walk
# below.
def is_copy_constructible(clang_type):
    canon = clang_type.get_canonical()
    decl = canon.get_declaration()
    if decl.kind not in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
        return True
    if qualified_name(decl) == "std::unique_ptr":
        return False
    element = reflectable_collection_element_type(canon)
    if element is not None:
        return is_copy_constructible(element)
    for child in decl.get_children():
        if child.kind == CursorKind.CONSTRUCTOR and child.is_copy_constructor():
            return not is_deleted(child)
    # No user-declared copy ctor found - the implicit one (the ordinary
    # case for a ptr-to-project-class like MenuItem) is only as
    # copy-constructible as every member/base is; recursing here also
    # happens to be the right fallback for the lazy-instantiation blind
    # spot above (a member/base's own type still needs to satisfy this
    # same check, whichever branch actually answers it).
    for child in decl.get_children():
        if child.kind == CursorKind.FIELD_DECL and not is_copy_constructible(child.type):
            return False
        if child.kind == CursorKind.CXX_BASE_SPECIFIER and not is_copy_constructible(child.type):
            return False
    return True


# Was: "does this class have ANY friend declaration at all" - a real bug,
# found only once reflectgen was actually pointed at real production
# classes (scan-all mode, see cmake/ReflectGen.cmake's own HANDOFF.md
# history) that have a `friend class X` for ordinary encapsulation reasons
# completely unrelated to reflection - reflectgen treated that as "this
# class is friended for reflection" and emitted a direct
# &Class::privateMember expression that genuinely can't compile outside
# the *real* friend's context (MSVC C2248, not a false alarm).
#
# Checks for the literal NEWUI_REFLECT_PRIVATE() macro name instead, via
# the class body's own raw source text (its extent's real file offsets,
# read directly - not through clang's already-macro-expanded token
# stream, so the literal, unexpanded macro name is exactly what's there
# to find) rather than trying to structurally identify "a friend
# declaration targeting newui::reflection::detail::ClassAccess specifically"
# through the AST - substantially simpler, and avoids depending on how a
# particular libclang version exposes a templated friend declaration's
# target. Same "cheap substring check, false positive is harmless (a
# stray comment mentioning the macro name), false negative is the
# failure mode worth avoiding" reasoning discover_reflectable.py's own
# marker scan already uses. Imprecise only for a nested class whose own
# body also contains the macro - see class body extents nesting inside
# each other - accepted as a known, rare edge case rather than a real
# per-declaration source-range parser.
def has_reflect_friend(class_cursor):
    extent = class_cursor.extent
    if extent.start.file is None:
        return False
    try:
        with open(extent.start.file.name, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
    except OSError:
        return False
    return "NEWUI_REFLECT_PRIVATE" in text[extent.start.offset:extent.end.offset]


# Matches one "key" or "key=value" token inside a "@reflect key[=value]
# [key[=value]...]" annotation - see reflect_annotations() below. The
# "=value" half is optional: "@reflect property" (bare, no value at all,
# same as "@reflect ignore=true" but without spelling out "=true") is the
# documented, common-case form (README.md's own examples use it for both
# "@reflect ignore=true" and "@reflect property"/"@reflect collection") -
# a bare key's captured value is None, mapped to "true" by
# reflect_annotations() below, same as an explicit "=true" would be.
REFLECT_ANNOTATION_PAIR_RE = re.compile(
    r"([A-Za-z_][A-Za-z_0-9]*)(?:\s*=\s*([A-Za-z_][A-Za-z_0-9]*))?"
)


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
    return {
        m.group(1): (m.group(2) if m.group(2) is not None else "true")
        for m in REFLECT_ANNOTATION_PAIR_RE.finditer(annotation_text)
    }


def is_reflect_ignored(cursor):
    # "@reflect ignore=true" right above a class/struct excludes it from
    # generation entirely - e.g. for a type that's hand-registered
    # elsewhere, or isn't meant to be reflectable at all.
    return reflect_annotations(cursor).get("ignore", "").lower() == "true"


# "@reflect flags" right above an enum opts it into ObjectWriter/
# ObjectReader's "flags" treatment (reflectionio.h - a JSON5 array of
# decomposed flag names, instead of a single name) - always explicit,
# never guessed from the enum's own values/name/operators. Every shape-
# based heuristic considered (power-of-two/combinable values, an
# "operator|" overload, a "Mask"/"Flags" name suffix) either misses a
# real flags enum in this project's own real headers or - worse -
# misclassifies an ordinary sequential enum as flags-shaped (DialogResult's
# Ok=1/Cancel=2/No=4 are individually powers of two purely by coincidence
# of small sequential values, which would make Abort=5 decompose into the
# nonsensical "Ok"|"No") - same "shape alone isn't reliable signal, so
# it's opt-in" reasoning collect_collection_accessors()'s own "@reflect
# collection" already uses.
def is_reflect_flags(cursor):
    return reflect_annotations(cursor).get("flags", "").lower() == "true"


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


class BaseInfo:
    def __init__(self, name, access):
        self.name = name  # fully qualified, e.g. "newui::Widget"
        self.access = access  # AccessSpecifier - PUBLIC/PROTECTED/PRIVATE inheritance


# A property whose value is reached through an accessor method (or pair of
# them) rather than a raw data member - see collect_property_accessors()
# for how getter_name/setter_name are found: an explicit "@reflect
# property[=name]" annotation always works, but the common get/set (or
# same-name-overloaded) pair is now recognized automatically too - see
# that function's own comment for the exact heuristic and its guardrails.
class PropertyAccessor:
    def __init__(self, key, scope, getter_name, getter_return_type, getter_is_const, ambiguous,
                 setter_name, setter_arg_type=None, setter_ambiguous=False, setter_is_const=False):
        self.key = key  # the property's own name, e.g. "name", "visible", "bounds"
        self.scope = scope
        self.getter_name = getter_name  # real C++ method name, e.g. "getName", "isVisible", "style"
        self.getter_return_type = getter_return_type  # clang spelling, e.g. "ViewStyle &"
        self.getter_is_const = getter_is_const
        # True when another overload of getter_name also exists (almost
        # always a const/non-const pair, or - for the "same name, overload
        # on arity" pattern, e.g. `name() const` / `name(const string&)` -
        # the setter itself) - emit_property_getter_expr() then wraps the
        # chosen overload in selectOverload<>() instead of a bare
        # &Class::method, which is ambiguous for an overloaded name.
        self.ambiguous = ambiguous
        self.setter_name = setter_name  # real C++ method name, or None (get-only)
        self.setter_arg_type = setter_arg_type  # clang spelling of the setter's one argument, or None
        # True when setter_name has other overloads too (see `ambiguous`'s
        # own comment - same reasoning, mirrored for the setter side,
        # needed for e.g. the same-name getter/setter pattern where
        # setter_name == getter_name) - emit_property_setter_expr() wraps
        # the setter reference in selectOverload<>() when this is true.
        self.setter_ambiguous = setter_ambiguous
        self.setter_is_const = setter_is_const  # vanishingly rare, checked for symmetry with getter_is_const


# A collection reachable only through a whole-container-returning accessor
# plus (optionally) real add()/remove() methods - ClassBuilder::
# propertyCollection()'s accessor-based overload (reflection.h), e.g.
# View::childViews()/addChild()/removeChild(). See collect_collection_
# accessors() for how getter_name/add_name/remove_name are found and
# README.md's "@reflect collection" section for why this only ever
# happens when the getter is explicitly marked, same "opt-in, never
# guessed from naming conventions" reasoning as PropertyAccessor above -
# a getter/add/remove triple can't be told apart from three unrelated
# methods by shape alone.
class CollectionAccessor:
    def __init__(self, key, scope, getter_name, getter_return_type, getter_is_const, ambiguous, add_name, remove_name):
        self.key = key  # the collection's own name, e.g. "childViews"
        self.scope = scope
        self.getter_name = getter_name  # real C++ method name, e.g. "childViews"
        self.getter_return_type = getter_return_type  # clang spelling, e.g. "const std::vector<SubView *> &"
        self.getter_is_const = getter_is_const
        self.ambiguous = ambiguous  # see PropertyAccessor.ambiguous - same selectOverload<>() reasoning
        self.add_name = add_name  # real C++ method name, or None (enumerate-only, no add)
        self.remove_name = remove_name  # real C++ method name, or None (enumerate-only, no remove)


class ClassInfo:
    def __init__(self, name, has_friend):
        self.name = name
        self.has_friend = has_friend
        self.fields = []
        self.delegates = []
        self.methods = []
        self.property_accessors = []
        self.collection_accessors = []
        self.ctors = []
        self.bases = []


class EnumValue:
    def __init__(self, name, value):
        self.name = name
        self.value = value


class EnumInfo:
    def __init__(self, name, is_flags=False):
        self.name = name  # fully qualified, e.g. "newui::SyncReturn::ReturnCode"
        self.values = []
        self.is_flags = is_flags  # see is_reflect_flags()'s own comment

    @property
    def bare_name(self):
        # Matches Class::name()'s own convention (demangleTypeName() strips
        # namespace, ClassBuilder keeps it in a separate namespaceName()) -
        # this is only ever passed as EnumBuilder<T>'s own `name` argument
        # (reflectgen.py's emit_register_enum_function()), which is *not*
        # where Enum::namespaceName() comes from - EnumBuilder<T>'s
        # constructor derives that itself from typeid(T) directly (same
        # extractNamespace() ClassBuilder<T> uses), independent of this
        # string. Keeping this bare (not fully qualified) still matters:
        # it's Enum::name()'s own value, and registerEnum() indexes the
        # bare name in the registry same as it always has, alongside the
        # namespace-derived qualified name.
        return self.name.rsplit("::", 1)[-1]


def is_getter_shaped(cursor):
    # A property getter: takes nothing, returns something. Deliberately
    # not checking is_const_method() here - View::style() (mutable, no
    # const-only alternative used for reflection) is exactly the kind of
    # getter this needs to accept.
    if cursor.result_type.spelling == "void":
        return False
    return sum(1 for _ in cursor.get_arguments()) == 0


def is_setter_shaped(cursor):
    # A property setter: takes exactly the one new value, returns nothing.
    # (A fluent setter returning T& for chaining isn't recognized in v1 -
    # ClassBuilder::property()'s Setter shape is void(SourceT&,const
    # ValueT&) anyway, so a fluent one would need wrapping regardless.)
    if cursor.result_type.spelling != "void":
        return False
    return sum(1 for _ in cursor.get_arguments()) == 1


# Strips a "get"/"is"/"set" prefix only at a real word boundary right
# after it - CamelCase (next char uppercase, "getTitle") or snake_case
# (next char "_", "get_title") - never a coincidental lowercase substring
# ("isolate" is not "is" + "olate", "setup" is not "set" + "up"). Case-
# insensitive on the prefix itself (matches "GETTITLE"/"gettitle" too),
# case-preserving on the returned stem. Returns (stem, matched) - stem is
# method_name unchanged and matched is False when no real prefix boundary
# was found, so a caller can tell "no prefix" apart from "prefix stripped
# down to nothing" (the latter never happens here - a boundary needs at
# least one more character after it).
def strip_accessor_prefix(method_name, prefix):
    if not method_name.lower().startswith(prefix):
        return method_name, False
    rest = method_name[len(prefix):]
    if rest[:1] == "_" and len(rest) > 1:
        return rest[1:], True
    if rest[:1].isupper():
        return rest, True
    return method_name, False


# "has" alongside "get"/"is" - the same boolean-query convention "is"
# covers (isActive()/hasPermission() - see cpp_naming_conventions.md).
_GETTER_PREFIXES = ("get", "is", "has")
_SETTER_PREFIXES = ("set",)


# "getTitle"/"get_title"->"Title", "isVisible"->"Visible", "bounds"->
# "Bounds" (no real get/is prefix - just capitalized as-is). This is the
# shared stem used to derive a property's default key - see
# strip_accessor_prefix()'s own comment for the word-boundary rule.
def getter_stem(method_name):
    for prefix in _GETTER_PREFIXES:
        stem, matched = strip_accessor_prefix(method_name, prefix)
        if matched:
            return stem
    return method_name[0:1].upper() + method_name[1:]


def derive_property_key(method_name):
    stem = getter_stem(method_name)
    return stem[0:1].lower() + stem[1:]


# Lowercased, for *matching* different spellings of the same accessor
# against each other (getTitle()/get_title()/GETTITLE() all normalize to
# "title") - not for the property's own key, which derive_property_key()
# derives separately with casing preserved.
def accessor_stem(method_name, prefixes):
    for prefix in prefixes:
        stem, matched = strip_accessor_prefix(method_name, prefix)
        if matched:
            return stem.lower()
    return method_name.lower()


def has_accessor_prefix(method_name, prefixes):
    return any(strip_accessor_prefix(method_name, p)[1] for p in prefixes)


# Common "private backing member for a public getter" spellings - trailing
# underscore is what newui's own classes actually use (Gadget::count_,
# View::bounds_, ...); leading underscore and "m_" are common enough
# elsewhere to recognize too. Matched case-insensitively against `stem`
# (already normalized by accessor_stem()).
_BACKING_MEMBER_PATTERNS = ("{stem}", "{stem}_", "_{stem}", "m_{stem}")

# Strips const/volatile/reference/pointer qualifiers and whitespace so
# `const std::string &` and `std::string` compare equal - a heuristic
# normalization (string-level, not a real canonical-type comparison),
# proportionate to everything else this pass guesses from shape/naming
# rather than a full semantic analysis.
_TYPE_NORMALIZE_RE = re.compile(r"\b(const|volatile)\b|[&*]|\s+")


def normalize_type_spelling(spelling):
    return _TYPE_NORMALIZE_RE.sub("", spelling)


# True if `fields` (FIELD_DECL cursors, any access level - only existence
# and type matter here, not reflecting the member itself) has one whose
# name matches `stem` under a common backing-member convention and whose
# type matches getter_return_type once qualifiers are stripped - e.g.
# `int foobar() const` paired with `int foobar_;` or `int m_foobar;`.
# This is what makes a *bare*, unprefixed getter (no "get"/"is") safe to
# infer automatically without an annotation: a real storage-backed
# accessor has a real member to match against, where a computed/derived
# method (View::computeDesiredSize(), say) essentially never does -
# see collect_property_accessors()'s own comment for where this is used.
def has_matching_backing_member(stem, getter_return_type, fields):
    candidate_names = {p.format(stem=stem) for p in _BACKING_MEMBER_PATTERNS}
    normalized_return = normalize_type_spelling(getter_return_type)
    for field in fields:
        if field.spelling.lower() not in candidate_names:
            continue
        if normalize_type_spelling(field.type.spelling) == normalized_return:
            return True
    return False


# Finds every public getter-shaped method that looks like a real property
# accessor and pairs it with a same-stem setter, if one exists -
# ClassInfo.property_accessors, consumed by emit_register_function()'s
# ".property(...)" chain entries. Every cursor involved (the chosen
# overload and, if ambiguous, its const/non-const siblings, plus a paired
# setter) is added to `consumed` so collect_class()'s later .method() pass
# skips them - a getter already driving a property shouldn't also show up
# as a separately-invocable Method for the same accessor.
#
# Heuristic-driven, not annotation-gated - an explicit "@reflect
# property[=name]" still works (to force-include something the heuristic
# misses, or rename the key), but is no longer required:
#   - a "get"/"is"-prefixed getter (getTitle/get_title/isVisible/GETTITLE,
#     case- and underscore-insensitive via accessor_stem()) is always a
#     property candidate - the prefix itself is the deliberate signal a
#     human already gave it.
#   - a bare, unprefixed getter (title(), no prefix at all) is only a
#     candidate if it has a matching setter (setTitle/set_title - a real
#     pair is strong evidence on its own) OR a matching private backing
#     member of the same type (has_matching_backing_member()) - never
#     from the bare name alone, which is indistinguishable from a
#     computed/derived method (View::computeDesiredSize(), say) by shape.
#     (Verified against a real, already-existing case: View::bounds() is
#     exactly this pattern - bare getter, no setter on View itself,
#     backed by a real `Rect bounds_;` member.)
#   - "@reflect ignore=true" directly above a candidate getter opts it out
#     of the heuristic entirely (same vocabulary the class-level opt-out
#     already uses) - the escape hatch for a false positive.
#   - two distinct method *names* normalizing to the same stem (a real
#     collision, e.g. both title() and getTitle() existing) is genuinely
#     ambiguous - warned about on stderr, neither registered as a
#     property (both stay ordinary .method() entries); not the same thing
#     as one name's own const/non-const overload pair, which is handled
#     exactly as before via selectOverload<>().
#   - a "set"-prefixed method with no matching getter stem at all can't
#     become a Property at all (ClassBuilder::property() always needs a
#     getter) - warned about on stderr, left as a plain .method().
def collect_property_accessors(method_cursors_by_name, fields, consumed, class_name):
    accessors = []

    getter_names_by_stem = {}
    setter_names_by_stem = {}

    for name, cursors in method_cursors_by_name.items():
        public_cursors = [c for c in cursors if c.access_specifier == AccessSpecifier.PUBLIC]
        if not public_cursors:
            continue
        if any(is_getter_shaped(c) for c in public_cursors):
            getter_names_by_stem.setdefault(accessor_stem(name, _GETTER_PREFIXES), []).append(name)
        if any(is_setter_shaped(c) for c in public_cursors):
            setter_names_by_stem.setdefault(accessor_stem(name, _SETTER_PREFIXES), []).append(name)

    for stem, getter_names in getter_names_by_stem.items():
        if len(getter_names) > 1:
            sys.stderr.write(
                f"reflectgen: '{class_name}' has {len(getter_names)} distinct methods "
                f"({', '.join(getter_names)}) that all look like a getter for '{stem}' - "
                "ambiguous, none registered as a property (still available via .method(...)). "
                "Annotate the intended one with '@reflect property' to resolve.\n"
            )
            continue

        getter_name = getter_names[0]
        cursors = method_cursors_by_name[getter_name]
        candidates = [
            c for c in cursors
            if c.access_specifier == AccessSpecifier.PUBLIC
            and is_getter_shaped(c)
            and reflect_annotations(c).get("ignore", "").lower() != "true"
            # An explicit "@reflect collection" on this getter means
            # collect_collection_accessors() (below) owns it - without this
            # exclusion, this pass has no way to know that annotation
            # exists at all (it only ever checks for "ignore") and happily
            # registers the same getter as a plain, get-only .property()
            # too. Both ending up in the same Class's properties_ isn't
            # just redundant: Class::allProperties()'s first-name-wins
            # merge (reflection.h) keeps whichever was registered first -
            # this plain one, since collect_property_accessors() always
            # runs before collect_collection_accessors() - permanently
            # shadowing the real add()/remove()-wired collection and
            # silently no-op'ing every read() (found via a real repro:
            # View::childViews(), annotated "@reflect collection
            # add=addChild remove=removeChild", never got its children
            # reconstructed until this exclusion was added).
            and "collection" not in reflect_annotations(c)
        ]
        if not candidates:
            continue  # every overload opted out via "@reflect ignore=true", or is a "@reflect collection" instead

        # Prefer a non-const overload - needed for an addressable/mutable
        # property (e.g. View::style()); a const-only accessor still
        # works fine, it just falls through to the by-value get-only
        # shape ClassBuilder::property() picks for a const-returning
        # getter (see reflection.h's own comment on that).
        getter_cursor = next((c for c in candidates if not c.is_const_method()), candidates[0])

        # ClassBuilder<T>::property()'s TypedProperty<T, ValueT> machinery
        # (reflection.h) needs a real ValueT - a bare `void*` getter (e.g.
        # PropertyBase::source(), a type-erased handle with no reflectable
        # pointee) deduces ValueT=void, which fails to compile (can't form
        # `void&`/`const void&` - see TypedProperty's RefGetter/Setter
        # aliases). Not fixable by picking a different accessor shape here
        # - void* genuinely isn't representable as a Property - so this
        # stays a plain .method() instead, same as any other candidate
        # that doesn't pan out.
        if normalize_type_spelling(getter_cursor.result_type.spelling) == "void":
            sys.stderr.write(
                f"reflectgen: '{class_name}.{getter_name}' returns void* - not representable as a "
                "Property (ClassBuilder::property() needs a real pointee type); registered as a plain "
                "method instead.\n"
            )
            continue

        # Same reasoning, for the "reflectable collection" shape instead
        # of the plain-scalar one - a getter returning e.g. `const
        # std::vector<std::unique_ptr<Key>>&` (Animation::keys()) auto-
        # detects as TypedPropertyCollection<T, std::unique_ptr<Key>>
        # inside ClassBuilder::property() (see
        # reflectable_collection_element_type()'s own comment), whose
        # get() wraps each element in a std::any - impossible when the
        # element type (here, unique_ptr) can't be copy-constructed at
        # all. Same fallback as everywhere else this session: not a
        # Property, still a plain .method().
        collection_element = reflectable_collection_element_type(getter_cursor.result_type)
        if collection_element is not None and not is_copy_constructible(collection_element):
            sys.stderr.write(
                f"reflectgen: '{class_name}.{getter_name}' returns a collection of "
                f"'{collection_element.spelling}', which isn't copy-constructible - "
                "ClassBuilder::property()'s collection support needs to copy each element; "
                "registered as a plain method instead.\n"
            )
            continue

        setter_names = setter_names_by_stem.get(stem, [])
        setter_cursor = None
        if len(setter_names) == 1:
            setter_candidates = [
                c for c in method_cursors_by_name[setter_names[0]]
                if c.access_specifier == AccessSpecifier.PUBLIC and is_setter_shaped(c)
            ]
            if len(setter_candidates) == 1:
                setter_cursor = setter_candidates[0]
            else:
                sys.stderr.write(
                    f"reflectgen: '{class_name}.{setter_names[0]}' has {len(setter_candidates)} "
                    f"overloads - ambiguous which is the setter for '{stem}'; registered read-only.\n"
                )
        elif len(setter_names) > 1:
            sys.stderr.write(
                f"reflectgen: '{class_name}' has {len(setter_names)} distinct methods "
                f"({', '.join(setter_names)}) that all look like a setter for '{stem}' - "
                "ambiguous, none wired in; registered read-only.\n"
            )

        # A same-stem setter isn't necessarily the *getter's* setter -
        # ClassBuilder::property()'s Setter/PtrSetter shapes both need
        # their one argument to be the same ValueT the getter returns
        # (stripped of const/ref/pointer - see TypedProperty's own
        # aliases), which a same-named "transfer ownership" setter taking
        # e.g. `std::unique_ptr<LayoutParams>` against a getter returning
        # `LayoutParams*` (SubView::layoutParams(), View::style()) isn't:
        # different types, just related ones. Caught here the same
        # string-normalized way has_matching_backing_member() already
        # compares a getter against a backing field's type; a real
        # mismatch falls back to read-only rather than emitting a
        # `.property(...)` call std::invoke can't actually compile.
        if setter_cursor is not None:
            setter_arg = next(setter_cursor.get_arguments())
            setter_arg_type = setter_arg.type.spelling
            if normalize_type_spelling(setter_arg_type) != normalize_type_spelling(getter_cursor.result_type.spelling):
                sys.stderr.write(
                    f"reflectgen: '{class_name}.{setter_cursor.spelling}' takes '{setter_arg_type}', which "
                    f"doesn't match getter '{getter_name}''s return type '{getter_cursor.result_type.spelling}' "
                    f"- not a real setter for '{stem}'; registered read-only.\n"
                )
                setter_cursor = None
            elif (
                setter_arg.type.kind not in (cindex.TypeKind.LVALUEREFERENCE, cindex.TypeKind.RVALUEREFERENCE)
                and not is_copy_constructible(setter_arg.type)
            ):
                # A by-value setter (setCursor(Cursor), not setCursor(const
                # Cursor&)) whose parameter type can't be copy-constructed
                # at all - see is_copy_constructible()'s own comment for
                # why that's fatal for property()'s Setter shape
                # specifically: the wrapping lambda
                # `[setter](T&, const ValueT& value) { std::invoke(setter,
                # self, value); }` (ClassBuilder::property(), reflection.h)
                # itself has to copy `value` to call a by-value setter
                # with it - unconditional, built whenever a setter is
                # given at all, not guarded the way TypedProperty::set()'s
                # *own* body later is.
                sys.stderr.write(
                    f"reflectgen: '{class_name}.{setter_cursor.spelling}' takes '{setter_arg_type}' by value, and "
                    f"that type isn't copy-constructible - ClassBuilder::property()'s setter can't pass it "
                    f"through; registered read-only.\n"
                )
                setter_cursor = None

        # ClassBuilder<T>::property()'s getter/setter overload picks one of
        # several TypedProperty construction modes at compile time
        # (reflection.h) based on the getter's return shape - a
        # PtrGetter/PtrSetter pair only ever moves a pointer at
        # *registration* time, and an "addressable" RefGetter (a non-
        # const-reference-returning getter with no setter, e.g. `MenuItem&
        # root()`, MenuBar) just returns the reference as-is - neither
        # copies ValueT there. TypedProperty::get()/set()/write()/read()
        # are all themselves guarded by `if constexpr
        # (std::is_copy_constructible_v<ValueT>)`, which would make a
        # non-copy-constructible ValueT look safe in every mode... except
        # that guard is only as reliable as the trait itself, and MSVC's
        # own std::is_copy_constructible_v is - verified directly, both
        # against MenuItem itself and against a from-scratch minimal
        # repro (a struct holding nothing but a
        # std::vector<std::unique_ptr<Self>>) - WRONG for exactly this
        # "class holds a container of unique_ptr to its own type" shape:
        # it reports true, so write()/read()'s "safe" branch still gets
        # compiled and still really copy-constructs ValueT inside it
        # (write()'s `ValueT temp = std::any_cast<ValueT>(boxed);`, e.g.),
        # which is real, unconditional MSVC-vtable-forced instantiation
        # (write()/read() are virtual overrides - built for every
        # TypedProperty<T,ValueT> regardless of getter shape or whether
        # they're ever called) - that's what actually fails to compile,
        # cascading through MenuItem's own now-attempted implicit copy
        # constructor into its `std::vector<std::unique_ptr<MenuItem>>
        # children_` member.
        #
        # Since reflectgen has no way to ask MSVC's real, in-compiler
        # trait evaluation whether a given ValueT is one of the types this
        # bites (only its own, more careful is_copy_constructible() -
        # which gets MenuItem right), the only safe policy is to distrust
        # every mode equally here rather than try to carve out the
        # ones that are "supposed" to be safe - .method() has no such
        # weak point (TypedMethod has no write()/read() analogue) and is
        # always available as a fallback regardless of return shape (see
        # method_return_type_unsupported()'s own comment for the
        # narrower rule that applies there instead).
        getter_result_type = getter_cursor.result_type
        if getter_result_type.kind == cindex.TypeKind.POINTER:
            value_type = getter_result_type.get_pointee()
        elif getter_result_type.kind in (cindex.TypeKind.LVALUEREFERENCE, cindex.TypeKind.RVALUEREFERENCE):
            value_type = getter_result_type.get_pointee()
        else:
            value_type = getter_result_type
        if not is_copy_constructible(value_type):
            sys.stderr.write(
                f"reflectgen: '{class_name}.{getter_name}' returns '{value_type.spelling}', which isn't "
                "copy-constructible - ClassBuilder::property()'s TypedProperty<T,ValueT> can't safely be "
                "built for it (see reflectgen's own notes on MSVC's is_copy_constructible_v reliability "
                "here); registered as a plain method instead.\n"
            )
            continue

        if not has_accessor_prefix(getter_name, _GETTER_PREFIXES) and setter_cursor is None:
            if not has_matching_backing_member(stem, getter_cursor.result_type.spelling, fields):
                continue  # not enough signal - stays a plain .method()

        annotation_value = reflect_annotations(getter_cursor).get("property", "true")
        key = derive_property_key(getter_name) if annotation_value.lower() == "true" else annotation_value

        # setter_ambiguous covers both the ordinary "this setter name also
        # has other overloads" case and the same-name-as-the-getter
        # pattern (`int foobar() const` / `void foobar(int)`, see
        # cpp_naming_conventions.md's "Overloaded" style) - in the latter,
        # setter_name == getter_name, so method_cursors_by_name[setter_name]
        # is the *same* overload set the getter's own `cursors`/`ambiguous`
        # already point at, and a bare &Class::setter_name would be just as
        # ambiguous at the point of '&' as a bare &Class::getter_name is -
        # emit_property_setter_expr() wraps it in selectOverload<>() too
        # whenever this is true, using setter_arg_type (and the setter
        # cursor's own const-ness, vanishingly rare but checked for
        # symmetry with the getter's own handling) to build the signature.
        setter_arg_type = None
        setter_ambiguous = False
        setter_is_const = False
        if setter_cursor is not None:
            setter_arg_type = qualify_type_spelling(next(setter_cursor.get_arguments()).type)
            setter_ambiguous = len(method_cursors_by_name[setter_cursor.spelling]) > 1
            setter_is_const = setter_cursor.is_const_method()

        accessors.append(PropertyAccessor(
            key=key,
            scope=SCOPE_NAMES[AccessSpecifier.PUBLIC],
            getter_name=getter_name,
            getter_return_type=qualify_type_spelling(getter_cursor.result_type),
            getter_is_const=getter_cursor.is_const_method(),
            ambiguous=len(cursors) > 1,
            setter_name=setter_cursor.spelling if setter_cursor else None,
            setter_arg_type=setter_arg_type,
            setter_ambiguous=setter_ambiguous,
            setter_is_const=setter_is_const,
        ))

        # Every overload sharing getter_name (not just the chosen one) - a
        # const sibling nobody annotated separately would otherwise still
        # leak into .method() as a redundant read-only view of the same
        # accessor.
        consumed.update(cursors)
        if setter_cursor is not None:
            consumed.add(setter_cursor)

    for stem, setter_names in setter_names_by_stem.items():
        if stem in getter_names_by_stem:
            continue
        for setter_name in setter_names:
            sys.stderr.write(
                f"reflectgen: '{class_name}.{setter_name}' looks like a setter but no matching "
                f"getter was found for '{stem}' - a Property always needs a getter "
                "(ClassBuilder::property()); registered as a plain method instead.\n"
            )

    return accessors


# A member that takes exactly one argument and returns nothing - the shape
# both an add() and a remove() need (see ClassBuilder::propertyCollection()'s
# AddFnT/RemoveFnT in reflection.h). Deliberately not checking the argument's
# type against the collection's own element type - reflectgen has no cheap
# way to compute that from a bare accessor's return type (`const
# std::vector<SubView*>&` needs real template-argument extraction, not just
# string comparison) without more libclang plumbing than v1's other
# heuristics use elsewhere; a real mismatch surfaces as an ordinary compile
# error in the generated .cpp, same as a hand-written registration mistake
# would.
def is_add_remove_shaped(cursor):
    if cursor.result_type.spelling != "void":
        return False
    return sum(1 for _ in cursor.get_arguments()) == 1


# Finds every public, "@reflect collection[=name] add=... remove=..."-
# annotated getter and pairs it with the named add()/remove() methods (both
# optional - either or neither may be given, for a read-only, enumerate-only
# collection) - ClassInfo.collection_accessors, consumed by
# emit_register_function()'s ".propertyCollection(...)" chain entries.
# Mirrors collect_property_accessors() throughout (same opt-in-only
# reasoning, same consumed-set bookkeeping so a getter/add/remove trio
# already driving a collection doesn't also leak into .method() as three
# separately-invocable methods) - kept as a distinct function rather than
# folded into it since the annotation vocabulary (collection vs. property)
# and the shape being assembled (getter+add+remove vs. getter+setter) are
# different enough that sharing the loop body would need as much branching
# as just writing two loops.
def collect_collection_accessors(method_cursors_by_name, consumed):
    accessors = []

    for getter_name, cursors in method_cursors_by_name.items():
        annotated = [
            c for c in cursors
            if c.access_specifier == AccessSpecifier.PUBLIC
            and is_getter_shaped(c)
            and reflect_annotations(c).get("collection")
        ]
        if not annotated:
            continue

        getter_cursor = next((c for c in annotated if not c.is_const_method()), annotated[0])
        annotations = reflect_annotations(getter_cursor)

        annotation_value = annotations.get("collection", "true")
        key = derive_property_key(getter_name) if annotation_value.lower() == "true" else annotation_value

        def find_method(method_name):
            if not method_name:
                return None
            return next(
                (c for c in method_cursors_by_name.get(method_name, [])
                 if c.access_specifier == AccessSpecifier.PUBLIC and is_add_remove_shaped(c)),
                None,
            )

        add_cursor = find_method(annotations.get("add"))
        remove_cursor = find_method(annotations.get("remove"))

        accessors.append(CollectionAccessor(
            key=key,
            scope=SCOPE_NAMES[AccessSpecifier.PUBLIC],
            getter_name=getter_name,
            getter_return_type=qualify_type_spelling(getter_cursor.result_type),
            getter_is_const=getter_cursor.is_const_method(),
            ambiguous=len(cursors) > 1,
            add_name=add_cursor.spelling if add_cursor else None,
            remove_name=remove_cursor.spelling if remove_cursor else None,
        ))

        consumed.update(cursors)
        if add_cursor is not None:
            consumed.add(add_cursor)
        if remove_cursor is not None:
            consumed.add(remove_cursor)

    return accessors


def collect_class(cursor):
    name = qualified_name(cursor)
    has_friend = has_reflect_friend(cursor)
    info = ClassInfo(name, has_friend)

    method_name_counts = {}
    method_cursors = []
    method_cursors_by_name = {}

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
            method_cursors_by_name.setdefault(child.spelling, []).append(child)

    # Every FIELD_DECL regardless of access level or NEWUI_REFLECT_PRIVATE()
    # - has_matching_backing_member() only ever checks a member's name/type
    # (never reflects it), so a private member is just as valid a signal
    # as a public one even in a class that isn't friended at all.
    field_cursors = [child for child in cursor.get_children() if child.kind == CursorKind.FIELD_DECL]

    consumed_by_accessors = set()
    info.property_accessors = collect_property_accessors(method_cursors_by_name, field_cursors, consumed_by_accessors, name)
    info.collection_accessors = collect_collection_accessors(method_cursors_by_name, consumed_by_accessors)

    for child in cursor.get_children():
        kind = child.kind
        access = child.access_specifier

        if kind == CursorKind.FIELD_DECL:
            if access != AccessSpecifier.PUBLIC and not info.has_friend:
                continue
            if child.type.is_const_qualified():
                # ClassBuilder::field()'s ValueT T::* overload (reflection.h)
                # always builds a TypedMemberField<T, ValueT> - unconditionally
                # both readable *and* writable (TypedField::set()/
                # TypedMemberField::set() write through the raw pointer/member
                # unconditionally) - ValueT=const X fails to compile there
                # (assigning through a const X*/const X T::*). A const field
                # is real, read-only C++ that reflectgen's Field vocabulary
                # (see reflection.h's own Field class comment) just doesn't
                # have a read-only variant of yet; skip and warn rather than
                # emit something that can't compile.
                sys.stderr.write(
                    f"reflectgen: '{name}.{child.spelling}' is const-qualified - "
                    "ClassBuilder::field() has no read-only variant; not registered.\n"
                )
                continue
            field_collection_element = reflectable_collection_element_type(child.type)
            if field_collection_element is not None and not is_copy_constructible(field_collection_element):
                # ClassBuilder::field()'s ValueT T::* overload always
                # branches into TypedFieldCollection<T, ValueT> for a
                # reflectable-collection ValueT (reflection.h) - the
                # caller (reflectgen) has no way to force the plain
                # TypedMemberField path instead just by how `&Class::member`
                # is spelled. TypedFieldCollection's element access goes
                # through container_traits<ContainerT>::getByIndex(), which
                # for std::vector returns `c.at(index)` *by value* - the
                # exact same copy-a-move-only-element problem
                # reflectable_collection_element_type()'s own comment
                # describes for a property getter (e.g. Animation::keys()),
                # just reached from a private field going through
                # NEWUI_REFLECT_PRIVATE() instead (MenuItem::children_,
                # menus.h - `std::vector<std::unique_ptr<MenuItem>>`).
                # Not representable via .field() at all - skip entirely.
                sys.stderr.write(
                    f"reflectgen: '{name}.{child.spelling}' is a collection of "
                    f"'{field_collection_element.spelling}', which isn't copy-constructible - "
                    "ClassBuilder::field()'s collection support needs to copy each element; not registered.\n"
                )
                continue
            if (
                is_delegate_field_type(child.type)
                and delegate_sender_matches(child.type, name)
                and not delegate_args_unsupported(child.type)
            ):
                # Never a Property, even though it's a member variable -
                # see Delegate's class comment in reflection.h.
                info.delegates.append(DelegateField(child.spelling, SCOPE_NAMES[access]))
            elif is_delegate_field_type(child.type):
                # Still a real Delegate<...> field, just either with a
                # Sender fixed to some other class (see
                # delegate_sender_matches()'s own comment) or with an Args
                # type TypedDelegate::invoke() can't pass through std::any
                # (see delegate_args_unsupported()'s own comment, e.g.
                # RunLoop's `Delegate<RunLoop, bool&> onIdle;`) -
                # ClassBuilder::delegate()'s templated overload can't
                # express either case, but plain .field() can (no such
                # constraint on ValueT, and never instantiates invoke() at
                # all), so it's still fully reflected, just without the
                # Delegate-specific connect/invoke API.
                info.fields.append(Field(child.spelling, SCOPE_NAMES[access], is_static=False))
            else:
                info.fields.append(Field(child.spelling, SCOPE_NAMES[access], is_static=False))

        elif kind == CursorKind.VAR_DECL and child.semantic_parent == cursor:
            if access == AccessSpecifier.PUBLIC or info.has_friend:
                if child.type.is_const_qualified():
                    # Same TypedField<ValueT>::set() constraint as the
                    # FIELD_DECL branch above (e.g. `static const size_t
                    # Invalid = ...;`, controls.h) - a static field goes
                    # through the exact same const-can't-set() failure.
                    sys.stderr.write(
                        f"reflectgen: '{name}.{child.spelling}' is const-qualified - "
                        "ClassBuilder::field() has no read-only variant; not registered.\n"
                    )
                    continue
                info.fields.append(Field(child.spelling, SCOPE_NAMES[access], is_static=True))

        elif kind == CursorKind.CXX_METHOD and child in method_cursors:
            if child in consumed_by_accessors:
                # Already driving a .property(...) or .propertyCollection(...)
                # entry (see collect_property_accessors()/
                # collect_collection_accessors()) - not also registered as
                # a separately-invocable Method for the same accessor.
                continue
            if access != AccessSpecifier.PUBLIC:
                # Only publicly-invocable methods get a real accessor in v1
                # (see Method::invoke()'s doc comment in reflection.h).
                continue
            if has_unsupported_invoke_arg(child):
                sys.stderr.write(
                    f"reflectgen: '{name}.{child.spelling}' takes a parameter Method::invoke() can't "
                    "pass through std::any (a non-const/rvalue reference, or a by-value type that isn't "
                    "copy-constructible); not registered.\n"
                )
                continue
            if method_return_type_unsupported(child.result_type):
                sys.stderr.write(
                    f"reflectgen: '{name}.{child.spelling}' returns '{child.result_type.spelling}', which "
                    "isn't copy-constructible - Method::invoke() can't pass it back through std::any; "
                    "not registered.\n"
                )
                continue
            arg_types = [qualify_type_spelling(a.type) for a in child.get_arguments()]
            info.methods.append(Method(
                name=child.spelling,
                scope=SCOPE_NAMES[access],
                is_const=child.is_const_method(),
                return_type=qualify_type_spelling(child.result_type),
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
            if cursor.is_abstract_record():
                # TypedConstructor<T,...>::invoke() (reflection.h) does a
                # real `new T(...)` - a class with any pure virtual method
                # left unoverridden (e.g. ViewController::loadView(),
                # Document::readFromFile()/writeToFile() - real abstract
                # base classes meant to be subclassed, not instantiated
                # directly) can't be instantiated at all, constructor
                # visibility notwithstanding; emitting .constructor<...>()
                # for one is a hard compile error (C2259), not a heuristic
                # miss - skip every constructor for the whole class.
                continue
            if has_unsupported_invoke_arg(child):
                # Same std::any_cast<Args> constraint as
                # has_unsupported_invoke_arg()'s own comment describes for
                # TypedMethod - TypedConstructor<T,...>::invokeImpl() does
                # the exact same `std::any_cast<Args>(args.at(I))...` unpack.
                sys.stderr.write(
                    f"reflectgen: '{name}' has a constructor taking a parameter Constructor::invoke() "
                    "can't pass through std::any (a non-const/rvalue reference, or a by-value type that "
                    "isn't copy-constructible); not registered.\n"
                )
                continue
            arg_types = [qualify_type_spelling(a.type) for a in child.get_arguments()]
            info.ctors.append(Ctor(arg_types))

        elif kind == CursorKind.CXX_BASE_SPECIFIER:
            # child.type is the base *as named in the specifier* (e.g. could
            # itself be a typedef) - get_declaration() resolves through to
            # the real class/struct cursor, same reasoning
            # is_delegate_field_type() already applies via get_canonical()
            # for a delegate field's type.
            base_decl = child.type.get_declaration()
            info.bases.append(BaseInfo(qualified_name(base_decl), access))

    return info


def choose_base(info):
    # Class::parentClass() is a single pointer (see reflection.h) - only one
    # base can ever be reflected, so multiple inheritance is only partially
    # supported: the first *public* base found is used, everything else is
    # silently un-representable beyond a stderr warning. A private/protected-
    # only base can't usefully be linked at all (ClassBuilder<T>::base<BaseT>()
    # would compile - std::is_base_of_v doesn't care about accessibility -
    # but reflecting a relationship the class itself hides from the outside
    # would be misleading), so those are reported via plain .derived(true)
    # instead, by the caller (emit_register_function) checking info.bases
    # directly when this returns None.
    public_bases = [b for b in info.bases if b.access == AccessSpecifier.PUBLIC]
    if not public_bases:
        return None
    if len(public_bases) > 1:
        names = ", ".join(b.name for b in public_bases)
        sys.stderr.write(
            f"reflectgen: '{info.name}' has {len(public_bases)} public base classes ({names}) - "
            f"only the first ('{public_bases[0].name}') is reflected via base<BaseT>(); multiple "
            "inheritance isn't representable through Class::parentClass() (a single pointer).\n"
        )
    return public_bases[0].name


def collect_enum(cursor):
    info = EnumInfo(qualified_name(cursor), is_reflect_flags(cursor))
    # Mask to the enum's own underlying-type width (mod 2^width) - matches
    # EnumBuilder<T>'s own toUInt64_ conversion (reflection.h) exactly: it
    # narrows through make_unsigned_t<underlying_type_t<T>> first, then
    # zero-extends to uint64_t. Emitting child.enum_value's raw (possibly
    # negative) Python int directly as a uint64_t literal instead would
    # sign-extend across all 64 bits - fine for a 64-bit underlying type,
    # but a mismatch against toUInt64_'s own narrower-first result for any
    # negative-valued enum constant with a narrower one (e.g. -1 as a
    # 32-bit enum's own EnumValue.value needs to end up 0x00000000FFFFFFFF,
    # not 0xFFFFFFFFFFFFFFFF).
    width_bits = cursor.enum_type.get_size() * 8
    mask = (1 << width_bits) - 1
    for child in cursor.get_children():
        if child.kind == CursorKind.ENUM_CONSTANT_DECL:
            info.values.append(EnumValue(child.spelling, child.enum_value & mask))
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
        if f.scope == "Scope::Public":
            chain.append(f'.field("{f.name}", {f.scope}, &{info.name}::{f.name})')
        else:
            chain.append(f'.field("{f.name}", {f.scope}, detail::ClassAccess<{info.name}>::{f.name}())')

    for d in info.delegates:
        if d.scope == "Scope::Public":
            chain.append(f'.delegate("{d.name}", {d.scope}, &{info.name}::{d.name})')
        else:
            chain.append(f'.delegate("{d.name}", {d.scope}, detail::ClassAccess<{info.name}>::{d.name}())')

    for pa in info.property_accessors:
        getter_expr = emit_property_getter_expr(info, pa)
        if pa.setter_name:
            setter_expr = emit_property_setter_expr(info, pa)
            chain.append(f'.property("{pa.key}", {pa.scope}, {getter_expr}, {setter_expr})')
        else:
            chain.append(f'.property("{pa.key}", {pa.scope}, {getter_expr})')

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
            args.append(f"&{info.name}::{ca.add_name}")
        if ca.remove_name:
            if not ca.add_name:
                # ClassBuilder::propertyCollection()'s remove parameter
                # comes after add positionally - a remove-only collection
                # (no add_name) still has to fill that slot with an
                # explicit nullptr to reach remove.
                args.append("nullptr")
            args.append(f"&{info.name}::{ca.remove_name}")
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



# The namespace a fully-qualified name (info.name, e.g. "newui::gfx::Image")
# was declared in ("newui::gfx"), or None for one at global scope. Used by
# generate() below to bring every involved namespace into scope - see its
# own comment for why that's needed at all.
def enclosing_namespace(qualified_name_str):
    if "::" not in qualified_name_str:
        return None
    return qualified_name_str.rsplit("::", 1)[0]


def generate(classes, enums, sources, extra_includes, register_function_name=DEFAULT_REGISTER_FUNCTION_NAME):
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

    out.append(f"void {register_function_name}()")
    out.append("{\n")

    for funcname in order_registration_calls(function_listing):
        out.append(f"\t{funcname}();\n")

    #out.append("\n")
    #out.append(f"\tReflectionRegistry::addInitFunction(&{register_function_name});")

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

    run_start = time.perf_counter()

    valid_paths = []
    for path in input_files:
        if not os.path.isfile(path):
            sys.stderr.write(f"reflectgen: '{path}' does not exist - skipping\n")
            continue
        valid_paths.append(path)

    # One combined synthetic translation unit - #include "newui/newui.h"
    # plus every input file, in one temp .cpp - parsed ONCE, not once per
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
    # find_declarations()'s existing same_file() check still correctly
    # attributes each declaration back to its own real source file
    # afterward, so per-file output is unaffected, only *how* parsing gets
    # there. (Also meaningfully faster in practice - libclang parses the
    # shared standard/Windows-header preamble once instead of once per
    # input file.)
    tmp_dir = tempfile.mkdtemp(prefix="reflectgen_")
    try:
        prelude_path = os.path.join(tmp_dir, "reflectgen_prelude.cpp")
        with open(prelude_path, "w", encoding="utf-8") as f:
            f.write('#include "newui/newui.h"\n')
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
            sys.stderr.write("reflectgen: combined scan could not be parsed at all - aborting\n")
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
                "reflectgen: combined scan had parse errors (see above) - aborting, no output written\n"
            )
            sys.exit(1)

        for path in valid_paths:
            classes, enums = find_declarations(tu, path)
            all_classes.extend(classes)
            all_enums.extend(enums)
    finally:
        try:
            os.remove(prelude_path)
            os.rmdir(tmp_dir)
        except OSError:
            pass

    if not all_classes and not all_enums:
        sys.stderr.write("reflectgen: no reflectable classes or enums found\n")

    # Everything discovered across every input file lands in this one
    # generate() call, so -o always produces a single combined output file
    # regardless of how many inputs (files or directories) were scanned.
    # The "Source:" comment lists what was passed on the command line
    # (args.inputs), not the (possibly much longer) expanded file list.
    output = generate(all_classes, all_enums, args.inputs, args.include, args.register_function)

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
