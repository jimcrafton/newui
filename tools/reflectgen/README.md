# reflectgen (v1)

Scans a C++ header/source file with libclang and emits a `.cpp` that
registers each class it finds against `newui::reflection` (see
`include/newui/reflection.h`) - the same shape of code
`examples/reflection1.cpp` writes by hand for `Widget`.

v1 status: standalone script, run manually (also wired into the CMake
build for headers under `include/newui` - see `cmake/ReflectGen.cmake`
and `reflection.md`'s own "Automatic CMake integration" section at the
repo root). Handles: public/private data members (static or not) as
fields, getter/setter method pairs detected by naming convention as
properties (see below; an explicit annotation still works as an override,
just isn't required), explicitly-annotated whole-container-accessor/add/
remove method groups as collection properties (see below),
`newui::Delegate<SenderT, Args...>` members (typedef'd or not) as
delegates, non-overloaded-or-disambiguated public methods, public
non-copy/move constructors, base classes (see below), and enums
(namespace-scope or public nested - a private/protected nested enum's name
isn't spellable from the free `register...Enum()` function reflectgen
emits, so those are skipped). Static methods, templates, and operator
overloads are explicitly out of scope for now.

**Getter/setter properties (detected automatically):** a public
getter-shaped method (zero args, non-`void` return) is registered as a
`.property(...)` entry instead of a `.method(...)` one whenever it matches
one of the naming conventions surveyed in `cpp_naming_conventions.md`
(repo root) - case- and underscore-insensitive, so `getTitle`/`get_title`/
`GETTITLE` all normalize to the same accessor:

- **CamelCase** / **snake_case**: `getName()`/`setName(...)`,
  `get_name()`/`set_name(...)`
- **Boolean query prefixes**: `isActive()`, `hasPermission()` - get-only,
  no setter expected
- **Property-style** (no getter prefix): `title()` paired with
  `setTitle(...)` or `set_title(...)`
- **Overloaded** (same name, disambiguated by arity/const, e.g.
  `int foobar() const` / `void foobar(int)`)

A **bare, unprefixed getter with no setter** (`title()` alone) is the one
shape that needs more than naming to trust - structurally identical to a
computed method like `View::computeDesiredSize()` or `View::cursor()`.
It's only registered as a read-only property if there's a matching
private backing member of the same type (`title_`, `_title`, or
`m_title`) - real evidence of storage, not just a query; no match, and it
stays a plain `.method()` entry, silently (the expected outcome for most
computed methods, not a failure). A `get`/`is`/`has`-prefixed getter needs
no such check - the prefix is already the deliberate signal.

`reflectgen` warns on stderr whenever it can't resolve something safely,
rather than guessing: two distinct methods normalizing to the same stem
(ambiguous which is "the" getter - neither becomes a property), a
`set`/`set_`-prefixed method with no matching getter at all (can't become
a `Property` - `ClassBuilder::property()` always needs a getter -
registered as a plain method instead), or a matched setter that's itself
ambiguously overloaded (property still registered, just read-only).

An overloaded getter (a const/non-const pair, e.g. `ViewStyle& style()`
vs. `const ViewStyle& style() const` - or the "same name" setter pattern
above) is disambiguated automatically:

```cpp
ViewStyle& style() { return *style_; }
const ViewStyle& style() const { return *style_; }
```

emits `.property("style", Scope::Public, selectOverload<ViewStyle&(View::*)()>(&View::style))`
- `&Class::getter` on its own doesn't compile there (see reflection.h's
own comment on `selectOverload`). Every overload of a detected getter
(and its setter, when it comes from the same overloaded name) is excluded
from separate `.method(...)` registration, so the same accessor never
ends up double-registered.

**`@reflect property`/`@reflect property=someName`** still work exactly
as before, written directly above a getter - now as an *override* (force
a method the heuristic wouldn't have picked up, e.g. a bare getter with no
backing member, or rename the derived key) rather than a requirement.
**`@reflect ignore=true` above a specific method** is new: the escape
hatch for a heuristic false positive, opting that one method out of
automatic detection without touching the rest of the class (`@reflect
ignore=true` above a *class* still excludes the whole class, as before).

**Plain data members - fields, not properties:** a member variable with
no accessor methods at all (static or not) becomes a `.field(...)` entry,
never `.property(...)` - `ClassBuilder<T>::field()`'s pointer-to-member
overload (`reflection.h`) covers both a real static `ValueT*` and a
non-static `ValueT T::*` the same way, so reflectgen emits the identical
`&Class::name` expression either way and lets the C++ type system pick
the right overload. Only a member actually reached *through* a getter/
setter method (see above) is a `.property(...)`.

**Collection properties:** a public, zero-argument, non-`void`-returning
method marked `// @reflect collection` (or `// @reflect
collection=someName`, same naming rule as `@reflect property`) directly
above it becomes a `.propertyCollection(...)` entry - the accessor-based
overload, for a class with one real method that returns the whole
container (e.g. `const std::vector<SubView*>& View::childViews() const`).
`add=methodName`/`remove=methodName` (either, both, or neither) name the
real add/remove methods (each public, one argument, `void`-returning) to
wire in - e.g.

```cpp
// @reflect collection add=addChild remove=removeChild
const std::vector<SubView*>& childViews() const { return childViews_; }
```

emits `.propertyCollection("childViews", Scope::Public, &View::childViews,
&View::addChild, &View::removeChild)`. Same opt-in-only reasoning as
`@reflect property` above - a getter/add/remove trio can't be told apart
from three unrelated methods by shape alone - and every method involved
(getter, add, remove) is excluded from `.method(...)` registration, same
"don't double-register the same accessor" rule.

Not currently generated: the no-single-accessor shape
`ClassBuilder<T>::propertyCollectionByCountAndIndex()` covers (a class with
only independent `count()`/`getAt(index)` methods and no method that
returns the whole container at all, e.g. `test_reflectionio.cpp`'s
`Sprocket`) - real, hand-writable, just not something reflectgen detects
yet; nothing in the real (non-test) codebase needs it generated today.

**Base classes:** a class with exactly one *public* base gets a
`.base<BaseClass>()` call emitted (first in the chain, before any
`.property()`/`.method()`/etc.) - the same call
`ClassBuilder<T>::base<BaseT>()` in `reflection.h` expects, which links
`Class::parentClass()` to `BaseClass`'s own already-registered `Class`. As
with hand-written registration, **the base's own `register...Reflection()`
function must be called before the derived class's** - reflectgen has no
way to enforce or automate that ordering across (possibly separately
generated) registration functions; get it wrong and `base<BaseT>()` throws
`std::logic_error` at the call site, not silently. A class with only a
private/protected base (or no base at all but some other real C++
inheritance reflectgen can see) gets a plain `.derived(true)` instead -
there's nothing for `base<BaseT>()` to usefully link to, but `isDerived()`
still reflects the truth. Multiple public bases aren't representable
(`Class::parentClass()` is a single pointer) - only the first is reflected,
with a warning on stderr naming the ones that were dropped.

## Setup

```
cd tools/reflectgen
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
```

Uses the `clang` PyPI package (bindings only, no bundled native library) so
the venv stays in sync with whatever LLVM/libclang is actually installed on
the machine rather than an older bundled copy. It points at
`C:\Program Files\LLVM\bin\libclang.dll` by default - set the
`LIBCLANG_PATH` environment variable to override. The bindings (21.1.7 -
the newest published on PyPI as of writing) are newer than a system
libclang 19.1.0; `configure_libclang()` sets `Config.compatibility_check =
False` so bindings calling a handful of C API functions the older library
doesn't export (base-class layout queries reflectgen never uses) fail only
if actually invoked, not eagerly at startup.

**MSVC STL version gate:** this machine's MSVC STL headers hard-error via
a `static_assert` unless the compiler is Clang 20+ (or `-D_ALLOW_COMPILER_
AND_STL_VERSION_MISMATCH` is defined) - libclang 19.1.0 trips it the moment
a scanned file transitively includes `<string>`/`<vector>`/etc. reflectgen
passes that define automatically by default. It's a real compatibility
gap, not just noise - if it ever causes a genuine parse mismatch, the fix
is upgrading the system LLVM install (22.1.0 is current stable) rather
than relying on the bypass define indefinitely.

**No system LLVM found?** `configure_libclang()` falls back automatically:
if `LIBCLANG_PATH`/the default `C:\Program Files\LLVM\bin\libclang.dll`
doesn't exist, it pulls just the native library out of the `libclang`
PyPI package's wheel (via `pip download`, not `pip install` - that avoids
clobbering the `clang` bindings package already in this venv, since both
packages occupy the same `clang/` import path) and caches it under
`tools/reflectgen/.venv/libclang-fallback/` - downloaded once, reused on
every run after. No version pin on the download: `libclang`'s own PyPI
releases lag real LLVM versions (latest published is 18.1.1 as of
writing) - "latest compatible" in practice means "latest published",
since libclang's stable C ABI tolerates the mismatch the same way this
tool already tolerates bindings 21.x against a system libclang 19.1.0.
Requires network access the one time it downloads; a real system LLVM
install is still the more capable option (the bundled fallback is old
enough to hit more MSVC-STL-compatibility errors than the system install
does - see above).

