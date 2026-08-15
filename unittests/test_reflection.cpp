#include "newui/reflection.h"

#include <array>
#include <any>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace {

// Small private-field test subject, registered once (RegisterOnce below)
// against newui::reflection the same way examples/reflection1.cpp's Widget
// is - one collection field per container kind this pass supports, plus a
// plain scalar for regression coverage.
class Gadget {
public:
    NEWUI_REFLECT_FRIEND();

private:
    int count_ = 7;
    std::vector<int> numbers_ = {10, 20, 30};
    std::array<float, 3> weights_ = {1.5f, 2.5f, 3.5f};
    std::map<std::string, int> scores_ = {{"alice", 90}, {"bob", 80}};
};

}  // namespace

template<> struct newui::reflection::detail::ClassAccess<Gadget> {
    static constexpr auto count_() { return &Gadget::count_; }
    static constexpr auto numbers_() { return &Gadget::numbers_; }
    static constexpr auto weights_() { return &Gadget::weights_; }
    static constexpr auto scores_() { return &Gadget::scores_; }
};

namespace {

const Class* RegisterAndGetGadgetClass() {
    static const Class* registered = [] {
        ClassBuilder<Gadget> builder;
        builder.clazz()
            .property("count_", Scope::Private, detail::ClassAccess<Gadget>::count_())
            .property("numbers_", Scope::Private, detail::ClassAccess<Gadget>::numbers_())
            .property("weights_", Scope::Private, detail::ClassAccess<Gadget>::weights_())
            .property("scores_", Scope::Private, detail::ClassAccess<Gadget>::scores_());
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(Gadget));
    }();
    return registered;
}

}  // namespace

TEST(Reflection, ScalarPropertyIsNotCollection) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();
    const Property* countProp = gadgetClass->property("count_");

    EXPECT_FALSE(countProp->isCollection());
    EXPECT_FALSE(countProp->isAssociative());
    EXPECT_EQ(dynamic_cast<const PropertyCollection*>(countProp), nullptr);
}

TEST(Reflection, VectorPropertyReportsSequentialCollectionFlags) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();
    const Property* numbersProp = gadgetClass->property("numbers_");

    EXPECT_TRUE(numbersProp->isCollection());
    EXPECT_FALSE(numbersProp->isAssociative());

    const auto* collection = dynamic_cast<const PropertyCollection*>(numbersProp);
    ASSERT_NE(collection, nullptr);
    EXPECT_EQ(collection->elementType(), std::type_index(typeid(int)));
    EXPECT_EQ(collection->keyType(), std::type_index(typeid(std::size_t)));
}

TEST(Reflection, VectorPropertyCountAndIndexAccess) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();
    const Property* numbersProp = gadgetClass->property("numbers_");
    const auto* collection = dynamic_cast<const PropertyCollection*>(numbersProp);
    Gadget g;

    std::any boxed = numbersProp->get(&g);
    ASSERT_EQ(collection->count(boxed), 3u);
    EXPECT_EQ(std::any_cast<int>(collection->get(boxed, 0)), 10);
    EXPECT_EQ(std::any_cast<int>(collection->get(boxed, 2)), 30);

    collection->set(boxed, 1, std::any(99));
    numbersProp->set(&g, boxed);

    std::any confirm = numbersProp->get(&g);
    EXPECT_EQ(std::any_cast<int>(collection->get(confirm, 1)), 99);

    // Whole-container access still works unchanged.
    EXPECT_EQ(numbersProp->getAs<std::vector<int>>(&g), (std::vector<int>{10, 99, 30}));

    // The any-key overload is index-based for a sequential collection.
    EXPECT_EQ(std::any_cast<int>(collection->get(confirm, std::any(std::size_t(0)))), 10);
}

TEST(Reflection, ArrayPropertyIsFixedSizeSequentialCollection) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();
    const Property* weightsProp = gadgetClass->property("weights_");
    const auto* collection = dynamic_cast<const PropertyCollection*>(weightsProp);
    Gadget g;

    EXPECT_TRUE(weightsProp->isCollection());
    EXPECT_FALSE(weightsProp->isAssociative());

    std::any boxed = weightsProp->get(&g);
    ASSERT_EQ(collection->count(boxed), 3u);
    EXPECT_FLOAT_EQ(std::any_cast<float>(collection->get(boxed, 1)), 2.5f);

    collection->set(boxed, 2, std::any(9.5f));
    weightsProp->set(&g, boxed);

    std::any confirm = weightsProp->get(&g);
    EXPECT_FLOAT_EQ(std::any_cast<float>(collection->get(confirm, 2)), 9.5f);
    EXPECT_EQ(collection->count(confirm), 3u);
}

