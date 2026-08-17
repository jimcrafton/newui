// Covers the parts of reflection.h's Property/PropertyCollection/Class
// API that test_reflection.cpp doesn't reach - test_reflection.cpp is all
// MemberPtr-backed properties (a real pointer-to-data-member always
// exists); everything here is reachable only through accessor *methods*
// (get()/set() by value, a mutable-reference-returning getter, a
// possibly-null pointer getter+setter, or add/remove/count/getAt with no
// real container object at all) - the shapes ClassBuilder<T>::property()'s
// getter/setter overload and ::propertyCollection() exist for, and the
// ones examples/reflection2.cpp actually exercises live against the real
// View/Frame/Application hierarchy. ObjectWriter/ObjectReader (the JSON5
// read/write pipeline built on top of this) are deliberately out of scope
// here - see reflection2.cpp's own demoWriter()/demoRoundTrip() for that
// coverage instead.

#include "newui/reflection.h"

#include <any>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace {

// RefGetter target - reachable only via a live-reference-returning
// accessor pair (overloaded on constness, so selectOverload<>() is
// needed to register the mutable one - see RefGetterPropertyIsAddressable
// AndLive below), never a raw data member.
struct Gear {
    float ratio = 0.0f;
};

// PropertyCollection element (via Sprocket's add/remove/count/getAt) and
// PtrGetter/PtrSetter element (via Sprocket's optionalCog()/setOptionalCog()) -
// a plain public-field struct is enough; nothing here needs Cog to be
// separately reflected (that's only needed for ObjectReader's own
// nested-Class recursion, not for driving Property/PropertyCollection
// directly the way every test below does).
struct Cog {
    Cog() = default;
    explicit Cog(int t) : teeth(t) {}
    int teeth = 0;
};

class Sprocket {
public:
    Sprocket() = default;

    // RefGetter + selectOverload target - overloaded on constness.
    Gear& gear() { return gear_; }
    const Gear& gear() const { return gear_; }

    // PtrGetter/PtrSetter + selectOverload target - optional (starts
    // null), reassigned via a real setter, never copied into a
    // gettable/addressable Cog member.
    Cog* optionalCog() { return optionalCog_; }
    const Cog* optionalCog() const { return optionalCog_; }
    void setOptionalCog(Cog* cog) { optionalCog_ = cog; }

    // Getter/Setter by value - unambiguous (not overloaded), so no
    // selectOverload<>() wrapper is needed to register it.
    std::string label() const { return label_; }
    void setLabel(const std::string& label) { label_ = label; }

    // Getter-only (no setter anywhere) - read-only through reflection by
    // design, same as View::isVisible()/bounds() before a subclass adds
    // a real setter.
    int fixedId() const { return fixedId_; }

    // No real container anywhere - just add/remove/count/getAt, the
    // exact shape ClassBuilder::propertyCollection() exists for (see
    // View::childViews()/addChild()/removeChild() in the real codebase).
    std::size_t cogCount() const { return cogs_.size(); }
    Cog* cogAt(std::size_t index) const { return cogs_.at(index); }
    void addCog(Cog* cog) { cogs_.push_back(cog); }
    void removeCog(Cog* cog) {
        for (auto it = cogs_.begin(); it != cogs_.end(); ++it) {
            if (*it == cog) {
                cogs_.erase(it);
                return;
            }
        }
    }

private:
    Gear gear_;
    Cog* optionalCog_ = nullptr;
    std::string label_ = "default";
    int fixedId_ = 42;
    std::vector<Cog*> cogs_;
};

const Class* RegisterAndGetSprocketClass() {
    static const Class* registered = [] {
        ClassBuilder<Sprocket> builder;
        builder.clazz()
            .property("gear", Scope::Public, selectOverload<Gear&(Sprocket::*)()>(&Sprocket::gear))
            .property("optionalCog", Scope::Public,
                selectOverload<Cog*(Sprocket::*)()>(&Sprocket::optionalCog),
                &Sprocket::setOptionalCog)
            .property("label", Scope::Public, &Sprocket::label, &Sprocket::setLabel)
            .property("fixedId", Scope::Public, &Sprocket::fixedId)
            .propertyCollectionByCountAndIndex("cogs", Scope::Public,
                &Sprocket::cogCount, &Sprocket::cogAt,
                &Sprocket::addCog, &Sprocket::removeCog)
            .constructor<>();
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(Sprocket));
    }();
    return registered;
}

}  // namespace

// ---------------------------------------------------------------------
// selectOverload<>() itself - independent of ClassBuilder, since it's a
// standalone template (reflection.h's own "disambiguates a bare
// &Class::method when method is overloaded" helper, same idiom as Qt's
// qOverload<>() or RTTR's select_overload<>()).
// ---------------------------------------------------------------------

