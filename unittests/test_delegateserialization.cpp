// Covers the "delegates" JSON5 block reflectionio.h's ObjectWriter/
// ObjectReader now read and write - deliberately excluded from both
// test_delegate.cpp (delegate.h's own descriptor-tracking add()
// overloads/describedListeners() are covered there, independent of
// reflection entirely) and test_reflectionio.cpp (whose own header
// comment scopes it to Property/PropertyCollection, pointing at
// reflection2.cpp's demoWriter()/demoRoundTrip() for ObjectWriter/
// ObjectReader coverage instead - neither of those touches multi-object
// documents or delegate connections at all, so this is genuinely new
// ground, not overlapping duplication of either).

#include "newui/newui.h"
#include "newui/reflection.h"
#include "newui/reflectionio.h"

#include <any>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace {

class FooBar;

// The listening side of a cross-object connection - happyChanged()'s
// first parameter is FooBar* (a pointer), not FooBar& - see
// Delegate::connectListener()'s own comment in reflection.h for why a
// reflectable listener method has to take its sender that way (a
// non-const reference can't safely round-trip through Method::invoke()'s
// std::any-based argument boxing). Because of that mismatch with
// newui::Delegate<>'s own SenderRefT (always SourceT&), happyChanged()
// can never be wired up directly via Delegate::add(instance, method) -
// it's only ever reachable through TypedDelegate::connectListener()'s own
// reflection-driven trampoline (see ObjectReader::readObjects() below),
// which is exactly the thing under test here.
class Bar {
public:
    newui::SyncReturn happyChanged(FooBar* sender, bool value) {
        ++callCount;
        lastValue = value;
        return newui::SyncReturn::Handled;
    }

    int callCount = 0;
    bool lastValue = false;
};

// The sending side - two Delegate<> fields so "only delegates with a
// described listener get written" has something real to distinguish.
class FooBar {
public:
    newui::Delegate<FooBar, bool> onHappyChanged;
    newui::Delegate<FooBar, bool> onSadChanged;
};

newui::SyncReturn FreeHappyChanged(FooBar& sender, bool value) {
    return newui::SyncReturn::Handled;
}

const Class* RegisterAndGetFooBarClass() {
    static const Class* registered = [] {
        ClassBuilder<FooBar> builder;
        builder.clazz()
            .delegate("onHappyChanged", Scope::Public, &FooBar::onHappyChanged)
            .delegate("onSadChanged", Scope::Public, &FooBar::onSadChanged)
            .constructor<>();
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(FooBar));
    }();
    return registered;
}

const Class* RegisterAndGetBarClass() {
    static const Class* registered = [] {
        ClassBuilder<Bar> builder;
        builder.clazz()
            .method("happyChanged", Scope::Public, &Bar::happyChanged)
            .constructor<>();
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(Bar));
    }();
    return registered;
}

void RegisterClasses() {
    RegisterAndGetFooBarClass();
    RegisterAndGetBarClass();
}

}  // namespace

// ---------------------------------------------------------------------
// Write side - TypedClass<T>::write()'s new "delegates" block
// (reflection.h). No read/reconnection involved yet, just what a live
// object with connections already made produces.
// ---------------------------------------------------------------------

TEST(DelegateSerialization, OnlyDelegatesWithDescribedListenersAreWritten) {
    RegisterClasses();
    FooBar foo;
    foo.onHappyChanged.add("FreeHappyChanged", &FreeHappyChanged);
    // onSadChanged stays untouched - no listener at all.

    ObjectWriter writer;
    writer.write(&foo);

    json5::value delegatesNode = writer.doc["delegates"];
    ASSERT_TRUE(delegatesNode.is_object());
    json5::object_view ov(delegatesNode);
    EXPECT_EQ(ov.size(), 1u);
    EXPECT_TRUE(ov.find("onHappyChanged") != ov.end());
    EXPECT_TRUE(ov.find("onSadChanged") == ov.end());

    json5::array_view names(writer.doc["delegates"]["onHappyChanged"]);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(std::string(names.begin()[0].get_c_str("")), "FreeHappyChanged");
}

TEST(DelegateSerialization, NoDelegatesBlockWhenNothingIsDescribed) {
    RegisterClasses();
    FooBar foo;
    // Neither delegate has any listener at all - not even an undescribed
    // one - so the whole "delegates" key should be absent, not an empty
    // object.

    ObjectWriter writer;
    writer.write(&foo);

    EXPECT_FALSE(writer.doc["delegates"].is_object());
}

