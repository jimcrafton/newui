"""Unit tests for reflectgen.py's own logic - the pure/near-pure helpers
(naming heuristics, type-spelling normalization) directly, plus the small
handful of clang-Type/-Cursor-dependent helpers via real (but tiny,
standalone) parsed snippets - not a test of any *generated* registration
.cpp's own behavior (that's unittests/test_reflection.cpp's job, against
real newui headers via the actual CMake pipeline).

Run from tools/reflectgen (needs the .venv set up - see README.md's
"Setup"):

    .venv\\Scripts\\python -m unittest unittests.test_reflectgen -v

or all tests under unittests/ in one go:

    .venv\\Scripts\\python -m unittest discover -s unittests -p "test_*.py" -v
"""

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import reflectgen as rg
import clang.cindex as cindex

# clang.cindex.Config.set_library_file() (inside configure_libclang()) can
# only be called once per process - a second call raises, so this has to
# happen exactly once at import time, not per test class.
rg.configure_libclang()


# ---------------------------------------------------------------------------
# Pure string-logic helpers - no clang objects involved at all.
# ---------------------------------------------------------------------------

class StripAccessorPrefixTest(unittest.TestCase):
    def test_camel_case_boundary(self):
        self.assertEqual(rg.strip_accessor_prefix("getTitle", "get"), ("Title", True))

    def test_snake_case_boundary(self):
        self.assertEqual(rg.strip_accessor_prefix("get_title", "get"), ("title", True))

    def test_case_insensitive_prefix(self):
        self.assertEqual(rg.strip_accessor_prefix("GETTITLE", "get"), ("TITLE", True))

    def test_no_coincidental_substring_match(self):
        # "isolate" is not "is" + "olate" - no boundary right after "is".
        self.assertEqual(rg.strip_accessor_prefix("isolate", "is"), ("isolate", False))
        self.assertEqual(rg.strip_accessor_prefix("setup", "set"), ("setup", False))

    def test_no_prefix_at_all(self):
        self.assertEqual(rg.strip_accessor_prefix("title", "get"), ("title", False))

    def test_bare_prefix_alone_never_matches(self):
        # "get" alone has no character after the prefix to form a boundary.
        self.assertEqual(rg.strip_accessor_prefix("get", "get"), ("get", False))


class GetterStemTest(unittest.TestCase):
    def test_get_prefixed(self):
        self.assertEqual(rg.getter_stem("getTitle"), "Title")

    def test_snake_case_get_prefixed(self):
        self.assertEqual(rg.getter_stem("get_title"), "title")

    def test_is_prefixed(self):
        self.assertEqual(rg.getter_stem("isVisible"), "Visible")

    def test_has_prefixed(self):
        self.assertEqual(rg.getter_stem("hasPermission"), "Permission")

    def test_bare_getter_capitalizes(self):
        self.assertEqual(rg.getter_stem("bounds"), "Bounds")


class DerivePropertyKeyTest(unittest.TestCase):
    def test_get_prefixed_lowercases_first_letter(self):
        self.assertEqual(rg.derive_property_key("getTitle"), "title")

    def test_bare_getter_round_trips_unchanged(self):
        self.assertEqual(rg.derive_property_key("bounds"), "bounds")

    def test_is_prefixed(self):
        self.assertEqual(rg.derive_property_key("isVisible"), "visible")


class AccessorStemTest(unittest.TestCase):
    def test_normalizes_different_spellings_to_the_same_stem(self):
        stems = {
            rg.accessor_stem("getTitle", rg._GETTER_PREFIXES),
            rg.accessor_stem("get_title", rg._GETTER_PREFIXES),
            rg.accessor_stem("GETTITLE", rg._GETTER_PREFIXES),
        }
        self.assertEqual(stems, {"title"})

    def test_bare_name_is_its_own_stem(self):
        self.assertEqual(rg.accessor_stem("bounds", rg._GETTER_PREFIXES), "bounds")

    def test_setter_prefix_list(self):
        self.assertEqual(rg.accessor_stem("setTitle", rg._SETTER_PREFIXES), "title")


