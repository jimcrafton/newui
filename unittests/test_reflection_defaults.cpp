// Default-value omission when writing: a property still at its default is left out of the document
// (Class::isDefaultValue(), reflection.h), so .newui files only carry what was actually changed.
// The default comes from a default-constructed instance of the *concrete* class (so a subclass
// constructor that changes a base property's default is honoured), unless the property was given an
// explicit Property::defaultVal().

// newui/newui.h must be the first include - see test_reflectionio.cpp's own comment.
#include "newui/newui.h"
#include "newui/reflection.h"
#include "newui/reflectionio.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/rootview.h"

#include <any>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace {

// A nested sub-object with no operator== - only reachable by value-recursion, never by compare.
struct Inner {
    Inner() = default;
    int level = 1;
    std::string tag = "t";
};

class Thing {
public:
    Thing() = default;
    virtual ~Thing() = default;

    int count = 5;                  // MemberPtr property
    std::vector<int> nums;          // collection (empty by default)
    Inner inner;                    // nested object, no operator==
    int limit = 7;                  // gets an explicit default

    std::string label() const { return label_; }
    void setLabel(const std::string& v) { label_ = v; }   // by-value getter/setter property

private:
    std::string label_ = "hi";
};

// Changes a *base* property's default in its constructor, like Slider does with View::visible.
class BigThing : public Thing {
public:
    BigThing() { count = 9; }
};

// A class with no default constructor registered: nothing to snapshot, so everything is written.
class Fussy {
public:
    explicit Fussy(int v) : value(v) {}
    int value = 3;
};

const Class* RegisterAll() {
    static const Class* thing = [] {
        {
            ClassBuilder<Inner> b;
            b.clazz().constructor<>()
                .property("level", Scope::Public, &Inner::level)
                .property("tag", Scope::Public, &Inner::tag);
            ReflectionRegistry::registerClass(b);
        }
        {
            ClassBuilder<Thing> b;
            b.clazz().constructor<>()
                .property("count", Scope::Public, &Thing::count)
                .property("nums", Scope::Public, &Thing::nums)
                .property("inner", Scope::Public, &Thing::inner)
                .property("limit", Scope::Public, &Thing::limit).defaultVal(std::any(7))
                .property("label", Scope::Public, &Thing::label, &Thing::setLabel);
            ReflectionRegistry::registerClass(b);
        }
        {
            ClassBuilder<BigThing> b;
            b.clazz().constructor<>().base<Thing>();
            ReflectionRegistry::registerClass(b);
        }
        {
            ClassBuilder<Fussy> b;
            b.clazz().property("value", Scope::Public, &Fussy::value);
            ReflectionRegistry::registerClass(b);
        }
        return classinfo(typeid(Thing));
    }();
    return thing;
}

template<typename T>
std::string writeText(T& obj, bool writeDefaults = false) {
    ObjectWriter writer;
    writer.setWriteDefaults(writeDefaults);
    writer.write(&obj);
    return json5::to_string(writer.doc);
}

bool has(const std::string& text, const std::string& key) {
    return text.find(key) != std::string::npos;
}

}  // namespace

TEST(ReflectionDefaults, UnchangedObjectWritesOnlyItsType) {
    RegisterAll();
    Thing t;
    std::string text = writeText(t);
    EXPECT_TRUE(has(text, "type: \"Thing\""));
    for (const char* key : {"count", "nums", "inner", "limit", "label"}) {
        EXPECT_FALSE(has(text, key)) << key << " should have been omitted:\n" << text;
    }
}

TEST(ReflectionDefaults, ChangedPropertiesAreWrittenAndOthersStillOmitted) {
    RegisterAll();
    Thing t;
    t.count = 6;
    t.setLabel("changed");
    std::string text = writeText(t);
    EXPECT_TRUE(has(text, "count: 6")) << text;
    EXPECT_TRUE(has(text, "label: \"changed\"")) << text;
    EXPECT_FALSE(has(text, "nums")) << text;
    EXPECT_FALSE(has(text, "inner")) << text;
}

