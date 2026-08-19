# Reflection

`newui` has a runtime reflection system (`include/newui/reflection.h`) that lets code
discover and read/write a class's members, call its methods, and construct instances by
name — all through `newui::reflection::Class`/`Property`/`Field`/`Method`/`Delegate`
objects rather than compile-time C++ type info. It's used today by the JSON5-based
read/write pipeline in `include/newui/reflectionio.h` (`ObjectWriter`/`ObjectReader` —
see `examples/reflection2.cpp` for a full working example), and is meant to be usable
for other things (scripting bindings, editors, serialization formats) built on the same
`Class`/`Property`/`Field` API.

A class becomes reflectable by **registering** it — building a `newui::reflection::Class`
via `ClassBuilder<T>` and handing it to `ReflectionRegistry::registerClass()`. There are
two ways to do that registration: **by hand** (write the `ClassBuilder<T>` calls
yourself), or **generated** (annotate the class, let `reflectgen` write the registration
function for you, wired automatically into the CMake build). This document covers both,
plus the annotation vocabulary they share.

## Quick example

```cpp
#include "newui/reflection.h"
using namespace newui::reflection;

class Widget {
public:
    int count = 0;                          // plain member -> Field

    const std::string& title() const { return title_; }   // accessor pair -> Property
    void setTitle(const std::string& t) { title_ = t; }

private:
    std::string title_;
};

void registerWidgetReflection() {
    ClassBuilder<Widget> builder;
    builder.clazz()
        .field("count", Scope::Public, &Widget::count)
        .property("title", Scope::Public, &Widget::title, &Widget::setTitle)
        .constructor<>();
    ReflectionRegistry::registerClass(builder);
}
```

Once registered, look the class up anywhere via `classinfo<Widget>()`, `classinfo(typeid(Widget))`,
or `classinfo("Widget")`, and read/write members through `Class::field(name)`/`property(name)`
without knowing `Widget`'s real C++ type at all:

```cpp
const Class* cls = classinfo<Widget>();
Widget w;
cls->property("title")->set(&w, std::any(std::string("hello")));
std::string t = cls->property("title")->getAs<std::string>(&w);
```

## Field vs Property — the one rule that matters

This is the load-bearing distinction in the whole system:

> **A member with no getter/setter methods at all — static or not — is a `Field`. A
> member reached only through real accessor methods is a `Property`.** Never mixed.

- `Field`/`TypedField`/`TypedMemberField`/`TypedFieldCollection` — raw storage access,
  via a real pointer (`ValueT*` for a static class variable) or a real pointer-to-data-member
  (`ValueT T::*` for an ordinary member). `ClassBuilder<T>::field(name, scope, &Class::member)`
  covers both — the C++ type system picks the right shape automatically from `&Class::member`,
  so you never have to say which one it is.
- `Property`/`TypedProperty`/`TypedPropertyCollection` — accessed through a getter (and
  optionally a setter) *method*, never a raw member. `ClassBuilder<T>::property(name, scope,
  getter, setter = nullptr)` covers a getter-only or getter+setter pair; the getter can
  return by value, by reference, or by pointer — see reflection.h's own comment on
  `ClassBuilder::property()` for exactly how each shape behaves (addressable vs. copy-only,
  heap vs. stack on read).

If a member has accessor methods, register it as a `Property` through those methods — not
as a `Field` through its underlying storage, even if that storage happens to be reachable.
Conversely, don't wrap a plain member in a getter/setter pair just to make it a `Property`
— register it as a `Field` directly.

## Collections

A member whose value is itself a collection (`std::vector`/`std::array`/`std::map`) gets
element-level access (`count()`, `get(index)`, `set(index, value)`, key-based access for a
map) in addition to the whole-member `get()`/`set()` — automatically, from the same
`field()`/`property()` calls above, whenever the value type is one of those containers
(`container_traits`, reflection.h). No separate call needed for the common case:

```cpp
.field("numbers", Scope::Public, &Widget::numbers)   // std::vector<int> numbers;
```

