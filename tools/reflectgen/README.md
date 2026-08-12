# reflectgen (v1)

Scans a C++ header/source file with libclang and emits a `.cpp` that
registers each class it finds against `newui::reflection` (see
`include/newui/reflection.h`) - the same shape of code
`examples/reflection1.cpp` writes by hand for `Widget`.

v1 status: standalone script, run manually. Not wired into the CMake
build yet. Handles: public/private data members as properties,
`newui::Delegate<SenderT, Args...>` members (typedef'd or not) as
delegates, non-overloaded-or-disambiguated public methods, public
non-copy/move constructors, and enums (namespace-scope or public nested -
a private/protected nested enum's name isn't spellable from the free
`register...Enum()` function reflectgen emits, so those are skipped).
Static methods, templates, and operator overloads are explicitly out of
scope for now.

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

Only classes with a `NEWUI_REFLECT_FRIEND()` in their body get their
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