TEST(Reflection, MapPropertyReportsAssociativeCollectionFlags) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();
    const Property* scoresProp = gadgetClass->property("scores_");

    EXPECT_TRUE(scoresProp->isCollection());
    EXPECT_TRUE(scoresProp->isAssociative());

    const auto* collection = dynamic_cast<const PropertyCollection*>(scoresProp);
    ASSERT_NE(collection, nullptr);
    EXPECT_EQ(collection->elementType(), std::type_index(typeid(int)));
    EXPECT_EQ(collection->keyType(), std::type_index(typeid(std::string)));
}

TEST(Reflection, MapPropertyKeyAndPositionalAccess) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();
    const Property* scoresProp = gadgetClass->property("scores_");
    const auto* collection = dynamic_cast<const PropertyCollection*>(scoresProp);
    Gadget g;

    std::any boxed = scoresProp->get(&g);
    ASSERT_EQ(collection->count(boxed), 2u);
    EXPECT_EQ(std::any_cast<int>(collection->get(boxed, std::any(std::string("alice")))), 90);

    // std::map is key-ordered ("alice" < "bob"), so positional index 0 is
    // "alice" - this is the same iterator-order guarantee container_traits
    // relies on for std::map::getByIndex/setByIndex.
    EXPECT_EQ(std::any_cast<int>(collection->get(boxed, std::size_t(0))), 90);

    collection->set(boxed, std::any(std::string("bob")), std::any(85));
    scoresProp->set(&g, boxed);

    std::any confirm = scoresProp->get(&g);
    EXPECT_EQ(std::any_cast<int>(collection->get(confirm, std::any(std::string("bob")))), 85);
    EXPECT_EQ((scoresProp->getAs<std::map<std::string, int>>(&g)).at("bob"), 85);
}

// ---------------------------------------------------------------------
// ClassBuilder<T>::base<BaseT>() / Class::parentClass() - see
// reflection.h's own doc comments on both for the full contract
// (registration-order requirement, throw-on-unregistered-base, non-
// ownership of the stored pointer).
// ---------------------------------------------------------------------

namespace {

class ReflectedBase {
public:
    int baseValue = 1;
};

class ReflectedDerived : public ReflectedBase {
public:
    int derivedValue = 2;
};

const Class* RegisterAndGetReflectedBaseClass() {
    static const Class* registered = [] {
        ClassBuilder<ReflectedBase> builder;
        builder.clazz().property("baseValue", Scope::Public, &ReflectedBase::baseValue);
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(ReflectedBase));
    }();
    return registered;
}

const Class* RegisterAndGetReflectedDerivedClass() {
    RegisterAndGetReflectedBaseClass();  // base<ReflectedBase>() below needs this registered first
    static const Class* registered = [] {
        ClassBuilder<ReflectedDerived> builder;
        builder.clazz()
            .base<ReflectedBase>()
            .property("derivedValue", Scope::Public, &ReflectedDerived::derivedValue);
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(ReflectedDerived));
    }();
    return registered;
}

}  // namespace

TEST(Reflection, BaseLinksDerivedClassToParentClass) {
    const Class* baseClass = RegisterAndGetReflectedBaseClass();
    const Class* derivedClass = RegisterAndGetReflectedDerivedClass();

    EXPECT_TRUE(derivedClass->isDerived());
    ASSERT_NE(derivedClass->parentClass(), nullptr);
    EXPECT_EQ(derivedClass->parentClass(), baseClass);
    EXPECT_EQ(derivedClass->parentClass()->name(), "ReflectedBase");
}

TEST(Reflection, ClassWithNoBaseCallHasNullParentClass) {
    const Class* gadgetClass = RegisterAndGetGadgetClass();

    EXPECT_FALSE(gadgetClass->isDerived());
    EXPECT_EQ(gadgetClass->parentClass(), nullptr);
}

TEST(Reflection, BaseThrowsIfBaseClassNotYetRegistered) {
    struct UnregisteredBase {};
    struct UnregisteredDerived : UnregisteredBase {};

    ClassBuilder<UnregisteredDerived> builder;
    EXPECT_THROW(builder.clazz().base<UnregisteredBase>(), std::logic_error);
}

TEST(Reflection, PropertyFlagsCombineCollectionAndAssociative) {
    PropertyFlags flags = PropertyFlags::Collection | PropertyFlags::Associative;
    EXPECT_TRUE((flags & PropertyFlags::Collection) != PropertyFlags::None);
    EXPECT_TRUE((flags & PropertyFlags::Associative) != PropertyFlags::None);
    EXPECT_TRUE((PropertyFlags::None & PropertyFlags::Collection) == PropertyFlags::None);
}