class HasAccessorPrefixTest(unittest.TestCase):
    def test_true_for_a_real_prefix(self):
        self.assertTrue(rg.has_accessor_prefix("getTitle", rg._GETTER_PREFIXES))

    def test_false_for_bare_name(self):
        self.assertFalse(rg.has_accessor_prefix("bounds", rg._GETTER_PREFIXES))

    def test_false_for_coincidental_substring(self):
        self.assertFalse(rg.has_accessor_prefix("isolate", rg._GETTER_PREFIXES))


class NormalizeTypeSpellingTest(unittest.TestCase):
    def test_strips_const_ref(self):
        self.assertEqual(rg.normalize_type_spelling("const std::string &"), "std::string")

    def test_strips_pointer(self):
        self.assertEqual(rg.normalize_type_spelling("LayoutParams *"), "LayoutParams")

    def test_plain_value_unchanged_besides_whitespace(self):
        self.assertEqual(rg.normalize_type_spelling("int"), "int")

    def test_const_and_bare_forms_match(self):
        self.assertEqual(
            rg.normalize_type_spelling("const std::string &"),
            rg.normalize_type_spelling("std::string"),
        )

    def test_does_not_touch_const_inside_a_template_argument_name(self):
        # "const" is only stripped as a whole word - a hypothetical type
        # spelling containing "const" as a substring of an identifier
        # (not a real C++ keyword occurrence, contrived for the test)
        # should survive untouched.
        self.assertEqual(rg.normalize_type_spelling("ConstantHolder"), "ConstantHolder")


class EnclosingNamespaceTest(unittest.TestCase):
    def test_namespaced_name(self):
        self.assertEqual(rg.enclosing_namespace("newui::gfx::Image"), "newui::gfx")

    def test_single_level_namespace(self):
        self.assertEqual(rg.enclosing_namespace("newui::Color"), "newui")

    def test_global_scope_name_returns_none(self):
        self.assertIsNone(rg.enclosing_namespace("GlobalWidget"))


class HasMatchingBackingMemberTest(unittest.TestCase):
    class _FakeType:
        def __init__(self, spelling):
            self.spelling = spelling

    class _FakeField:
        def __init__(self, spelling, type_spelling):
            self.spelling = spelling
            self.type = HasMatchingBackingMemberTest._FakeType(type_spelling)

    def _field(self, name, type_spelling):
        return self._FakeField(name, type_spelling)

    def test_matches_trailing_underscore_convention(self):
        fields = [self._field("bounds_", "Rect")]
        self.assertTrue(rg.has_matching_backing_member("bounds", "Rect", fields))

    def test_matches_leading_underscore_convention(self):
        fields = [self._field("_bounds", "Rect")]
        self.assertTrue(rg.has_matching_backing_member("bounds", "Rect", fields))

    def test_matches_m_prefix_convention(self):
        fields = [self._field("m_bounds", "Rect")]
        self.assertTrue(rg.has_matching_backing_member("bounds", "Rect", fields))

    def test_type_mismatch_does_not_match(self):
        fields = [self._field("bounds_", "Size")]
        self.assertFalse(rg.has_matching_backing_member("bounds", "Rect", fields))

    def test_qualifiers_on_either_side_are_ignored(self):
        fields = [self._field("bounds_", "Rect")]
        self.assertTrue(rg.has_matching_backing_member("bounds", "const Rect &", fields))

    def test_no_matching_name_at_all(self):
        fields = [self._field("otherField_", "Rect")]
        self.assertFalse(rg.has_matching_backing_member("bounds", "Rect", fields))

    def test_empty_fields_list(self):
        self.assertFalse(rg.has_matching_backing_member("bounds", "Rect", []))


# ---------------------------------------------------------------------------
# clang.cindex Type/Cursor-dependent helpers - parsed from small, self-
# contained snippets (no newui headers involved) rather than mocked, since
# these functions' whole job is reasoning about real clang Type/Cursor
# shapes (template arguments, declaration contexts, deleted special
# members) that aren't worth hand-faking.
# ---------------------------------------------------------------------------

def _find(cursor, predicate):
    for child in cursor.get_children():
        if predicate(child):
            return child
        found = _find(child, predicate)
        if found is not None:
            return found
    return None


