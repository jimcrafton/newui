# scriptgen

Reads C++ headers with libclang and writes pybind11 bindings (and a `.pyi` stub file) for the classes
marked `// @scriptable`. It is the Python-binding twin of reflectgen and shares its header parser
(`tools/reflectgen_lib`) and its venv (`tools/reflectgen/.venv`, see that README's "Setup").

```
python scriptgen.py <headers or dirs...> -o bindings.cpp [--stubs module.pyi] [--register-function NAME]
                    [--marker-only] [--strict] [--prelude HEADER] [--include HEADER] -- -std=c++20 -I...
```

The output defines `void NAME(pybind11::module_& m)` (default `registerScripting`). Call it from a
`PYBIND11_EMBEDDED_MODULE` or `PYBIND11_MODULE` body. Compiling it needs pybind11 and the Python headers.
In CMake use `cmake/ScriptGen.cmake` (`newui_add_scriptgen_output`).

## What gets bound

A class is bound only when the comment directly above it contains `@scriptable`. Every public member of
a bound class is exposed unless it says `// @scriptable ignore=true`.

| Marker | Where | Effect |
|---|---|---|
| `// @scriptable` | class/struct | bind the class and all its public members |
| `// @scriptable holder=shared` | class | bind with `std::shared_ptr` instead of the default `std::unique_ptr` |
| `// @scriptable name=Foo` | class, enum, field, method | Python name (methods and fields are snake_case by default: `setFloat` becomes `set_float`) |
| `// @scriptable ignore=true` | member | leave it out |
| `// @scriptable ignore=true` | class or enum | never bind it, and silently leave out every member that names it (use it for internals such as thread pools and writers) |
| `// @scriptable` | enum | bind the enum (an enum a bound signature names is bound anyway) |
| `// @scriptable release_gil` | method | call with the GIL released |
| `// @scriptable policy=reference` | method | a pybind11 `return_value_policy` instead of the automatic one |
| `// @scriptable property=name setter=setX` | getter method | Python property (`setter=` is optional); the methods are not also exposed |

`@scriptable` and `@reflect` can share a comment (`// @reflect category=basic @scriptable`); each tool only
reads the keys after its own marker.

**Dependencies.** What a bound class needs is bound too, as an empty class when it is not marked itself:
its public base classes, and the classes and enums its signatures name (including inside `std::vector`,
`std::optional`, `std::shared_ptr`, `std::function` ...). They must be reachable from the scanned headers.
A class named inside `std::shared_ptr` gets the shared holder, and classes related by inheritance share
one holder. Public nested enums of a bound class become attributes of its Python class.

**Members that are skipped (with a warning; `--strict` fails the run):** operators, templates, rvalue
reference and non-const reference parameters (out parameters), pointers to pointers / void / numbers, by-value
parameters or results of classes that cannot be copied, `std` types pybind11 does not convert, and
signatures that name a class or enum outside the scanned headers. Everything else of a class
(constructors, static methods, public fields, static members) is bound.

**Details.** Parameter names become snake_case keyword names (`typeName` is `type_name`). Python has no
const, so a const and a non-const overload with the same parameters are bound once (the non-const one).
An abstract class gets no constructors. Overloaded methods are cast to their own signature. Default arguments are not copied: a method
with trailing defaults gets one extra overload per shorter argument count, so Python can leave them out.
A returned reference or pointer to a class uses `reference_internal` (`reference` for a static method), so the
object stays alive with its owner. Parameter types are spelled canonically (`std::basic_string<char>` for
`std::string`), so the generated file needs no `using namespace`.

## Checking a change

`unittests/test_scriptgen.py` runs the tool over `unittests/sample/sample.h` and checks the text:

```
tools\reflectgen\.venv\Scripts\python -m unittest discover -s tools\scriptgen\unittests
```

Whether the bindings compile and behave is checked by hand: generate from the sample, compile the output
plus a `PYBIND11_MODULE(demo, m) { registerScripting(m); }` file with `cl /std:c++17 /EHsc /LD` against
pybind11 and Python 3.13 (x64), and import the resulting `.pyd`.