TEST(ReflectionIO, SelectOverloadPicksTheNamedSignature) {
    auto fn = selectOverload<Gear&(Sprocket::*)()>(&Sprocket::gear);

    Sprocket s;
    Gear& g = (s.*fn)();
    g.ratio = 5.0f;

    EXPECT_FLOAT_EQ(s.gear().ratio, 5.0f);
}

// ---------------------------------------------------------------------
// RefGetter mode - ClassBuilder::property() routes here when the getter
// returns a genuine mutable T& and no setter was given (see its own
// comment on the three shapes a getter's return type can select).
// ---------------------------------------------------------------------

TEST(ReflectionIO, RefGetterPropertyIsAddressableAndLive) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* gearProp = sprocketClass->property("gear");
    Sprocket s;

    EXPECT_TRUE(gearProp->isAddressable());

    // address() is a live pointer into s, not a boxed copy - mutating
    // through it changes s.gear() itself.
    auto* gearAddress = static_cast<Gear*>(gearProp->address(&s));
    gearAddress->ratio = 2.5f;
    EXPECT_FLOAT_EQ(s.gear().ratio, 2.5f);

    EXPECT_FLOAT_EQ(gearProp->getAs<Gear>(&s).ratio, 2.5f);
}

TEST(ReflectionIO, TypedPropertyGetTypedSetTypedBypassAnyBoxing) {
    RegisterAndGetSprocketClass();
    const auto* gearProp = dynamic_cast<const TypedProperty<Sprocket, Gear>*>(
        classinfo(typeid(Sprocket))->property("gear"));
    ASSERT_NE(gearProp, nullptr);

    Sprocket s;
    // getTyped() is a live reference - no std::any round trip, so
    // mutating through it changes s directly, same as address() does.
    gearProp->getTyped(s).ratio = 3.0f;
    EXPECT_FLOAT_EQ(s.gear().ratio, 3.0f);

    // setTyped() (like set()) only ever writes through a real setter -
    // member_/ptrSetter_/setter_ - never by assigning through a bare
    // RefGetter, even though it's addressable (see TypedProperty::set()'s
    // own "never improvise a set out of a bare getter" comment). "gear"
    // was registered getter-only, so this is silently a no-op, not a
    // write - the live-reference route above is the only way to mutate
    // a RefGetter-only property.
    gearProp->setTyped(s, Gear{9.0f});
    EXPECT_FLOAT_EQ(s.gear().ratio, 3.0f);
}

// ---------------------------------------------------------------------
// PtrGetter/PtrSetter mode - selected when the getter returns ValueT*
// (an *optional* nested object, unlike RefGetter's always-present one).
// Always CreatedOnHeap (see TypedProperty's own comment on why a
// PtrGetter property is never treated as "already-live storage to edit
// in place" the way MemberPtr/RefGetter are).
// ---------------------------------------------------------------------

TEST(ReflectionIO, PtrGetterPropertyIsNullSafeBeforeAnyCogIsSet) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* cogProp = sprocketClass->property("optionalCog");
    Sprocket s;

    EXPECT_TRUE(cogProp->isAddressable());
    EXPECT_TRUE(cogProp->shouldCreateOnHeap());
    EXPECT_EQ(cogProp->address(&s), nullptr);
    EXPECT_FALSE(cogProp->get(&s).has_value());
}

TEST(ReflectionIO, PtrGetterPropertyReadsThroughOnceSet) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* cogProp = sprocketClass->property("optionalCog");
    Sprocket s;
    Cog cog(12);
    s.setOptionalCog(&cog);

    EXPECT_EQ(cogProp->address(&s), &cog);
    EXPECT_EQ(cogProp->getAs<Cog>(&s).teeth, 12);
}

// set() on a PtrGetter/PtrSetter property means "point at this other
// object" (a ValueT* payload), not "copy a ValueT into whatever's
// already pointed at" - see TypedProperty::set()'s own comment on why
// that distinction matters (it's what lets a future generic Reader pass
// Class::createInstance()'s own std::any straight through with no
// per-type unboxing).
TEST(ReflectionIO, PtrSetterPropertyReassignsWhichObjectIsPointedAt) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* cogProp = sprocketClass->property("optionalCog");
    Sprocket s;
    Cog* freshCog = new Cog(7);

    cogProp->set(&s, std::any(freshCog));

    EXPECT_EQ(s.optionalCog(), freshCog);
    EXPECT_EQ(s.optionalCog()->teeth, 7);

    delete freshCog;
}

// ---------------------------------------------------------------------
// Getter/Setter (by value) and getter-only modes - neither ever backed
// by a real pointer-to-member, so isAddressable() is false and
// address() throws rather than returning a dangling pointer to a
// temporary.
// ---------------------------------------------------------------------