TEST(ReflectionDefaults, WriteDefaultsTrueWritesEverything) {
    RegisterAll();
    Thing t;
    std::string text = writeText(t, true);
    for (const char* key : {"count", "nums", "inner", "limit", "label"}) {
        EXPECT_TRUE(has(text, key)) << key << " missing:\n" << text;
    }
}

TEST(ReflectionDefaults, SubclassConstructorDefaultIsTheDefault) {
    RegisterAll();
    BigThing big;   // count == 9 from its own constructor
    EXPECT_FALSE(has(writeText(big), "count")) << "9 is BigThing's default";

    big.count = 5;  // Thing's default, but not BigThing's - must be written
    EXPECT_TRUE(has(writeText(big), "count: 5"));

    Thing plain;
    plain.count = 9;  // BigThing's default, but not Thing's - must be written
    EXPECT_TRUE(has(writeText(plain), "count: 9"));
}

TEST(ReflectionDefaults, NestedObjectOmittedWhenFullyDefaultWrittenWhenChanged) {
    RegisterAll();
    Thing t;
    EXPECT_FALSE(has(writeText(t), "inner"));

    t.inner.level = 3;
    std::string text = writeText(t);
    EXPECT_TRUE(has(text, "inner")) << text;
    EXPECT_TRUE(has(text, "level: 3")) << text;
}

TEST(ReflectionDefaults, EmptyCollectionOmittedNonEmptyWritten) {
    RegisterAll();
    Thing t;
    EXPECT_FALSE(has(writeText(t), "nums"));

    t.nums = {1, 2};
    EXPECT_TRUE(has(writeText(t), "nums")) << writeText(t);
}

TEST(ReflectionDefaults, ExplicitDefaultValIsUsed) {
    const Class* clazz = RegisterAll();
    const Property* limit = clazz->property("limit");
    ASSERT_NE(limit, nullptr);
    EXPECT_TRUE(limit->defaultVal().has_value());
    EXPECT_EQ(std::any_cast<int>(limit->defaultVal()), 7);

    Thing t;
    EXPECT_FALSE(has(writeText(t), "limit"));  // 7 == the explicit default
    t.limit = 0;
    EXPECT_TRUE(has(writeText(t), "limit: 0"));
}

TEST(ReflectionDefaults, SetDefaultValOverridesTheSnapshot) {
    const Class* clazz = RegisterAll();
    Property* label = const_cast<Property*>(clazz->property("label"));
    ASSERT_NE(label, nullptr);

    Thing t;   // label_ == "hi", which equals the constructor default...
    label->setDefaultVal(std::any(std::string("other")));
    EXPECT_TRUE(has(writeText(t), "label: \"hi\""));   // ...but the explicit default now wins
    label->setDefaultVal(std::any());                   // restore for the other tests
    EXPECT_FALSE(has(writeText(t), "label"));
}

TEST(ReflectionDefaults, ValueComparisonCapability) {
    const Class* clazz = RegisterAll();
    EXPECT_TRUE(clazz->property("count")->canCompareValues());
    EXPECT_TRUE(clazz->property("label")->canCompareValues());
    EXPECT_FALSE(clazz->property("inner")->canCompareValues());   // Inner has no operator==
    EXPECT_TRUE(clazz->property("count")->valuesEqual(std::any(4), std::any(4)));
    EXPECT_FALSE(clazz->property("count")->valuesEqual(std::any(4), std::any(5)));
    EXPECT_FALSE(clazz->property("count")->valuesEqual(std::any(4), std::any(std::string("4"))));
}

TEST(ReflectionDefaults, ClassWithNoDefaultConstructorIsAlwaysWritten) {
    RegisterAll();
    Fussy f(3);   // same as the member initializer, but there's no instance to prove it
    EXPECT_TRUE(has(writeText(f), "value: 3"));
}