For a collection reachable *only* through methods — no real container object exists to
hand back at all, e.g. `View::childViews()`/`addChild()`/`removeChild()`, where the real
storage is a protected member nothing outside `View` can see — use
`ClassBuilder<T>::propertyCollection()` instead, naming the real accessor plus (optionally)
real add/remove methods:

```cpp
.propertyCollection("childViews", Scope::Public,
    &View::childViews, &View::addChild, &View::removeChild)
```

count/index access are derived from `childViews()` internally; `addChild()`/`removeChild()`
are what actually get called on a generic read — never a raw container mutation that would
skip whatever invariants those real methods maintain. If a class has no single accessor at
all (only independent `count()`/`getAt(index)` methods, no method returning the whole
container), use `propertyCollectionByCountAndIndex()` instead — same shape, four separate
methods instead of one accessor plus two.

## Enums

An `enum`/`enum class` is registered separately from any `Class` — via `EnumBuilder<T>`
(mirrors `ClassBuilder<T>`'s own shape: `T` is a template argument, not a runtime value, so
its `typeid` is derived internally) and `ReflectionRegistry::registerEnum()`:

```cpp
enum class Orientation { Horizontal, Vertical };

ReflectionRegistry::registerEnum(EnumBuilder<Orientation>("Orientation")
    .addValue("Horizontal", 0)
    .addValue("Vertical", 1)
    .build());
```

Once registered, any `Property`/`Field` whose value type is that enum is handled
automatically — both by generic code walking `Class::properties()`/`fields()`, and by
`ObjectWriter`/`ObjectReader` (see "Reading/writing objects" below, which is where this
actually matters day to day: an enum-typed property writes/reads as JSON automatically,
no per-enum glue needed).

**Flags enums** — a value meant to be a bitwise-OR of several named constants (window
manager masks, style flags, ...) — need one more thing: `.flags(true)` (or, from
`reflectgen`, a `// @reflect flags` comment directly above the `enum`):

```cpp
enum class ButtonFlags : std::uint32_t {
    None = 0,
    Bold = 1u << 0,
    Italic = 1u << 1,
    Underline = 1u << 2,
    BoldItalic = Bold | Italic,   // a declared combo - see Enum::decompose()'s own comment
};

ReflectionRegistry::registerEnum(EnumBuilder<ButtonFlags>("ButtonFlags")
    .addValue("None", 0)
    .addValue("Bold", 1)
    .addValue("Italic", 2)
    .addValue("Underline", 4)
    .addValue("BoldItalic", 3)
    .flags(true)
    .build());
```

This is **always explicit** — `reflectgen`/`EnumBuilder<T>` never guess "flags-shaped" from
an enum's own values, name, or operators. Every heuristic considered (power-of-two/
combinable values, an `operator|` overload, a `Mask`/`Flags` name suffix) either misses a
real flags enum in this project's own real headers or actively misclassifies an ordinary
one — `DialogResult`'s `Ok=1`/`Cancel=2`/`No=4` are individually powers of two purely by
coincidence of small sequential values, which would make `Abort=5` decompose into the
nonsensical `"Ok"|"No"`. Mark the ones that are really meant to combine; leave everything
else alone.

`Enum::tryParse(name, outValue)`/`tryToString(value, outName)` convert between a value's
name and its `std::uint64_t` numeric form (always unsigned, regardless of the enum's own
underlying type, so bitwise AND/OR/NOT never have sign-extension surprises); a flags enum's
`decompose(value)` breaks a combined value down into the fewest declared names that
reconstruct it, preferring a declared combo name (`"BoldItalic"`) over separately listing
its bits, with a `"0x..."` hex token for any leftover bits nothing declared can name (see
"Reading/writing objects" for where that actually surfaces).

## Reflecting private members — `NEWUI_REFLECT_PRIVATE()`

`field()`/`property()`/`delegate()` all take a real pointer-to-member or method pointer,
which only *compiles* for a public member from outside the class. To reflect a private or
protected member, add `NEWUI_REFLECT_PRIVATE()` once anywhere in the class body:

```cpp
class Gadget {
public:
    NEWUI_REFLECT_PRIVATE();
private:
    int count_ = 7;
};

template<> struct newui::reflection::detail::ClassAccess<Gadget> {
    static constexpr auto count_() { return &Gadget::count_; }
};

// registration:
.field("count_", Scope::Private, detail::ClassAccess<Gadget>::count_())
```