TEST(ReflectionIO, GetterSetterPropertyByValueRoundTrips) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* labelProp = sprocketClass->property("label");
    Sprocket s;

    EXPECT_FALSE(labelProp->isAddressable());
    EXPECT_THROW(labelProp->address(&s), std::logic_error);

    EXPECT_EQ(labelProp->getAs<std::string>(&s), "default");

    labelProp->set(&s, std::any(std::string("renamed")));
    EXPECT_EQ(s.label(), "renamed");
    EXPECT_EQ(labelProp->getAs<std::string>(&s), "renamed");
}

// Getter-only: set() is a silent no-op (same contract a getter-only
// Property has always had - see TypedProperty::set()'s own comment on
// deliberately never improvising a "set" out of a bare getter), not an
// exception or a partial write.
TEST(ReflectionIO, GetterOnlyPropertyIsSilentlyReadOnly) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* fixedIdProp = sprocketClass->property("fixedId");
    Sprocket s;

    EXPECT_FALSE(fixedIdProp->isAddressable());
    EXPECT_THROW(fixedIdProp->address(&s), std::logic_error);
    EXPECT_EQ(fixedIdProp->getAs<int>(&s), 42);

    fixedIdProp->set(&s, std::any(999));

    EXPECT_EQ(s.fixedId(), 42);
    EXPECT_EQ(fixedIdProp->getAs<int>(&s), 42);
}

// ---------------------------------------------------------------------
// propertyCollection() - the accessor-fn (CountFn/GetAtFn/AddFn/RemoveFn)
// mode, for a collection with no real, gettable/addressable ContainerT
// at all (see reflection2.cpp's "childViews" for the real-world case
// this mirrors). Never addressable; add()/remove() call the real
// methods directly - by construction here, since Sprocket exposes no
// container to mutate any other way, a passing test IS proof the real
// addCog()/removeCog() ran, not some incidental container swap.
// ---------------------------------------------------------------------

TEST(ReflectionIO, PropertyCollectionReportsAddRemoveNotAddressable) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const Property* cogsProp = sprocketClass->property("cogs");
    const auto* collection = dynamic_cast<const PropertyCollection*>(cogsProp);
    ASSERT_NE(collection, nullptr);

    EXPECT_TRUE(cogsProp->isCollection());
    EXPECT_FALSE(cogsProp->isAddressable());
    EXPECT_TRUE(collection->supportsAddRemove());
    EXPECT_TRUE(cogsProp->shouldCreateOnHeap());  // Cog* element - see flagsFor()'s own comment
}

TEST(ReflectionIO, PropertyCollectionAddDrivesTheRealMethod) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();
    const auto* collection = dynamic_cast<const PropertyCollection*>(sprocketClass->property("cogs"));
    Sprocket s;
    Cog* cog1 = new Cog(3);
    Cog* cog2 = new Cog(5);

    collection->add(&s, std::any(cog1));
    collection->add(&s, std::any(cog2));

    ASSERT_EQ(s.cogCount(), 2u);
    EXPECT_EQ(s.cogAt(0), cog1);
    EXPECT_EQ(s.cogAt(1), cog2);

    // Whole-container get() synthesizes a snapshot from count()/getAt(),
    // not a real container - still enumerable through the exact same
    // PropertyCollection interface a MemberPtr-backed collection uses.
    std::any boxed = collection->get(&s);
    ASSERT_EQ(collection->count(boxed), 2u);
    EXPECT_EQ(std::any_cast<Cog*>(collection->get(boxed, 0)), cog1);
    EXPECT_EQ(std::any_cast<Cog*>(collection->get(boxed, 1)), cog2);

    collection->remove(&s, std::any(cog1));
    ASSERT_EQ(s.cogCount(), 1u);
    EXPECT_EQ(s.cogAt(0), cog2);

    delete cog1;
    delete cog2;
}

// ---------------------------------------------------------------------
// Class::createInstance() / TypedClass<T>::createInstanceTyped() - the
// registered default constructor, exercised directly (test_reflection.cpp
// covers property access but never actually builds a Sprocket/Gadget
// through reflection).
// ---------------------------------------------------------------------

TEST(ReflectionIO, CreateInstanceBuildsARealDefaultConstructedObject) {
    const Class* sprocketClass = RegisterAndGetSprocketClass();

    std::any created = sprocketClass->createInstance();
    ASSERT_TRUE(created.has_value());
    Sprocket* s = std::any_cast<Sprocket*>(created);
    ASSERT_NE(s, nullptr);

    EXPECT_EQ(s->label(), "default");
    EXPECT_EQ(s->fixedId(), 42);
    EXPECT_EQ(s->cogCount(), 0u);

    delete s;
}

TEST(ReflectionIO, TypedCreateInstanceTypedReturnsRealTypeDirectly) {
    RegisterAndGetSprocketClass();  // ensure Sprocket is registered before classinfo<Sprocket>() below
    const TypedClass<Sprocket>* sprocketClass = classinfo<Sprocket>();
    ASSERT_NE(sprocketClass, nullptr);

    Sprocket* s = sprocketClass->createInstanceTyped();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->label(), "default");

    delete s;
}