TEST(ReflectionDefaults, RoundTripRestoresChangedAndDefaultValues) {
    RegisterAll();
    BigThing src;
    src.count = 2;
    src.setLabel("round");
    src.inner.tag = "tt";
    src.nums = {4, 5, 6};

    ObjectWriter writer;
    writer.write(&src);

    ObjectReader reader;
    ASSERT_FALSE(json5::from_string(json5::to_string(writer.doc), reader.doc));
    BigThing* back = reader.readNew<BigThing>();
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->count, 2);
    EXPECT_EQ(back->label(), "round");
    EXPECT_EQ(back->inner.tag, "tt");
    EXPECT_EQ(back->inner.level, 1);       // omitted on write, kept as constructed on read
    EXPECT_EQ(back->limit, 7);
    EXPECT_EQ(back->nums, (std::vector<int>{4, 5, 6}));
    delete back;

    BigThing untouched;                     // everything at its default: reads back identical
    ObjectWriter w2;
    w2.write(&untouched);
    ObjectReader r2;
    ASSERT_FALSE(json5::from_string(json5::to_string(w2.doc), r2.doc));
    BigThing* back2 = r2.readNew<BigThing>();
    ASSERT_NE(back2, nullptr);
    EXPECT_EQ(back2->count, 9);
    delete back2;
}

TEST(ReflectionDefaults, RealControlOmitsConstructorSetVisibleButWritesAChange) {
    auto* slider = new newui::Slider();   // Control-derived: its constructor makes it visible
    // Only the slider's own keys: its child (the thumb, a plain SubView whose default is invisible)
    // legitimately still writes visible: true.
    auto ownKeys = [](const std::string& text) { return text.substr(0, text.find("childViews")); };
    std::string compact = writeText(*slider);
    std::string full = writeText(*slider, true);
    EXPECT_FALSE(has(ownKeys(compact), "visible:")) << compact;
    EXPECT_TRUE(has(ownKeys(full), "visible:"));
    EXPECT_LT(compact.size(), full.size());

    slider->setVisible(false);
    EXPECT_TRUE(has(ownKeys(writeText(*slider)), "visible: false"));

    slider->destroy();
    delete slider;
}

TEST(ReflectionDefaults, TopLevelWriteTakesAWriteDefaultsArgument) {
    RegisterAll();
    Thing t;

    ObjectWriter compact;
    compact.write(&t);                 // default: false - only what differs
    EXPECT_FALSE(has(json5::to_string(compact.doc), "count"));

    ObjectWriter forced;
    forced.write(&t, true);            // true - everything, defaults included
    std::string text = json5::to_string(forced.doc);
    for (const char* key : {"count", "nums", "inner", "limit", "label"}) {
        EXPECT_TRUE(has(text, key)) << key << " missing:\n" << text;
    }
}

TEST(ReflectionDefaults, BundleWriteRootViewToTextTakesAWriteDefaultsArgument) {
    newui::Frame frame;
    const std::string compact = newui::Bundle::instance().writeRootViewToText(frame.rootView(), "{}");
    const std::string forced = newui::Bundle::instance().writeRootViewToText(frame.rootView(), "{}", false, true);
    EXPECT_LT(compact.size(), forced.size());
}

// Every registered class that can be default-constructed must see its own freshly-built instance as
// "all defaults" - a class that fails this (an uncomparable property, a constructor-built child that
// isn't recognised, ...) would write noise into every saved file. Catches a new class or property
// that breaks the default-omission rules.
TEST(ReflectionDefaults, FreshInstanceOfEveryRegisteredClassWritesOnlyItsType) {
    std::string offenders;
    for (const Class* c : ReflectionRegistry::allClasses()) {
        if (c->isAbstract() || c->isSingleton()) {
            continue;
        }
        void* raw = nullptr;
        c->createInstance(&raw);
        if (raw == nullptr) {
            continue;   // no zero-argument constructor: nothing to snapshot, always written in full
        }
        ObjectWriter w;
        c->write(raw, &w, std::string());
        std::string keys;
        for (const auto& [k, v] : json5::object_view(w.doc)) {
            std::string key(k);
            if (key != "type" && key != "meta") {
                keys += key + " ";
            }
        }
        if (!keys.empty()) {
            offenders += c->name() + ": " + keys + "\n";
        }
        if (const Method* m = c->method("destroy")) {
            m->invoke(raw, {});
        }
        c->deleteInstance(raw);
    }
    EXPECT_TRUE(offenders.empty()) << offenders;
}