TEST(DelegateSerialization, UndescribedListenerDoesNotMakeItIntoTheDelegatesBlock) {
    RegisterClasses();
    FooBar foo;
    foo.onHappyChanged.add(&FreeHappyChanged);  // no descriptor

    ObjectWriter writer;
    writer.write(&foo);

    // A connection was made (it would still fire), it's just not
    // describable, so it has nothing to write out.
    EXPECT_FALSE(writer.doc["delegates"].is_object());
}

// ---------------------------------------------------------------------
// Multi-object round trip - ObjectWriter::writeObjects()/
// ObjectReader::readObjects(), including the two-pass delegate
// reconnection. Real behavioral proof (fire the reconnected delegate and
// check the target ran), not just JSON shape.
// ---------------------------------------------------------------------

TEST(DelegateSerialization, CrossObjectConnectionReconnectsAndFires) {
    const Class* fooBarClass = RegisterAndGetFooBarClass();
    const Class* barClass = RegisterAndGetBarClass();

    FooBar foo;
    Bar bar;
    // What matters for the write step is only the descriptor string -
    // the lambda itself never runs (the whole point of this test is that
    // firing the *reconnected* delegate, on fresh objects readObjects()
    // creates, is what actually reaches Bar::happyChanged - not this
    // original, disconnected-after-write() lambda).
    foo.onHappyChanged.add("bar@Bar.happyChanged", [](FooBar&, bool) {
        return newui::SyncReturn::Handled;
    });

    ObjectWriter writer;
    writer.writeObjects({
        {"foo", fooBarClass, &foo},
        {"bar", barClass, &bar},
    });
    std::string text = json5::to_string(writer.doc);

    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    std::vector<ObjectReader::NamedObject> objects = reader.readObjects();
    ASSERT_EQ(objects.size(), 2u);

    FooBar* newFoo = nullptr;
    Bar* newBar = nullptr;
    for (auto& obj : objects) {
        if (obj.name == "foo") {
            newFoo = std::any_cast<FooBar*>(obj.instance);
        } else if (obj.name == "bar") {
            newBar = std::any_cast<Bar*>(obj.instance);
        }
    }
    ASSERT_NE(newFoo, nullptr);
    ASSERT_NE(newBar, nullptr);

    newFoo->onHappyChanged.syncCall(*newFoo, true);

    EXPECT_EQ(newBar->callCount, 1);
    EXPECT_TRUE(newBar->lastValue);

    delete newFoo;
    delete newBar;
}

TEST(DelegateSerialization, FreeFunctionConnectionRoundTripsButIsNotReconnected) {
    const Class* fooBarClass = RegisterAndGetFooBarClass();

    FooBar foo;
    foo.onHappyChanged.add("FreeHappyChanged", &FreeHappyChanged);

    ObjectWriter writer;
    writer.writeObjects({ {"foo", fooBarClass, &foo} });
    std::string text = json5::to_string(writer.doc);

    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    // Option C (this project's own deferred design decision - see
    // ObjectReader::resolveDelegateConnection()'s own comment): a bare
    // function name is written, but reconnecting it on read isn't
    // supported yet - readObjects() should skip it (logging, not
    // crashing), leaving the fresh instance's delegate with no listener
    // at all.
    std::vector<ObjectReader::NamedObject> objects = reader.readObjects();
    ASSERT_EQ(objects.size(), 1u);

    FooBar* newFoo = std::any_cast<FooBar*>(objects[0].instance);
    ASSERT_NE(newFoo, nullptr);

    EXPECT_TRUE(newFoo->onHappyChanged.describedListeners().empty());

    delete newFoo;
}

TEST(DelegateSerialization, UnknownTargetObjectIsSkippedWithoutCrashing) {
    const Class* fooBarClass = RegisterAndGetFooBarClass();

    FooBar foo;
    // "ghost" is never written as its own object - a dangling reference.
    foo.onHappyChanged.add("ghost@Bar.happyChanged", [](FooBar&, bool) {
        return newui::SyncReturn::Handled;
    });

    ObjectWriter writer;
    writer.writeObjects({ {"foo", fooBarClass, &foo} });
    std::string text = json5::to_string(writer.doc);

    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    std::vector<ObjectReader::NamedObject> objects = reader.readObjects();
    ASSERT_EQ(objects.size(), 1u);

    FooBar* newFoo = std::any_cast<FooBar*>(objects[0].instance);
    ASSERT_NE(newFoo, nullptr);
    EXPECT_TRUE(newFoo->onHappyChanged.describedListeners().empty());

    delete newFoo;
}