`NEWUI_REFLECT_PRIVATE()` grants access to `newui::reflection::detail::ClassAccess<T>` —
one specialization per class, one static method per private member you want reachable,
each just returning `&Class::member`. `reflectgen` (below) generates this specialization
automatically for every private member it finds in a friended class; you only need to
write it by hand when registering manually.

## Other `ClassBuilder<T>` entries

- `.base<BaseT>()` — links `Class::parentClass()` to `BaseT`'s own already-registered
  `Class` (`BaseT` must be registered *first*) and marks the class derived. `.derived(true)`
  records `isDerived()` alone, for a real base that isn't (or can't be) itself reflected.
- `.method(name, scope, &Class::method)` — a publicly invocable method (`Method::invoke()`
  only ever works for genuinely public methods, unlike `field()`/`property()`/`delegate()`).
- `.delegate(name, scope, &Class::delegateMember)` — a `newui::Delegate<SenderT, Args...>`
  member specifically (see `include/newui/delegate.h`), invoked via its own `syncCall()`.
  A connection made through one of `Delegate<>`'s *named* `add(descriptor, ...)` overloads
  (as opposed to the plain, unnamed `add(...)`) is also serializable — see "Reading/writing
  objects" below for what that actually produces.
- `.constructor<Args...>()` — a real, callable constructor overload; `Class::createInstance()`
  picks the first registered one whose argument count matches what's passed. Skip this if
  `T` is never meant to be freshly constructed through reflection (e.g. a singleton, or a
  type only ever reached as an already-live nested object).
- `.abstract()` / `.isStruct()` / `.singleton()` — plain metadata flags, no behavioral effect.

## Properties are detected automatically

`reflectgen` recognizes a getter/setter pair from naming convention alone — see
`cpp_naming_conventions.md` for the full survey of styles this covers:

- **CamelCase**: `getName()` / `setName()`
- **snake_case**: `get_name()` / `set_name()`
- **Boolean query prefixes**: `isActive()`, `hasPermission()` (get-only, no setter expected)
- **Property-style** (no getter prefix): `title()` paired with `setTitle(...)` or `set_title(...)`
- **Overloaded** (same name, disambiguated by arity/const): `int foobar() const` /
  `void foobar(int)`

All matching is case- and underscore-insensitive on the prefix, so `getTitle`/`get_title`/
`GETTITLE` are recognized as the same accessor. No annotation is needed for any of this —
plain data members are picked up automatically as `Field`s (see above) and accessor pairs
matching one of these shapes are picked up automatically as `Property`s.

A **bare, unprefixed getter with no setter** (`title()` alone, no `get`/`is`/`has` prefix
and no `setTitle`) is the one case that needs more than shape to be trusted — a computed
method like `View::computeDesiredSize()` looks structurally identical to a real accessor.
`reflectgen` only registers it as a read-only property if there's a matching private
backing member of the same type (`title_`, `_title`, or `m_title`) — real evidence the
getter is backed by real storage, not just a query. No member, no property: it stays a
plain `.method(...)` entry, silently (this is the expected, common case for most computed
methods, not a failure worth flagging). A `get`/`is`/`has`-prefixed getter needs no such
check — the prefix itself is already the deliberate signal a human gave it.

`reflectgen` warns on stderr (and registers nothing as a property, or falls back to
`.method(...)`) whenever it genuinely can't resolve something safely:
- two distinct methods normalize to the same stem (e.g. both `title()` and `getTitle()`
  exist) — ambiguous which one is "the" getter.
- a `set`/`set_`-prefixed method has no matching getter at all — a `Property` always needs
  a getter (`ClassBuilder::property()`'s own contract), so this can't become one.
- a matched setter itself has multiple overloads — ambiguous which one to wire in; the
  property is still registered, just read-only.

## Annotations (`@reflect ...`) — overrides, not requirements

Annotations are for the cases the heuristic can't (or shouldn't) resolve on its own — they
are no longer required to opt a method in. Written as a `//` (or `/* */`) comment directly
above the declaration:

| Annotation | Where | Meaning |
|---|---|---|
| `// @reflect property` | above a getter method | Force-register as a `.property(...)` even if the heuristic above wouldn't have picked it up (e.g. a bare getter with no backing member). |
| `// @reflect property=someName` | above a getter method | Same, but names the property `someName` instead of deriving one from the method name. |
| `// @reflect ignore=true` | above a getter method | Opt a specific method **out** of automatic detection — the escape hatch for a heuristic false positive. |
| `// @reflect collection` | above a whole-container-returning getter | Register as a `.propertyCollection(...)` — collections are still opt-in only (getter+add+remove can't be told apart from three unrelated methods by shape alone). |
| `// @reflect collection=someName add=addMethod remove=removeMethod` | above a whole-container-returning getter | Same, with an explicit name and/or the real add/remove methods to wire in (either, both, or neither). |
| `// @reflect ignore=true` | above a class/struct | Excludes the whole class from generation. |
| `// @reflect flags` | above an `enum`/`enum class` | Registers it as a combinable *flags* enum — see "Enums" below. Always opt-in, never guessed from the enum's own values/name/operators (see that section for why every shape-based heuristic tried against this project's own real enums turned out unsafe). |

The bare forms (`@reflect property`, `@reflect collection`) and the `key=value` forms can
be freely mixed on the same line (`@reflect collection add=addChild remove=removeChild`
— `collection` bare, `add`/`remove` with values).

An overloaded getter (a const/non-const pair, e.g. `ViewStyle& style()` vs. `const
ViewStyle& style() const` — or the "same name" setter pattern above) is disambiguated
automatically — `reflectgen` prefers the non-const getter overload and wraps whichever
side needs it in `selectOverload<Signature>(...)` for you; every overload of a detected
getter (and its setter, if from the same overloaded name) is excluded from separate
`.method(...)` registration, so the same accessor never ends up double-registered.

## Running `reflectgen` by hand

`tools/reflectgen/reflectgen.py` scans one or more headers/sources with libclang and emits
a single `.cpp` containing a `register_<Class>Reflection()` function per class/enum it
finds (plus any `detail::ClassAccess<T>` specializations private members need), plus one
master function — `registerReflectionData()` by default — that calls all of them, base
classes before derived (see "Automatic CMake integration" below for the full story on that
function and how to rename it).

**One-time setup** (see `tools/reflectgen/README.md` for the full story — MSVC STL/libclang
version notes, the no-system-LLVM fallback, etc.):

```
cd tools/reflectgen
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
```

**Usage:**

```
.venv\Scripts\python reflectgen.py <header_or_source.h/.cpp> [more files or directories...] -o <output.cpp> [-- <extra clang args>]
```

| Option | Meaning |
|---|---|
| `inputs` (positional, required) | One or more header/source files and/or directories to scan. |
| `-o`, `--output` (required) | Output `.cpp` path — every class/enum found across *every* input still lands in this one file. |
| `--include HEADER` | Extra `#include "HEADER"` line in the generated output (repeatable — one per source header is typical, since `reflectgen` doesn't include a class's own header for you). |
| `--ext .EXT` | File extension to scan when an input is a directory (repeatable; default `.h`). |
| `--no-recursive` | Only scan a directory input's top level, not subdirectories. |
| `--register-function NAME` | Name of the generated master function (default `registerReflectionData`) — see "Automatic CMake integration" below for why you'd rename it. |
| `-v`, `--version` | Print the tool's version and exit. |
| `-- <clang args>` | Extra arguments passed straight to libclang — include paths (`-I...`), `-std=...`, defines. Quote every `-I...` path in PowerShell (see `tools/reflectgen/README.md`'s own note on a real tokenizer gotcha there). |

Example — everything under `include/newui`, one combined output:

```powershell
.venv\Scripts\python reflectgen.py ..\..\include\newui -o all.gen.cpp `
    -- "-Id:\code\newui\include" "-Id:\code\newui\3rdparty\json5\include" -std=c++17
```

A generated file is **not auto-regenerated by hand-invoking the tool** — re-run it and
diff/replace the output if the source class(es) change. (The CMake integration below
*does* keep it in sync automatically for headers under `include/newui`.)

## Automatic CMake integration

Reflectable headers under `include/newui` are discovered and regenerated automatically as
part of the normal build — no manual `reflectgen` invocation needed for anything that
lives there. Two stages (`cmake/ReflectGen.cmake`):

1. **Discovery** (CMake *configure* time) — `tools/reflectgen/discover_reflectable.py`
   scans every header under `include/newui` and writes the result to a generated
   `REFLECTGEN_HEADERS` CMake list. `reflection.h`/`reflectionio.h` themselves are always
   excluded (they define the reflection system, not application classes to register). This
   re-runs automatically whenever a header is added or removed
   (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS ...)`) — reliably under the Ninja generator
   (`out/build/<config>/`), less reliably under the plain Visual Studio generator
   (`build/`), where a manual reconfigure may occasionally be needed.