namespace {
void scanForUninitialized(const json5::value& v, const std::string& path, std::string& out) {
    if (v.is_number()) {
        double d = v.get<double>(0.0);
        if (d == 0.0) { d = static_cast<double>(v.get<std::int64_t>(0)); }
        // MSVC debug heap fills fresh allocations with 0xCD.
        if (d == -842150451.0 || d == 3452816845.0 || d == 205.0 || (d < -4.3e8 && d > -4.4e8) ||
            std::abs(d) > 1e30 || d == 52685.0 || d == -12851.0) {
            out += path + " = " + std::to_string(d) + "\n";
        }
    } else if (v.is_string()) {
        std::string str(v.get_c_str(""));
        if (str.rfind("0x", 0) == 0) { out += path + " = " + str + "\n"; }
    } else if (v.is_object()) {
        for (const auto& [k, c] : json5::object_view(v)) {
            scanForUninitialized(c, path + "." + std::string(k), out);
        }
    } else if (v.is_array()) {
        std::size_t i = 0;
        for (const auto& c : json5::array_view(v)) {
            scanForUninitialized(c, path + "[" + std::to_string(i++) + "]", out);
        }
    }
}
}  // namespace

// Deliberately leaves its members uninitialized - the positive control for the scan below.
namespace { struct Dirty { Dirty() {} int x; float f; }; }

namespace {
// Runs one fresh instance of `c` through a write-everything pass and scans it for the values an
// uninitialized member shows up as. The MSVC debug heap fills new allocations with 0xCD, so those
// patterns are what to look for (this is a no-op in a Release build, where nothing is filled).
std::string uninitializedValuesIn(const Class* c) {
    void* raw = nullptr;
    c->createInstance(&raw);
    if (raw == nullptr) {
        return std::string();
    }
    ObjectWriter w;
    w.setWriteDefaults(true);
    c->write(raw, &w, std::string());
    std::string out;
    scanForUninitialized(w.doc, c->name(), out);
    if (const Method* m = c->method("destroy")) {
        m->invoke(raw, {});
    }
    c->deleteInstance(raw);
    return out;
}
}  // namespace

// A property that a constructor leaves unset would make "its default" unpredictable (and a saved file
// compare against garbage), so every reflected class must initialize everything it exposes.
TEST(ReflectionDefaults, NoRegisteredClassExposesAnUninitializedValue) {
    std::string offenders;
    for (const Class* c : ReflectionRegistry::allClasses()) {
        // A stringValue class (Color) writes as a bare string, with no document object to scan -
        // its values are covered through the properties that hold one.
        if (c->isAbstract() || c->isSingleton() || c->isStringValue() || c->name() == "Dirty") {
            continue;
        }
        offenders += uninitializedValuesIn(c);
    }
    EXPECT_TRUE(offenders.empty()) << offenders;
}

#ifdef _DEBUG
TEST(ReflectionDefaults, UninitializedScanActuallyCatchesAnUninitializedMember) {
    ClassBuilder<Dirty> b;
    b.clazz().constructor<>().property("x", Scope::Public, &Dirty::x).property("f", Scope::Public, &Dirty::f);
    ReflectionRegistry::registerClass(b);
    const std::string found = uninitializedValuesIn(classinfo(typeid(Dirty)));
    EXPECT_NE(found.find("Dirty.x"), std::string::npos) << found;
    EXPECT_NE(found.find("Dirty.f"), std::string::npos) << found;
}
#endif