def _find_named(cursor, name):
    return _find(cursor, lambda c: c.spelling == name)


class ClangSnippetTestCase(unittest.TestCase):
    """Base class that parses `SOURCE` once per subclass and exposes `tu`/
    `find(name)` to test methods - real clang parsing is slow enough
    (~tens of ms, more with STL headers) that sharing one parse per test
    class instead of per test method keeps the whole suite fast."""

    SOURCE = ""
    EXTRA_ARGS = ()

    @classmethod
    def setUpClass(cls):
        if not cls.SOURCE:
            raise unittest.SkipTest("no SOURCE set")
        cls._tmp_dir = tempfile.mkdtemp(prefix="reflectgen_test_")
        path = os.path.join(cls._tmp_dir, "snippet.cpp")
        with open(path, "w", encoding="utf-8") as f:
            f.write(cls.SOURCE)
        args = [
            "-std=c++17",
            "-x", "c++",
            "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
            "-fparse-all-comments",
        ]
        args.extend(cls.EXTRA_ARGS)
        index = cindex.Index.create()
        cls.tu = index.parse(path, args=args)
        errors = [d for d in cls.tu.diagnostics if d.severity >= d.Error]
        if errors:
            raise AssertionError(
                f"{cls.__name__}: snippet failed to parse: "
                + "; ".join(str(d) for d in errors)
            )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls._tmp_dir, ignore_errors=True)

    def find(self, name):
        cursor = _find_named(self.tu.cursor, name)
        self.assertIsNotNone(cursor, f"no cursor named {name!r} found in parsed snippet")
        return cursor


class QualifyTypeSpellingTest(ClangSnippetTestCase):
    SOURCE = """
    namespace newui {
        class SyncReturn {
        public:
            enum class ReturnCode { Handled, Ignored };
            void takesBareEnum(ReturnCode) {}
        };
        class ThemedTabItemStyle {
        public:
            enum class TabAlignment { Left, Right };
            // Partially qualified already (visible from outside the
            // class, unlike ReturnCode above) - only "newui::" itself is
            // missing from clang's own spelling.
            void takesPartiallyQualified(ThemedTabItemStyle::TabAlignment) {}
        };
        void takesBuiltin(int) {}
    }
    """

    def test_bare_nested_enum_gets_fully_qualified(self):
        fn = self.find("takesBareEnum")
        arg_type = next(fn.get_arguments()).type
        self.assertEqual(rg.qualify_type_spelling(arg_type), "newui::SyncReturn::ReturnCode")

    def test_partially_qualified_spelling_gets_only_the_missing_prefix(self):
        fn = self.find("takesPartiallyQualified")
        arg_type = next(fn.get_arguments()).type
        self.assertEqual(
            rg.qualify_type_spelling(arg_type),
            "newui::ThemedTabItemStyle::TabAlignment",
        )

    def test_builtin_type_passes_through_unchanged(self):
        # No declaration cursor at all for a builtin - qualify_type_spelling
        # must fall through to returning the spelling as-is rather than
        # choking on a missing/invalid declaration cursor.
        fn = self.find("takesBuiltin")
        arg_type = next(fn.get_arguments()).type
        self.assertEqual(rg.qualify_type_spelling(arg_type), "int")