## Usage

```
.venv\Scripts\python reflectgen.py <header_or_source.h/.cpp> [more files or directories...] -o <output.cpp>
```

Any input that's a directory is scanned recursively for `.h` files (pass
`--ext .hpp` etc. - repeatable - to scan other extensions instead of/in
addition to `.h`; pass `--no-recursive` to only scan that directory's top
level). Every class/enum found across every input, file or directory,
still lands in the single `-o` output:

```powershell
.venv\Scripts\python reflectgen.py ..\..\include\newui -o all.gen.cpp `
    -- "-Id:\code\newui\include" "-Id:\code\newui\3rdparty\json5\include" -std=c++17
```

Each matched file is parsed as its own standalone translation unit (not
through `newui.h`), so a header that relies on another header having
already pulled in a standard header for it (rather than including its own
dependencies directly) can fail to parse in isolation - reflectgen reports
that file's errors to stderr and moves on to the rest rather than aborting
the whole scan.

Extra clang args (include paths, `-std=...`) can be passed after `--`:

```powershell
.venv\Scripts\python reflectgen.py ..\..\examples\reflection1.cpp -o widget.gen.cpp `
    -- "-Id:\code\newui\include" "-Id:\code\newui\3rdparty\json5\include" -std=c++17
```

**Quote every `-I...` path in PowerShell.** An unquoted `-Id:\code\...`
argument that appears *after* a literal `--` gets split by PowerShell's
own tokenizer into two argv entries (`-Id:` and `\code\...`) before
reflectgen (or even Python) ever sees it - not a reflectgen bug, and not
reproducible in cmd.exe or with `--` omitted, only that specific
`-- <unquoted -I...>` combination in PowerShell. Symptom: `warning:
'linker' input unused` plus `fatal error: '...' file not found` for a
header that really is on the path you passed. Fix is either quoting
(`"-Id:\code\newui\include"`, as above) or using forward slashes
(`-Id:/code/newui/include`, which Clang accepts natively and PowerShell
doesn't mangle).

Only classes with a `NEWUI_REFLECT_PRIVATE()` in their body get their
private/protected members reflected; classes without it are still
scanned, but only their public members are emitted.

A class/struct can opt out of generation entirely with a `// @reflect
ignore=true` comment directly above its declaration:

```cpp
// @reflect ignore=true
class NotReflectable {
    ...
};
```

(`-fparse-all-comments` is passed to clang automatically so a plain `//`
comment like this is visible at all - libclang only attaches a
doxygen-style comment, `///`/`/** */`/`//!`, to a cursor by default.)
