"""Unit tests for scriptgen: the name helpers, and a real run over unittests/sample/sample.h whose generated
text is checked. Run with the reflectgen venv:  .venv\\Scripts\\python -m unittest discover -s ..\\scriptgen\\unittests

Whether the generated bindings compile and work is checked by hand (see README.md "Checking a change")."""

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.path.dirname(TOOL_DIR))
sys.path.insert(0, TOOL_DIR)

from scriptcollect import python_identifier, snake_case


class NameTest(unittest.TestCase):
    def test_snake_case(self):
        self.assertEqual(snake_case("setFloat"), "set_float")
        self.assertEqual(snake_case("cookDisplay"), "cook_display")
        self.assertEqual(snake_case("toJSONText"), "to_json_text")
        self.assertEqual(snake_case("already_snake"), "already_snake")
        self.assertEqual(snake_case("x"), "x")

    def test_python_keywords_get_a_trailing_underscore(self):
        self.assertEqual(python_identifier("from"), "from_")
        self.assertEqual(python_identifier("name"), "name")


def read_text(path):
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8") as f:
        return f.read()


def run_scriptgen(inputs, extra=()):
    with tempfile.TemporaryDirectory() as temp:
        output = os.path.join(temp, "out.cpp")
        stubs = os.path.join(temp, "out.pyi")
        command = [sys.executable, os.path.join(TOOL_DIR, "scriptgen.py"), *inputs, "-o", output, "--stubs", stubs,
                   *extra, "--", "-std=c++17"]
        result = subprocess.run(command, capture_output=True, text=True)
        return result, read_text(output), read_text(stubs)


class SampleRunTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result, cls.cpp, cls.stub = run_scriptgen([os.path.join(HERE, "sample")], ["--marker-only"])

    def test_it_succeeds(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)

    def test_a_bare_marker_binds_the_class_with_its_members(self):
        self.assertIn('py::class_<demo::Shape, std::shared_ptr<demo::Shape>> cls(m, "Shape");', self.cpp)
        self.assertIn('.def("name", &demo::Shape::name)', self.cpp)

    def test_an_abstract_class_gets_no_constructors(self):
        self.assertNotIn("py::init<int>", self.cpp)

    def test_a_base_class_and_a_holder_follow_the_marked_class(self):
        self.assertIn('py::class_<demo::Circle, demo::Shape, std::shared_ptr<demo::Circle>> cls(m, "Circle");', self.cpp)
        self.assertLess(self.cpp.index('"Shape")'), self.cpp.index('"Circle")'))

    def test_a_class_a_signature_names_is_bound_empty(self):
        self.assertIn('py::class_<demo::Hidden> cls(m, "Hidden");', self.cpp)
        self.assertNotIn("Hidden::secret", self.cpp)

    def test_enums_are_bound_at_module_level_and_nested(self):
        self.assertIn('py::enum_<demo::Mode>(m, "Mode")', self.cpp)
        self.assertIn('.value("LegacyB", demo::LegacyB)', self.cpp)
        self.assertIn(".export_values()", self.cpp)
        self.assertIn('py::enum_<demo::Circle::Kind>(cls, "Kind")', self.cpp)

    def test_default_arguments_get_shorter_overloads(self):
        self.assertIn('.def("add", &demo::Circle::add, py::arg("a"), py::arg("b"))', self.cpp)
        self.assertIn("return self_.add(a);", self.cpp)
        self.assertIn(".def(py::init<double>(), py::arg(\"radius\"))", self.cpp)

    def test_overloaded_methods_are_cast_to_their_signature(self):
        self.assertIn("static_cast<int (demo::Circle::*)(int)>(&demo::Circle::scale)", self.cpp)
        self.assertIn("static_cast<float (demo::Circle::*)(float)>(&demo::Circle::scale)", self.cpp)

    def test_a_const_and_non_const_twin_is_bound_once(self):
        self.assertEqual(self.cpp.count('.def("hidden"'), 1)
        self.assertIn("static_cast<demo::Hidden & (demo::Circle::*)()>(&demo::Circle::hidden)", self.cpp)

    def test_parameter_names_are_snake_case_for_python(self):
        self.assertIn('py::arg("width_scale"), py::arg("step_count")', self.cpp)
        self.assertIn("def stretch(self, width_scale: float, step_count: int = ...) -> float: ...", self.stub)
        self.assertIn("double widthScale", self.cpp)

    def test_a_returned_reference_keeps_the_owner_alive(self):
        self.assertIn("(&demo::Circle::hidden), py::return_value_policy::reference_internal)", self.cpp)

    def test_property_name_static_and_const_members(self):
        self.assertIn('.def_property("radius", &demo::Circle::radius, &demo::Circle::setRadius)', self.cpp)
        self.assertIn('.def_readwrite("renamed_value", &demo::Circle::counter)', self.cpp)
        self.assertIn('.def_readwrite_static("instances", &demo::Circle::instances)', self.cpp)
        self.assertIn('.def_readonly("fixed", &demo::Circle::fixed)', self.cpp)

    def test_ignored_and_unbindable_members_are_left_out_with_a_warning(self):
        self.assertNotIn("notExposed", self.cpp)
        self.assertNotIn("outParameter", self.cpp)
        self.assertIn("outParameter' not bound", self.result.stderr)

    def test_an_out_parameter_is_returned_after_the_result(self):
        self.assertIn('.def("range", [](const demo::Circle& self_, const std::basic_string<char> & key) '
                      '{ float low{}; float high{}; auto result_ = self_.range(key, low, high); '
                      'return std::make_tuple(std::move(result_), low, high); }, py::arg("key"))', self.cpp)
        self.assertIn("def range(self, key: str) -> tuple[bool, float, float]: ...", self.stub)

    def test_a_lone_out_parameter_of_a_void_method_is_returned_bare(self):
        self.assertIn("{ int total{}; self_.sum(a, total, b); return total; }", self.cpp)
        self.assertIn("{ int total{}; self_.sum(a, total); return total; }", self.cpp)
        self.assertIn("def sum(self, a: int, b: int = ...) -> int: ...", self.stub)

    def test_a_static_method_can_have_an_out_parameter(self):
        self.assertIn('.def_static("lookup", [](int id) { std::basic_string<char> name{}; '
                      'auto result_ = demo::Circle::lookup(id, name); return std::make_tuple(std::move(result_), name); }, py::arg("id"))', self.cpp)
        self.assertIn("def lookup(id: int) -> tuple[int, str]: ...", self.stub)

    def test_an_unmarked_reference_is_still_refused(self):
        self.assertNotIn("appendTo", self.cpp)
        self.assertIn("appendTo' not bound: takes a non-const reference", self.result.stderr)
        self.assertNotIn("range' not bound", self.result.stderr)

    def test_a_readonly_class_binds_only_what_cannot_change_it(self):
        self.assertIn('.def("total", &demo::Ledger::total)', self.cpp)
        self.assertNotIn("Ledger::add", self.cpp)
        self.assertIn('.def_readonly("note", &demo::Ledger::note)', self.cpp)
        self.assertNotIn("def_readwrite(\"note\"", self.cpp)

    def test_a_hidden_type_and_the_members_naming_it_are_left_out_silently(self):
        self.assertNotIn("Internal", self.cpp)
        self.assertNotIn("touch", self.cpp)
        self.assertNotIn("internals", self.cpp)
        self.assertNotIn("Internal", self.result.stderr)

    def test_the_stub_has_overloads_defaults_and_python_types(self):
        self.assertIn("class Circle(Shape):", self.stub)
        self.assertIn("    @overload\n    def scale(self, v: int) -> int: ...", self.stub)
        self.assertIn("def add(self, a: int, b: int = ...) -> int: ...", self.stub)
        self.assertIn("def maybe(self, present: bool) -> str | None: ...", self.stub)
        self.assertIn("def numbers(self) -> list[int]: ...", self.stub)
        self.assertIn("def unit() -> Circle: ...", self.stub)


class OptionsTest(unittest.TestCase):
    def test_strict_fails_on_a_warning(self):
        result, _cpp, _stub = run_scriptgen([os.path.join(HERE, "sample")], ["--marker-only", "--strict"])
        self.assertNotEqual(result.returncode, 0)

    def test_no_marked_header_writes_an_empty_function(self):
        with tempfile.TemporaryDirectory() as temp:
            with open(os.path.join(temp, "plain.h"), "w", encoding="utf-8") as f:
                f.write("class Plain {};\n")
            result, cpp, _stub = run_scriptgen([temp], ["--marker-only", "--register-function", "bindNothing"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("void bindNothing(pybind11::module_&) {}", cpp)


if __name__ == "__main__":
    unittest.main()