class IsCopyConstructibleTest(ClangSnippetTestCase):
    SOURCE = """
    #include <vector>
    #include <memory>
    #include <string>

    class ExplicitlyDeletedCopy {
    public:
        ExplicitlyDeletedCopy() = default;
        ExplicitlyDeletedCopy(const ExplicitlyDeletedCopy&) = delete;
    };
    ExplicitlyDeletedCopy g_explicitlyDeletedCopy;

    class OrdinaryCopyable {
    public:
        int value = 0;
    };
    OrdinaryCopyable g_ordinaryCopyable;

    // Self-referential via a container of unique_ptr to itself - the real
    // MSVC std::is_copy_constructible_v<T> blind spot this session found
    // (see is_copy_constructible()'s own comment in reflectgen.py) -
    // reflectgen's own check needs to get this right independently of
    // whatever the real compiler's trait says.
    struct SelfReferential {
        std::vector<std::unique_ptr<SelfReferential>> children;
    };
    SelfReferential g_selfReferential;

    std::unique_ptr<std::string> g_uniquePtr;
    std::vector<std::unique_ptr<std::string>> g_vectorOfUniquePtr;
    std::vector<int> g_vectorOfInt;
    """

    def test_explicit_delete_is_not_copy_constructible(self):
        var = self.find("g_explicitlyDeletedCopy")
        self.assertFalse(rg.is_copy_constructible(var.type))

    def test_ordinary_class_is_copy_constructible(self):
        var = self.find("g_ordinaryCopyable")
        self.assertTrue(rg.is_copy_constructible(var.type))

    def test_self_referential_container_of_unique_ptr_is_not_copy_constructible(self):
        var = self.find("g_selfReferential")
        self.assertFalse(rg.is_copy_constructible(var.type))

    def test_unique_ptr_itself_is_never_copy_constructible(self):
        var = self.find("g_uniquePtr")
        self.assertFalse(rg.is_copy_constructible(var.type))

    def test_vector_of_unique_ptr_is_not_copy_constructible(self):
        var = self.find("g_vectorOfUniquePtr")
        self.assertFalse(rg.is_copy_constructible(var.type))

    def test_vector_of_copyable_element_is_copy_constructible(self):
        var = self.find("g_vectorOfInt")
        self.assertTrue(rg.is_copy_constructible(var.type))

    def test_scalar_type_is_always_copy_constructible(self):
        var = self.find("g_vectorOfInt")
        # int itself (not the vector) - reached via the vector's own
        # template argument, exercising the non-class-type early return.
        elem = var.type.get_canonical().get_template_argument_type(0)
        self.assertTrue(rg.is_copy_constructible(elem))


class ReflectableCollectionElementTypeTest(ClangSnippetTestCase):
    SOURCE = """
    #include <vector>
    #include <array>
    #include <map>
    #include <string>

    class Widget {};

    std::vector<Widget> g_vector;
    std::array<Widget, 4> g_array;
    std::map<std::string, Widget> g_map;
    int g_scalar;
    """

    def test_vector_element_type(self):
        var = self.find("g_vector")
        elem = rg.reflectable_collection_element_type(var.type)
        self.assertIsNotNone(elem)
        self.assertEqual(elem.spelling, "Widget")

    def test_array_element_type(self):
        var = self.find("g_array")
        elem = rg.reflectable_collection_element_type(var.type)
        self.assertIsNotNone(elem)
        self.assertEqual(elem.spelling, "Widget")

    def test_map_element_type_is_the_value_not_the_key(self):
        var = self.find("g_map")
        elem = rg.reflectable_collection_element_type(var.type)
        self.assertIsNotNone(elem)
        self.assertEqual(elem.spelling, "Widget")

    def test_non_container_type_returns_none(self):
        var = self.find("g_scalar")
        self.assertIsNone(rg.reflectable_collection_element_type(var.type))