2. **Generation** (*build* time) — an `add_custom_command()` runs `reflectgen.py` itself
   against `REFLECTGEN_HEADERS`, producing one combined `<target>_reflection_generated.cpp`
   that's added straight to the `newui` library's sources. Real, per-file incremental:
   only reruns when one of those specific headers (or `reflectgen.py` itself) actually
   changes — not on every build. **Not committed to source control** (`out/`/`build/` are
   both `.gitignore`d) — it's a build artifact, always regenerated, never hand-edited.

Both stages need `tools/reflectgen/.venv` set up (the one-time setup above) — CMake
configure fails with a clear error naming the missing venv if it isn't.

**`NEWUI_REFLECTGEN_REQUIRE_MARKER`** (CMake option, **default `OFF`**) controls which
headers discovery actually includes:
- `OFF` (default, scan-all): every header under `include/newui` is scanned
  unconditionally — a class is reflectable unless it explicitly opts out
  (`@reflect ignore=true` on the class, or per-member) rather than a human remembering to
  mark every file that should participate. This is the real default because opt-in-only
  isn't very useful in practice: it requires going back and annotating every existing class
  by hand before reflectgen does anything at all. Verified end-to-end against every real
  header in this project — a full build (`newui`, `newui_tests`, every example) and the
  full test suite (583/583, as of this writing) both succeed with this on. `reflectgen.py` itself independently
  falls back to `.method()`/`.field()`/skips registration entirely (with a `reflectgen:`
  warning on stderr, not a build failure) for any accessor shape it can't safely wire up —
  see reflectgen.py's own comments on the non-copy-constructible-getter/setter/collection-
  element cases, the abstract-class-constructor case, and the MSVC
  `std::is_copy_constructible_v` reliability gap for a self-referential
  `std::vector<std::unique_ptr<T>>`-holding `T`, all found and fixed by scanning this
  project's own real headers.
- `ON`: only a header containing an `@reflect` annotation or `NEWUI_REFLECT_PRIVATE()`
  anywhere is included — the old opt-in-only mode. Add either to a header to make it
  participate; the next build (reconfigure first, if needed — see the Ninja/VS caveat
  above) picks it up automatically. Still available for a project that wants to reflect
  only a deliberately-chosen subset of its classes.

A generated `.cpp` this large (every real header, one registration function per class) can
exceed MSVC's default per-object-file section count — `newui_add_reflectgen_output()`
already compiles its own generated file with `/bigobj` (MSVC only) to cover this; nothing
to configure on your end.

`tools/reflectgen/reflectgen.py` itself has never required a marker to process a *class* —
`discover_reflectable.py`'s check is a separate, additional gate that only exists for this
automated build path (see `--require-marker`/`--var` in its own `--help`); running
`reflectgen.py` directly against a directory always scans every class in every file it's
pointed at, markers or not.

