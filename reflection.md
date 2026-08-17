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
finds (plus any `detail::ClassAccess<T>` specializations private members need).

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
   scans every header under `include/newui` for an `@reflect` annotation or
   `NEWUI_REFLECT_PRIVATE()` (a cheap substring check, not a real parse) and writes the
   matching subset to a generated `REFLECTGEN_HEADERS` CMake list. `reflection.h`/
   `reflectionio.h` themselves are excluded (they define the reflection system, not
   application classes to register). This re-runs automatically whenever a header is
   added or removed (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS ...)`) — reliably under the
   Ninja generator (`out/build/<config>/`), less reliably under the plain Visual Studio
   generator (`build/`), where a manual reconfigure may occasionally be needed.
2. **Generation** (*build* time) — an `add_custom_command()` runs `reflectgen.py` itself
   against `REFLECTGEN_HEADERS`, producing one combined `<target>_reflection_generated.cpp`
   that's added straight to the `newui` library's sources. Real, per-file incremental:
   only reruns when one of those specific headers (or `reflectgen.py` itself) actually
   changes — not on every build.

Both stages need `tools/reflectgen/.venv` set up (the one-time setup above) — CMake
configure fails with a clear error naming the missing venv if it isn't.

**To make a real header participate**: add `NEWUI_REFLECT_PRIVATE()` and/or `@reflect`
annotations to it as described above. The next build picks it up automatically (a fresh
reconfigure first, if needed — see the Ninja/VS caveat above). Nothing calls the generated
`register_*Reflection()` functions automatically, though — that's still up to application
setup code, the same way `examples/reflection1.cpp`/`reflection2.cpp` call their own
hand-written ones.

As of this writing, no real header under `include/newui` carries a reflectgen marker yet —
every class currently registered against `newui::reflection` (`examples/reflection2.cpp`)
is still hand-written. The pipeline is proven correct (verified against a throwaway
annotated header, added and removed, real CMake reconfigures both directions) and ready
for whenever a real header opts in.

## Reading/writing objects (`reflectionio.h`)

`ObjectWriter`/`ObjectReader` (`include/newui/reflectionio.h`) are a generic JSON5
read/write pipeline built entirely on the `Class`/`Property`/`Field` API above — no
knowledge of any specific class. See `examples/reflection2.cpp`'s `demoWriter()`/
`demoRoundTrip()` for a complete, working example (write a real object tree, read it back
into a fresh one, verify field-by-field) including document metadata (author/date/
copyright/version) and collection reconstruction via a `propertyCollection()`'s real
add method.

## Further reading

- `include/newui/reflection.h` — the API itself; every class has a substantial doc
  comment explaining its own design (start with `Property`, `Field`, and `ClassBuilder`).
- `tools/reflectgen/README.md` — full `reflectgen` setup/usage details, including the
  MSVC STL/libclang version-compatibility notes and base-class handling.
- `examples/reflection1.cpp` — a minimal, fully hand-written registration example
  (`Widget`/`SuperWidget`), good for seeing every `ClassBuilder` call in isolation.
- `examples/reflection2.cpp` — a real read/write round-trip against `newui`'s own
  `Application`/`Frame`/`View`/`SubView`/`ViewStyle` hierarchy.
- `unittests/test_reflection.cpp`, `unittests/test_reflectionio.cpp` — headless coverage
  of the `Class`/`Property`/`Field`/collection API and the JSON5 read/write pipeline.