class InvokeArgAndReturnTypeTest(ClangSnippetTestCase):
    SOURCE = """
    #include <memory>

    class Cursor {
    public:
        Cursor() = default;
        Cursor(const Cursor&) = delete;
    };

    class Widget {
    public:
        void takesConstRef(const Cursor&) {}
        void takesMutableRef(Cursor&) {}
        void takesByValueCopyable(int) {}
        void takesByValueNonCopyable(Cursor) {}
        Cursor& returnsMutableRef() { static Cursor c; return c; }
        Cursor* returnsPointer() { return nullptr; }
        int returnsByValue() { return 0; }
    };
    """

    def _arg_type(self, method_name):
        fn = self.find(method_name)
        return next(fn.get_arguments()).type

    def test_const_reference_arg_is_supported(self):
        self.assertFalse(rg.invoke_arg_type_unsupported(self._arg_type("takesConstRef")))

    def test_mutable_reference_arg_is_unsupported(self):
        self.assertTrue(rg.invoke_arg_type_unsupported(self._arg_type("takesMutableRef")))

    def test_by_value_copyable_arg_is_supported(self):
        self.assertFalse(rg.invoke_arg_type_unsupported(self._arg_type("takesByValueCopyable")))

    def test_by_value_non_copyable_arg_is_unsupported(self):
        self.assertTrue(rg.invoke_arg_type_unsupported(self._arg_type("takesByValueNonCopyable")))

    def test_has_unsupported_invoke_arg_true_for_mutable_ref_method(self):
        fn = self.find("takesMutableRef")
        self.assertTrue(rg.has_unsupported_invoke_arg(fn))

    def test_has_unsupported_invoke_arg_false_for_const_ref_method(self):
        fn = self.find("takesConstRef")
        self.assertFalse(rg.has_unsupported_invoke_arg(fn))

    def test_reference_return_of_non_copy_constructible_is_unsupported(self):
        fn = self.find("returnsMutableRef")
        self.assertTrue(rg.method_return_type_unsupported(fn.result_type))

    def test_pointer_return_is_always_supported(self):
        # A pointer return never needs the pointee to be copy-constructible
        # (std::any(ptr) just copies the pointer itself) - see
        # method_return_type_unsupported()'s own comment.
        fn = self.find("returnsPointer")
        self.assertFalse(rg.method_return_type_unsupported(fn.result_type))

    def test_by_value_return_is_always_supported(self):
        fn = self.find("returnsByValue")
        self.assertFalse(rg.method_return_type_unsupported(fn.result_type))


class DelegateHelpersTest(ClangSnippetTestCase):
    SOURCE = """
    namespace newui {
        template<typename SenderT, typename... Args>
        class Delegate {};

        class View {
        public:
            typedef Delegate<View, const int&> SizeChangedDelegate;
        };

        class RootView : public View {
        public:
            // Redeclares a field of the *base's* Delegate<View,...> type -
            // Sender is View, not RootView, even though this field lives
            // directly on RootView.
            SizeChangedDelegate onSizeChanged;
        };

        class RunLoop {
        public:
            // Non-const-reference Arg - can't be any_cast out of a
            // std::any at invoke time.
            Delegate<RunLoop, bool&> onIdle;
        };

        class Button {
        public:
            Delegate<Button> onClick;
        };

        int notADelegate;
    }
    """

    def test_is_delegate_field_type_true_for_real_delegate(self):
        field = _find_named(self.tu.cursor, "onClick")
        self.assertTrue(rg.is_delegate_field_type(field.type))

    def test_is_delegate_field_type_false_for_unrelated_type(self):
        field = _find_named(self.tu.cursor, "notADelegate")
        self.assertFalse(rg.is_delegate_field_type(field.type))

    def test_delegate_sender_matches_when_declared_on_the_same_class(self):
        field = _find_named(self.tu.cursor, "onClick")
        self.assertTrue(rg.delegate_sender_matches(field.type, "newui::Button"))

    def test_delegate_sender_does_not_match_inherited_base_sender(self):
        field = _find_named(self.tu.cursor, "onSizeChanged")
        self.assertFalse(rg.delegate_sender_matches(field.type, "newui::RootView"))
        # ... but it does match the class the typedef's Sender actually
        # names, confirming this is a real mismatch, not a lookup bug.
        self.assertTrue(rg.delegate_sender_matches(field.type, "newui::View"))

    def test_delegate_args_unsupported_true_for_mutable_reference_arg(self):
        field = _find_named(self.tu.cursor, "onIdle")
        self.assertTrue(rg.delegate_args_unsupported(field.type))

    def test_delegate_args_unsupported_false_for_no_args(self):
        field = _find_named(self.tu.cursor, "onClick")
        self.assertFalse(rg.delegate_args_unsupported(field.type))


class HasReflectFriendTest(ClangSnippetTestCase):
    SOURCE = """
    #define NEWUI_REFLECT_PRIVATE() friend struct ReflectFriendMarker

    class WithMarker {
        NEWUI_REFLECT_PRIVATE();
    private:
        int secret = 0;
    };

    class SomeUnrelatedFriend;
    class WithoutMarker {
        // A real, ordinary friend declaration for encapsulation reasons
        // completely unrelated to reflection - has_reflect_friend() must
        // not be fooled by this (the Part 40 bug this test guards
        // against - see has_reflect_friend()'s own comment).
        friend class SomeUnrelatedFriend;
    private:
        int secret = 0;
    };
    """

    def test_true_when_the_real_macro_is_present(self):
        cursor = self.find("WithMarker")
        self.assertTrue(rg.has_reflect_friend(cursor))

    def test_false_for_an_unrelated_friend_declaration(self):
        cursor = self.find("WithoutMarker")
        self.assertFalse(rg.has_reflect_friend(cursor))