Each generated `.cpp` ends with one master function — `registerReflectionData()` by
default — that calls every individual `register_*Reflection()`/`register_*Enum()` function
it emits, exactly once each, in an order that's guaranteed **base classes before their own
derived classes** (a topological sort over each class's `.base<BaseT>()` — necessary because
`ClassBuilder<T>::base<BaseT>()` throws `std::logic_error` if `BaseT` isn't registered yet;
discovery order alone can't guarantee that). Calling *that* one function is still up to
application setup code — `src/main.cpp` does it with a plain
`extern void registerReflectionData(); ...; registerReflectionData();`, and
`unittests/test_reflection.cpp` does the same via a `::testing::Environment` so it runs once
before any test — `examples/reflection1.cpp`/`reflection2.cpp` still call their own
hand-written per-class registration functions individually instead (they predate this and
demonstrate the hand-written path deliberately), which still works fine, just doesn't need
`registerReflectionData()` at all.

The master function's name is configurable, if a target ever needs more than one
`newui_add_reflectgen_output()` call of its own (two different `SCAN_DIRS`, say) — two
`void registerReflectionData()` definitions in one binary won't link, so a second call needs
a distinct name:

```cmake
newui_add_reflectgen_output(myapp
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/myapp/widgets
    REGISTER_FUNCTION registerWidgetsReflection
)
```

(Or, calling `reflectgen.py`/`generate_reflection.py` directly: `--register-function NAME`.)

As of this writing, every real header under `include/newui` is reflected automatically via
scan-all (114 classes, 32 enums, across the 34 headers with anything reflectable) - `newui`,
`newui_tests`, and every example all build clean against the generated output, and the full
test suite passes.

## Using reflectgen in your own project

`newui_add_reflectgen_output()` (`cmake/ReflectGen.cmake`) isn't specific to the `newui`
target — it's a general-purpose CMake function that takes the target and scan directories
as arguments, so a project that uses `newui` as a library can call it again for its *own*
classes, completely independently of `newui`'s own use of the same machinery. This section
walks through doing that, end to end.

**1. Get `newui`'s CMake into your build.** If you already consume `newui` via
`add_subdirectory(path/to/newui)` (or `FetchContent`), `cmake/ReflectGen.cmake` is already
`include()`d as part of `newui`'s own `CMakeLists.txt` processing — `newui_add_reflectgen_output()`
and the `NEWUI_REFLECTGEN_REQUIRE_MARKER` option are already available in your own
`CMakeLists.txt` once that call returns, no extra `include()` needed on your end.

**A note on scan-all (the default) for your own headers:** every real class under your own
`SCAN_DIRS` is reflected unless it opts out, same as `newui`'s own headers — the `@reflect`
markers in the next step are for overriding the automatic property/name detection, not for
opting a class in at all. If reflectgen's heuristics hit a shape they can't safely handle
(the non-copy-constructible-return/argument cases `reflectgen.py`'s own comments describe,
say), that one accessor falls back to a plain `.method()` (or is skipped, with a
`reflectgen:` warning on stderr) rather than failing your build — still real, callable C++
either way, just not reflected the way you might expect. Check the build output for these
warnings after first enabling reflectgen against a new set of headers.

**2. Make sure `tools/reflectgen/.venv` is set up** inside your copy of the `newui` source
tree (the "One-time setup" steps above) — `newui_add_reflectgen_output()` resolves the
Python/libclang environment relative to `newui`'s own source directory
(`${newui_source_dir}/tools/reflectgen/.venv`), not your project's, so this only needs
doing once per `newui` checkout, regardless of how many downstream projects use it.

**3. Write a class with `@reflect`/`NEWUI_REFLECT_PRIVATE()` markers**, same as anywhere
else in this document — nothing project-specific here:

```cpp
// myapp/widget.h
#pragma once
#include <newui/reflection.h>
#include <string>

class MyAppWidget {
public:
    MyAppWidget() = default;

    // @reflect property
    const std::string& label() const { return label_; }
    void setLabel(const std::string& v) { label_ = v; }

private:
    std::string label_;
};
```

**4. Call `newui_add_reflectgen_output()` for your own target**, after defining it, passing
`SCAN_DIRS` for wherever your reflectable headers live (your own `include/newui` equivalent
— can be one directory or several) and, if any of your headers need `-I` paths beyond what
`newui` itself already needs (its own `include/`, `3rdparty/json5/include`, etc. — always
added automatically), `INCLUDE_DIRS` for those:

```cmake
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE newui)

newui_add_reflectgen_output(myapp
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/myapp
    # INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/myapp/third_party/include   # only if needed
)
```

This registers a build-time step - discovery + generation together, real per-file
incremental (see "Automatic CMake integration" above for the mechanics) - that produces
`${CMAKE_CURRENT_BINARY_DIR}/generated/myapp_reflection_generated.cpp` and adds it straight
to `myapp`'s sources. `myapp`'s own generated file is completely independent of `newui`'s
own (`newui_reflection_generated.cpp`) - each target gets its own, scoped to its own
`SCAN_DIRS`. `NEWUI_REFLECTGEN_REQUIRE_MARKER` (`OFF`/scan-all by default, or
`-DNEWUI_REFLECTGEN_REQUIRE_MARKER=ON` for the old opt-in-only mode - see its own section
above) applies to every `newui_add_reflectgen_output()` call process-wide, not per call.

**5. Call the generated master registration function.** Nothing does this for you - add
`extern void registerReflectionData(); /* ... */ registerReflectionData();` somewhere in
your own startup code (see "Automatic CMake integration" above for exactly what that one
call does - registers every class/enum found under your `SCAN_DIRS`, base classes before
their own derived classes). If your target also called `newui_add_reflectgen_output()` a
second time with its own `REGISTER_FUNCTION` name (needed only if you have more than one
such call for the same target - see that section), call that name instead.

**Verified working end to end** (not just described) with exactly the `MyAppWidget` example
above, in a throwaway directory outside `newui`'s own tree, via
`newui_add_reflectgen_output(reflection2 SCAN_DIRS <that dir>)` temporarily added to
`examples/CMakeLists.txt`: a real CMake reconfigure + build produced a correctly-scoped,
separate `reflection2_reflection_generated.cpp` (empty placeholder before the `@reflect
property` marker was added, real `MyAppWidget` registration after - both without touching
`newui`'s own generated output at all), which compiled and linked cleanly.

## Reading/writing objects (`reflectionio.h`)

`ObjectWriter`/`ObjectReader` (`include/newui/reflectionio.h`) are a generic JSON5
read/write pipeline built entirely on the `Class`/`Property`/`Field` API above — no
knowledge of any specific class. Two entry points on each:

- **Single instance** — `ObjectWriter::write(InstanceT*)` / `ObjectReader::read(InstanceT*)`.
  The document's root *is* the instance itself (`{ meta: {...}, type: "...", ...its own
  properties... }`) — read reuses an existing live object, never allocates one. See
  `examples/reflection2.cpp`'s `demoWriter()`/`demoRoundTrip()` for a complete, working
  example (write a real object tree, read it back into a fresh one, verify field-by-field)
  including document metadata (author/date/copyright/version) and collection reconstruction
  via a `propertyCollection()`'s real add method.
- **Multiple named instances** — `ObjectWriter::writeObjects(vector<NamedObject>)` /
  `ObjectReader::readObjects()`. Several independently-named instances share one document,
  as siblings at the root alongside `meta`:

  ```json5
  {
    meta: { ... },
    foo: { type: "FooBar", ... },
    bar: { type: "Bar", ... }
  }
  ```

  `readObjects()` returns every instance it constructed (owned by the caller from that
  point on, same convention `Class::createInstance()` already has) and does this in two
  passes: first every named instance is constructed and its ordinary properties read
  (`Class::read()`, same as the single-instance path); only once every instance in the
  document exists does a second pass resolve delegate connections (below) that reference a
  *sibling* object by name — which is the whole reason this needs to be a document-level
  operation rather than something a single `Class::read()` call could do on its own.

### Delegate connections

A `Delegate<>` field with **at least one currently-connected, *describable* listener** (see
"Other `ClassBuilder<T>` entries" above — only a connection made through a named
`add(descriptor, ...)` counts; a plain `add(...)` connection is real but invisible to this)
gets a `"delegates"` object, keyed by the delegate's own field name, whose value is an array
of every describable connection's own descriptor string:

```json5
foo: {
  type: "FooBar",
  delegates: {
    onHappyChanged: [ "HappyChanged", "bar@Bar.happyChanged" ]
  }
}
```

Two descriptor forms:
- **A bare name** (`"HappyChanged"`) — a free/static function. Write-only today: the name
  round-trips into the file, but reading one back doesn't reconnect it (no name→address
  registry exists yet - `ObjectReader` logs a message and skips it, doesn't fail the read).
- **`<object>@<Class>.<method>`** (`"bar@Bar.happyChanged"`) — another object *in the same
  document*, reconnected on read via reflection: `bar` is looked up by name in the
  document's own name→instance map (see `readObjects()` above), then `happyChanged` is
  looked up as an ordinary, already-registered `Method` on `Bar`'s own `Class` (walking its
  base chain). `<Class>` itself isn't strictly enforced (a mismatch just logs a warning and
  connects using the object's real class) - it's there for readability and as a sanity
  check, not a hard requirement.

  The one real constraint this places on `happyChanged`'s own C++ signature: it has to take
  its sender **by pointer** (`SyncReturn happyChanged(FooBar* sender, bool value)`), not by
  reference — that's what makes it reachable as an ordinary reflected `.method()` at all
  (`Method::invoke()`'s `std::any`-based argument boxing can't safely carry a live mutable
  reference; a pointer is just a safe address copy). `newui::Delegate<>`'s own listener
  signature (`SourceT&`, used by every ordinary hand-wired `add(instance, method)`
  connection) is completely unaffected — the pointer convention only applies to a method
  meant to be reconnectable this way.

  Every failure mode here (unknown object name, unknown method, a signature that doesn't
  match) logs a message and skips just that one connection - it never fails the whole read.

Any *un*-connected `Delegate<>` field, or one whose only connections are undescribed, gets
no `"delegates"` entry at all - not even an empty one.

### Enum properties

A `Property`/`Field` whose value type is a registered enum (see "Enums" above) writes/reads
automatically: a plain enum as one JSON5 string, a `.flags(true)`-registered one as an array
of decomposed flag names:

```json5
orientation: "Vertical",
flags: [ "Bold", "Underline" ]
```

A value with no exact match (a plain enum) — or leftover bits nothing declared can name (a
flags enum) — falls back to a `"0x..."` hex token rather than silently dropping data, so
even a value nothing here can fully name still round-trips losslessly.

### Trying it end to end

`examples/reflection2.cpp`'s "Demo 3" (`demoDelegateAndEnumFileRoundTrip()`) exercises all
three of the above together, against a real file on disk (not just an in-memory string):
writes a `DemoPanel`/`DemoLogger` pair — a plain enum, a flags enum, and a delegate
connected across the two objects — via `writeObjects()` to `reflection2_demo3.json5`, reads
that file back via `readObjects()`, and *fires* the reconnected delegate to prove
`DemoLogger::onPanelResized` genuinely ran, not just that the JSON looked right.
`unittests/test_delegateserialization.cpp` and `unittests/test_enumserialization.cpp` cover
the same ground headlessly, including the failure-mode paths (unknown object, unrecognized
flag token, ...).

## Further reading

- `include/newui/reflection.h` — the API itself; every class has a substantial doc
  comment explaining its own design (start with `Property`, `Field`, and `ClassBuilder`).
- `tools/reflectgen/README.md` — full `reflectgen` setup/usage details, including the
  MSVC STL/libclang version-compatibility notes and base-class handling.
- `examples/reflection1.cpp` — a minimal, fully hand-written registration example
  (`Widget`/`SuperWidget`), good for seeing every `ClassBuilder` call in isolation.
- `examples/reflection2.cpp` — a real read/write round-trip against `newui`'s own
  `Application`/`Frame`/`View`/`SubView`/`ViewStyle` hierarchy (Demos 1-2), plus a
  delegates+enums round trip through a real file on disk (Demo 3).
- `unittests/test_reflection.cpp`, `unittests/test_reflectionio.cpp` — headless coverage
  of the `Class`/`Property`/`Field`/collection API and the JSON5 read/write pipeline.
- `unittests/test_delegate.cpp` — `newui::Delegate<>`'s own descriptor-tracking `add(...)`
  overloads and `describedListeners()`, independent of reflection entirely.
- `unittests/test_delegateserialization.cpp`, `unittests/test_enumserialization.cpp` —
  headless coverage of the "delegates" JSON5 block (including cross-object reconnection)
  and enum-typed properties (plain and flags), respectively.
