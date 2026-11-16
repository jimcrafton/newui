# scriptgen

scriptgen reads C++ headers with libclang and writes [pybind11](https://github.com/pybind/pybind11) bindings for the
classes you mark with `// @scriptable`, plus a `.pyi` type-stub file for editors. It is the Python-binding twin of
reflectgen: reflectgen turns marked headers into `newui::reflection` registrations, scriptgen turns them into
Python bindings. Both are built on the same header parser (`tools/reflectgen_lib`) and use the same venv
(`tools/reflectgen/.venv`, see `tools/reflectgen/README.md` "Setup").

You write the C++ once. scriptgen keeps the Python API in step with it, so a new public method appears in Python on the
next build and nobody maintains a second list of bindings by hand.

Contents: [Quick start](#quick-start) · [Calling scriptgen](#calling-scriptgen) · [Marking code](#marking-code) ·
[How it works](#how-it-works) · [How it uses pybind11](#how-it-uses-pybind11) · [What it emits](#what-it-emits) ·
[Using the output](#using-the-output) · [Limitations](#limitations) · [Checking a change](#checking-a-change)

## Quick start

1. Mark a class (the comment must be directly above it):

   ```cpp
   // @scriptable
   class Circle {
   public:
       explicit Circle(double radius, const std::string& label = "circle");
       double area() const;
       void setRadius(double radius);
   };
   ```

2. Generate (or let CMake do it, see below):

   ```
   tools\reflectgen\.venv\Scripts\python tools\scriptgen\scriptgen.py include\mylib -o build\bindings.cpp ^
       --stubs build\mylib.pyi --marker-only -- -std=c++20 -Iinclude
   ```

3. Compile `bindings.cpp` with pybind11 and call the generated function from your module:

   ```cpp
   void registerScripting(pybind11::module_& m);              // defined in bindings.cpp
   PYBIND11_MODULE(mylib, m) { registerScripting(m); }
   ```

   ```python
   import mylib
   c = mylib.Circle(2.0)
   c.area()
   c.set_radius(3.0)          # camelCase becomes snake_case
   ```

## Calling scriptgen

### Command line

```
python scriptgen.py <headers or directories...> -o <bindings.cpp> [options] [-- <clang arguments>]
```

| Option | Meaning |
|---|---|
| `inputs` | Header files and/or directories (searched recursively for `*.h`). Relative paths are made absolute. |
| `-o FILE` | The generated C++ file. |
| `--stubs FILE` | Also write a `.pyi` stub file. |
| `--register-function NAME` | Name of the generated function (default `registerScripting`). |
| `--marker-only` | Only read input files that contain `@scriptable`. The other headers are still seen through `#include`, so a marked class can use their types. Use this when scanning a whole include folder. |
| `--strict` | A warning (a member that could not be bound) makes the run fail. Good for CI. |
| `--prelude HEADER` | A header `#include`d before the inputs while parsing (repeatable). Needed when your headers are not self-contained. |
| `--include HEADER` | An extra `#include "HEADER"` written into the generated file (repeatable). |
| `--ext .EXT`, `--no-recursive` | Which files a directory input yields, and whether to descend. |
| `-- ...` | Everything after `--` goes to libclang: `-std=c++20`, `-I<dir>`, `-D<macro>`. libclang reads the headers, so give it the include paths and language standard your headers need. `-std=c++17` is the default. |

A run prints one line (`wrote ... N class(es): ...; M opaque: ...; K enum(s)`) and a `scriptgen: ... not bound: <reason>`
line for every member it had to skip. If no input contains a marker it writes a valid empty function, so the build still works.

### CMake

```cmake
include(<newui>/cmake/ScriptGen.cmake)

newui_add_scriptgen_output(mylib_python
    SCAN_DIRS     ${CMAKE_CURRENT_SOURCE_DIR}/include/mylib
    INCLUDE_DIRS  ${CMAKE_CURRENT_SOURCE_DIR}/include
    CXX_STANDARD  20
    REGISTER_FUNCTION registerMylibScripting
    STUBS         ${CMAKE_CURRENT_BINARY_DIR}/mylib/_core.pyi
    STRICT)
```

It adds a custom command that regenerates `<target>_scripting_generated.cpp` whenever a scanned header or the tool changes,
and adds that file to the target's sources. The target must be linked against pybind11 (`pybind11::pybind11` for a
library, `pybind11::module` or `pybind11::embed` for the final module or program). `SCAN_DIRS` may list directories or
single headers; `--marker-only` is always on. Requires the reflectgen venv (the function stops with a message if it is missing).

## Marking code

Markers are ordinary comments directly above the declaration, in the same style as reflectgen's `@reflect`
(`// @scriptable`, `//@scriptable`, or a `/* */` block all work). A marker line can carry both tools' keys:
`// @reflect category=basic @scriptable holder=shared`. Each tool only reads the keys after its own marker.

| Marker | On | Effect |
|---|---|---|
| `// @scriptable` | class / struct | Bind the class and every public member. |
| `// @scriptable` | enum | Bind the enum (an enum named by a bound signature is bound anyway). |
| `// @scriptable holder=shared` | class | Use `std::shared_ptr` as the pybind11 holder instead of the default `std::unique_ptr`. |
| `// @scriptable name=Foo` | class, enum, method, field | The Python name. Methods and fields default to snake_case (`setFloat` becomes `set_float`). |
| `// @scriptable ignore=true` | member | Leave this member out. |
| `// @scriptable ignore=true` | class / enum | Never bind this type, and silently leave out every member whose signature names it. Use it for internals (thread pools, writers). |
| `// @scriptable readonly` | class | Bind only the const and static methods, make every field read-only and drop property setters. For a class whose objects Python receives shared with something that must not see them change (a cached result handed out as `shared_ptr<const T>`): Python has no `const`, so without this a script could call any mutating method on the shared object. |
| `// @scriptable release_gil` | method | Call it with the Python GIL released (long running work, or work that calls back into Python from other threads). |
| `// @scriptable policy=reference` | method | A pybind11 `return_value_policy` instead of the automatic one. |
| `// @scriptable property=name setter=setX` | getter method | Expose a Python property; the two methods are then not also exposed as methods. `setter=` is optional. |

A class is bound only if it is marked (or something marked needs it, see below). Everything else in your headers is ignored.

## How it works

```
 headers ──libclang──▶ one translation unit ──▶ index of every class/enum ──▶ marked classes ──▶ model ──▶ .cpp + .pyi
            (reflectgen_lib)                       (scriptcollect.Collector)     + what they need   (scriptgen.py)
```

1. **Parse.** `reflectgen_lib.driver.translation_unit()` writes a temporary `.cpp` that `#include`s the prelude headers
   and every input file, and parses that one translation unit with libclang (the same approach reflectgen uses: a header
   that relies on another header having been included first still parses). A parse error stops the run. The temporary
   file is removed afterwards.
2. **Index.** `Collector.index_declarations()` walks the AST (skipping system headers) and records every class,
   struct and enum definition by qualified name, including nested ones, and which ones are marked `ignore=true` (hidden).
3. **Select.** Classes and enums that carry `@scriptable` *and* live in one of the input files are the marked set.
   Reading comments needs `-fparse-all-comments`, which the driver adds.
4. **Collect members.** For each marked class the collector walks its public members and builds a small model:
   constructors, methods, static methods, fields, static fields, properties, nested enums. Each signature is checked
   (`type_refs()`) for whether pybind11 can convert every type in it, and records which classes and enums it names.
   A member that fails is skipped with a warning. The members' rules are under [Limitations](#limitations).
5. **Resolve what is needed.** Public base classes are always bound (pybind11 requires it). A class or enum named by a kept member
   is bound too, as an **opaque** class (no members) if it is not marked itself, so the signature still works and Python can
   pass the object around. A type that is not found in the parsed headers makes the member that names it be skipped.
6. **Holders.** A class named inside `std::shared_ptr<...>` gets the shared holder, a class marked `holder=shared` too,
   and classes related by inheritance must agree, so the choice is propagated up and down the hierarchy.
7. **Order and emit.** Bases are emitted before derived classes. Then `scriptgen.py` writes the C++ and the stub file.

The code is in `scriptcollect.py` (steps 2 to 6) and `scriptgen.py` (the command line, step 7 and the stub writer);
steps 1 and the annotation reader are in `reflectgen_lib`.

## How it uses pybind11

scriptgen does not link pybind11 or Python: it only writes source text. The generated file uses these pybind11 features:

| C++ | Emitted pybind11 |
|---|---|
| bound class | `py::class_<T, Base..., Holder> cls(m, "Name");` |
| constructor | `.def(py::init<Args...>(), py::arg("x"), ...)`; none for an abstract class |
| method | `.def("snake_name", &T::method, py::arg(...))`, static ones with `.def_static` |
| overloaded method | the member pointer is cast to its own signature: `static_cast<R (T::*)(A) const>(&T::method)` |
| default arguments | one extra overload per shorter argument count, as a lambda that calls the C++ method (`[](const T& self_, int a) -> decltype(auto) { return self_.add(a); }`), so Python can leave the trailing arguments out |
| public field / static field | `.def_readwrite` / `.def_readonly` (const), and the `_static` forms |
| marked getter (and setter) | `.def_property("name", &T::get, &T::set)` or `.def_property_readonly` |
| enum | `py::enum_<E>(m, "E").value("a", E::a)...`, plus `.export_values()` for an unscoped enum; a nested enum is bound on its class: `py::enum_<T::E>(cls, "E")` |
| `release_gil` | `py::call_guard<py::gil_scoped_release>()` |
| returned reference or pointer to a bound class | `py::return_value_policy::reference_internal` (`reference` for a static method), so the owner is kept alive while Python holds the result |
| `std::vector`, `map`, `set`, `optional`, `pair`, `tuple`, `variant`, `string` ... | converted by `<pybind11/stl.h>` |
| `std::function` | converted by `<pybind11/functional.h>` (a Python callable can be passed) |
| `std::shared_ptr<T>` / `std::unique_ptr<T>` | the holder types |

Parameter and result types are spelled in their **canonical** form (`std::basic_string<char>` for `std::string`,
`unsigned int` for a typedef'd id). That is verbose but always valid in the generated file, which therefore needs no
`using namespace`. The generated file includes `<pybind11/pybind11.h>`, `stl.h`, `functional.h` and every header that
defines something it binds (spelled relative to the `-I` directories you passed, otherwise as an absolute path).

## What it emits

For a header like the quick-start example the generated file looks like this (shortened):

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <mylib/circle.h>

namespace py = pybind11;

void registerScripting(py::module_& m)
{
    {
        py::class_<mylib::Circle> cls(m, "Circle");
        cls
            .def(py::init<double, const std::basic_string<char> &>(), py::arg("radius"), py::arg("label"))
            .def(py::init<double>(), py::arg("radius"))
            .def("area", &mylib::Circle::area)
            .def("set_radius", &mylib::Circle::setRadius, py::arg("radius"));
    }
}
```

It defines exactly one function (`registerScripting` unless you rename it) that adds the enums first, then the classes in
base-before-derived order. Python names are the C++ class names; methods and parameters are snake_case. The stub file
(`--stubs`) declares the same classes in Python syntax so editors can complete and type-check:

```python
class Circle:
    @overload
    def __init__(self, radius: float, label: str = ...) -> None: ...
    def area(self) -> float: ...
    def set_radius(self, radius: float) -> None: ...
```

Types map as `int`, `float`, `bool`, `str`, `list[...]`, `dict[..., ...]`, `set[...]`, `tuple[...]`, `X | None`,
`Callable[..., Any]`, bound classes by name, and `Any` for what it cannot name. Overloads get `@overload`.

## Using the output

* **Extension module** (`import mylib` from any Python): `PYBIND11_MODULE(mylib, m) { registerScripting(m); }`, built with
  `pybind11_add_module`.
* **Embedded in an application**: `PYBIND11_EMBEDDED_MODULE(mylib, m) { registerScripting(m); }` and link `pybind11::embed`.
* **Behind a hand-written layer**: call it on a submodule (`m.def_submodule("_core")`) and put safe wrappers on top. That is
  what jitnodes does, because the generated classes are raw references into the C++ objects; see jitnodes'
  `docs/python.md`.

You can add to a generated class afterwards from hand-written code: look it up by name and `.def(...)` more members.

pybind11 3.x and Python 3.9 or newer are needed to compile the output. The generated code is plain C++17.

## Limitations

What scriptgen does **not** do, and what happens instead:

* **Only public members of marked classes.** Private and protected members are never bound.
* **No operators, templates or conversion functions.** Methods named `operator...`, function templates and members of class
  templates are skipped silently. Bind them by hand if you need them.
* **Parameters it cannot pass are skipped with a warning:** rvalue references; non-const references to non-class types
  (out parameters, `int&`, `std::vector<float>&`); raw pointers to pointers, to `void` and to numbers (`float*`); by-value classes
  that cannot be copied; `std` types pybind11 does not convert (`std::span`, `std::atomic`, ...); and any signature that names a
  class or enum that is not in the parsed headers. The message names the member and the reason; `--strict` turns these into failures.
* **Default argument values are not copied.** They are reproduced by calling the shorter C++ overload, so the Python signature
  shows `= ...` and the real default stays in C++. Keyword names are the snake_case form of the C++ parameter names.
* **Python cannot override virtual methods.** There is no trampoline class, so a Python subclass of a bound class does not change
  what C++ calls. Abstract classes are bound without constructors (they cannot be created from Python).
* **Unmarked dependencies are opaque.** A class named in a signature but not marked has no methods in Python. Mark it (or
  mark the members you want) to give it an API. Mark `ignore=true` to hide it and everything that mentions it.
* **const and non-const overloads become one method** (the non-const one), since Python has no const. scriptgen does not
  otherwise track constness across the boundary: a `const T&` or `shared_ptr<const T>` you return reaches Python as an ordinary
  mutable object. Mark such a class `readonly`, or hide its mutating members.
* **No docstrings.** The stub file and the C++ header are the documentation.
* **Nested classes** are bound at module level under their short name, only when something needs them; they are not
  nested in Python, and their members are not bound unless the nested class is itself marked.
* **Holders are per class, not per call site.** A class used as `shared_ptr` in one place and `unique_ptr` in another is bound with
  the shared holder, and a function that hands over a `std::unique_ptr<T>` of it will not work (pybind11 rejects it when the
  bindings are compiled or at run time). Give such a class one ownership style.
* **Lifetime is the caller's problem.** scriptgen cannot know who owns an object. References returned to Python are kept
  alive only as far as `reference_internal` goes; if the owner can destroy the object while Python still holds it,
  put a handle layer on top (jitnodes does) or `ignore` the member.
* **Not run or verified for:** multiple inheritance with several bound bases, bit-field members, nested namespaces with the same
  class name in two places (Python names are the short names and would clash), and `-std` below C++17.

## Checking a change

`unittests/test_scriptgen.py` runs the tool over `unittests/sample/sample.h` and checks the text it generates (abstract classes,
holders, hidden types, defaults, overload casts, properties, enums, stubs, `--strict`, the empty case):

```
tools\reflectgen\.venv\Scripts\python -m unittest discover -s tools\scriptgen\unittests
```

Whether generated bindings compile and behave is checked by building them: generate from the sample, compile the output together with
`PYBIND11_MODULE(demo, m) { registerScripting(m); }` using `cl /std:c++17 /EHsc /LD` against pybind11 and Python 3.13 (x64), and
import the resulting `.pyd`. The jitnodes Python module (`3rdparty/jitnodes/python`) is the large real-world check: scriptgen runs
over the whole `include/jitnodes` tree with `--strict`, and its Python and embedded tests exercise the result.

Changes to `reflectgen_lib` must keep reflectgen's output identical: regenerate newui's and any downstream registration
file before and after and compare them byte for byte, and run `tools/reflectgen/unittests`.