class QualifiedNameTest(ClangSnippetTestCase):
    SOURCE = """
    namespace newui {
        namespace gfx {
            class Image {};
        }
        class Color {};
    }
    class GlobalWidget {};
    """

    def test_nested_namespace(self):
        cursor = self.find("Image")
        self.assertEqual(rg.qualified_name(cursor), "newui::gfx::Image")

    def test_single_namespace(self):
        cursor = self.find("Color")
        self.assertEqual(rg.qualified_name(cursor), "newui::Color")

    def test_global_scope(self):
        cursor = self.find("GlobalWidget")
        self.assertEqual(rg.qualified_name(cursor), "GlobalWidget")


class ReflectAnnotationsTagsTest(ClangSnippetTestCase):
    SOURCE = """
    #include <string>
    namespace newui {
        // @reflect tags=filepath,pathlike
        class Thing {
        public:
            // @reflect tags=urlvalue
            std::string getPath() const;
            void setPath(std::string);

            // @reflect ignore=true
            int getSkipped() const;
        };
    }
    """

    def test_class_level_tags_are_parsed_as_a_comma_joined_value(self):
        cursor = self.find("Thing")
        self.assertEqual(rg.reflect_annotations(cursor).get("tags"), "filepath,pathlike")

    def test_property_level_tags_are_parsed(self):
        cursor = self.find("getPath")
        self.assertEqual(rg.reflect_annotations(cursor).get("tags"), "urlvalue")

    def test_broadened_value_charset_does_not_break_a_plain_annotation(self):
        # REFLECT_ANNOTATION_PAIR_RE's value group now also allows "," (for
        # tags=...) - confirms an ordinary identifier-shaped value (no
        # comma at all) still parses exactly as before.
        cursor = self.find("getSkipped")
        self.assertEqual(rg.reflect_annotations(cursor).get("ignore"), "true")


class CollectClassTagsTest(ClangSnippetTestCase):
    SOURCE = """
    #include <string>
    namespace newui {
        template<typename SenderT, typename... Args>
        class Delegate {};

        // @reflect tags=file,pathlike
        class Document {
        public:
            // @reflect tags=filepath
            std::string getPath() const;
            void setPath(std::string);

            // @reflect tags=notification
            Delegate<Document> onChanged;
        };
    }
    """

    def test_class_level_tags_land_on_class_info(self):
        info = rg.collect_class(self.find("Document"))
        self.assertEqual(info.tags, ["file", "pathlike"])

    def test_property_level_tags_land_on_the_accessor(self):
        info = rg.collect_class(self.find("Document"))
        matching = [pa for pa in info.property_accessors if pa.key == "path"]
        self.assertEqual(len(matching), 1)
        self.assertEqual(matching[0].tags, ["filepath"])

    def test_delegate_level_tags_land_on_the_delegate_field(self):
        info = rg.collect_class(self.find("Document"))
        matching = [d for d in info.delegates if d.name == "onChanged"]
        self.assertEqual(len(matching), 1)
        self.assertEqual(matching[0].tags, ["notification"])

    def test_emitted_registration_includes_all_three_tags_calls(self):
        info = rg.collect_class(self.find("Document"))
        source = rg.emit_register_function(info, [])
        lines = source.splitlines()

        self.assertIn('.tags({"file", "pathlike"})', source)

        property_lines = [l for l in lines if '.property("path"' in l]
        self.assertEqual(len(property_lines), 1)
        self.assertIn('{"filepath"}', property_lines[0])

        delegate_lines = [l for l in lines if '.delegate("onChanged"' in l]
        self.assertEqual(len(delegate_lines), 1)
        self.assertIn('{"notification"}', delegate_lines[0])


if __name__ == "__main__":
    unittest.main()
