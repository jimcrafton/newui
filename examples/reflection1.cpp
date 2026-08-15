// A tour of newui::reflection - hand-wiring a plain class (Widget, below)
// against the Field/Property/Method/Delegate/Constructor/Class/
// ClassBuilder/ReflectionRegistry API in reflection.h. There's no
// reflectgen tool yet (see reflection.h's comments), so everything from
// the detail::InstanceAccessor<Widget,I> specializations down to the
// ClassBuilder<Widget> calls in registerWidgetReflection() below is
// exactly the boilerplate a future code generator would emit
// automatically. Writing it by hand here is what validates the API shape.
//
// This uses ClassBuilder<Widget>'s *typed* add*() overloads
// (addTypedProperty/addTypedMethod/addTypedConstructor) throughout - they
// take a real C++ pointer-to-member(-function) or pointer and need no
// separately hand-written std::any-boxing thunk function at all, unlike
// the untyped addProperty()/addMethod()/addConstructor() (still part of
// the API, just not exercised here - see reflection.h's ClassBuilder
// comment for when that path is the better fit).
//
// Widget mixes public and private members on purpose: price/active are
// public (their pointers-to-member need nothing special), label_/quantity_/
// weight_ are private (matching this codebase's own trailing-'_'
// convention for private members) and only reachable because Widget
// declares NEWUI_REFLECT_FRIEND() once in its body - see reflection.h's
// top comment for why that's enough to cover every field, not just one.

#include "newui/newui.h"
#include "newui/reflection.h"

#include <any>
#include <iostream>
#include <map>
#include <string>
#include <typeinfo>
#include <vector>

using namespace newui::reflection;

class Widget {
public:
    NEWUI_REFLECT_FRIEND();

    Widget() = default;
    Widget(std::string label, int quantity) : label_(std::move(label)), quantity_(quantity) {}

    float price = 0.0f;
    bool active = true;

    // A sequential collection field (public - no ClassAccess needed) and an
    // associative one (private - reached the same way label_/quantity_/
    // weight_ are, via ClassAccess<Widget> below). Both are registered with
    // the exact same .property(name, scope, &Widget::field) call any
    // scalar field uses - see demoCollectionProperties() for what that
    // buys on top.
    std::vector<std::string> tags;

    int addQuantity(int amount) {
        quantity_ += amount;
        return quantity_;
    }

    // Also doubles as this demo's read-only window into the private fields
    // below - main() has no other legitimate way to see label_/quantity_/
    // weight_ directly, which is exactly why reading them through
    // reflection (Demo 2) is the interesting part.
    std::string describe() const {
        return label_ + " x" + std::to_string(quantity_) + " (" + std::to_string(weight_) + "kg)";
    }

private:
    std::string label_ = "widget";
    int quantity_ = 1;
    float weight_ = 2.5f;
    std::map<std::string, float> ratings_ = { {"quality", 4.5f}, {"value", 3.5f} };
};

// ---------------------------------------------------------------------
// Hand-wired reflection registration for Widget - see this file's header
// comment. One friended ClassAccess<Widget> specialization exposes each
// private field's pointer-to-member as a named static method - computing
// &Widget::label_ is only legal inside this specialization's own body
// (it's a friend of Widget, see NEWUI_REFLECT_FRIEND()), but reading the
// already-computed pointer-to-member value back out isn't privileged, so
// registerWidgetReflection() below can use it freely. No index bookkeeping
// needed - just one named method per field, in one block per class.
// ---------------------------------------------------------------------

template<> struct newui::reflection::detail::ClassAccess<Widget> {
    static constexpr auto label_() { return &Widget::label_; }
    static constexpr auto quantity_() { return &Widget::quantity_; }
    static constexpr auto weight_() { return &Widget::weight_; }
    static constexpr auto ratings_() { return &Widget::ratings_; }
};

void registerWidgetReflection() {
    ClassBuilder<Widget> builder;

    builder.clazz()
        .property("label_", Scope::Private, detail::ClassAccess<Widget>::label_())
        .property("quantity_", Scope::Private, detail::ClassAccess<Widget>::quantity_())
        .property("weight_", Scope::Private, detail::ClassAccess<Widget>::weight_())
        .property("ratings_", Scope::Private, detail::ClassAccess<Widget>::ratings_())
        // price/active/tags are public - no InstanceAccessor needed,
        // &Widget::price is legal from any context.
        .property("price", Scope::Public, &Widget::price)
        .property("active", Scope::Public, &Widget::active)
        .property("tags", Scope::Public, &Widget::tags)

        .method("addQuantity", Scope::Public, &Widget::addQuantity)
        .method("describe", Scope::Public, &Widget::describe)

        .constructor<>()                    // Widget()
        .constructor<std::string, int>();     // Widget(std::string, int)

    ReflectionRegistry::registerClass(builder);
}

// A small derived-class demo for ClassBuilder<T>::base<BaseT>() - adds one
// field of its own on top of everything Widget already has. Widget must
// already be registered before this runs (base<Widget>() looks it up in
// ReflectionRegistry immediately) - registerWidgetReflection() is called by
// demoRegisterAndLookup() below, before demoInheritance().
class SuperWidget : public Widget {
public:
    int priority = 0;
};

void registerSuperWidgetReflection() {
    ClassBuilder<SuperWidget> builder;

    builder.clazz()
        .base<Widget>()
        .property("priority", Scope::Public, &SuperWidget::priority);

    ReflectionRegistry::registerClass(builder);
}

// ---------------------------------------------------------------------
// Demos - everything below only talks to Widget through the reflection API
// (Class/Property/Method), never Widget's own members/methods directly,
// except where noted as "ground truth" to check the reflected result
// against.
// ---------------------------------------------------------------------

const char* scopeName(Scope scope) {
    switch (scope) {
        case Scope::Public: return "public";
        case Scope::Protected: return "protected";
        case Scope::Private: return "private";
    }
    return "?";
}

void demoRegisterAndLookup() {
    std::cout << "\n== Demo 1: register Widget, look it up by type and by name ==\n";

    registerWidgetReflection();

    const Class* byType = classinfo(typeid(Widget));
    const Class* byName = classinfo("Widget");

    std::cout << "  found by type: " << std::boolalpha << (byType != nullptr) << "\n";
    std::cout << "  found by name: " << std::boolalpha << (byName != nullptr) << "\n";
    std::cout << "  both lookups return the same Class: " << std::boolalpha << (byType == byName) << "\n";
    std::cout << "  name=" << byType->name() << ", properties=" << byType->properties().size()
               << ", methods=" << byType->methods().size() << "\n";
}

void demoEnumerateAndReadProperties() {
    std::cout << "\n== Demo 2: enumerate properties, read public and private fields by name ==\n";

    const Class* widgetClass = classinfo(typeid(Widget));// classinfo<Widget>();
    Widget w("gadget", 3);
    w.price = 9.99f;

    // properties() (and fields()/methods()/delegates()) is a vector of
    // owning base-class pointers now - TypedProperty<Widget,ValueT> and a
    // plain Property could coexist in the same Class, so iterating by
    // pointer (not by value) is what lets that be a single homogeneous
    // collection without slicing.
    for (const auto* prop : widgetClass->properties()) {
        std::cout << "  " << scopeName(prop->scope()) << " " << prop->name() << "\n";
    }

    std::cout << "  label_ via reflection:  " << widgetClass->property("label_")->getAs<std::string>(&w) << "\n";
    std::cout << "  weight_ via reflection: " << widgetClass->property("weight_")->getAs<float>(&w) << "\n";
    std::cout << "  price via reflection:   " << widgetClass->property("price")->getAs<float>(&w) << "\n";
    std::cout << "  ground truth: " << w.describe() << ", price=" << w.price << "\n";

    // address() is a live pointer into w, not a boxed copy - mutate through
    // it directly and confirm w itself changed.
    float* weightAddress = static_cast<float*>(widgetClass->property("weight_")->address(&w));
    *weightAddress = 4.0f;
    std::cout << "  after writing through address(): " << w.describe() << "\n";
}

void demoSetProperties() {
    std::cout << "\n== Demo 3: set() private and public fields by name ==\n";

    const Class* widgetClass = classinfo(typeid(Widget));
    Widget w;

    widgetClass->property("label_")->set(&w, std::any(std::string("renamed")));
    widgetClass->property("quantity_")->set(&w, std::any(7));
    widgetClass->property("weight_")->set(&w, std::any(1.25f));
    widgetClass->property("price")->set(&w, std::any(19.99f));
    widgetClass->property("active")->set(&w, std::any(false));

    std::cout << "  " << w.describe() << ", price=" << w.price
               << ", active=" << std::boolalpha << w.active << "\n";
}

void demoInvokeMethods() {
    std::cout << "\n== Demo 4: enumerate and invoke methods ==\n";

    const Class* widgetClass = classinfo(typeid(Widget));
    for (const auto* m : widgetClass->methods()) {
        std::cout << "  " << scopeName(m->scope()) << " " << m->name() << "(" << m->arguments().size()
                   << " args), hasReturnValue=" << std::boolalpha << m->hasReturnValue() << "\n";
    }

    Widget w("thing", 5);

    std::any addResult = widgetClass->method("addQuantity")->invoke(&w, { std::any(10) });
    std::cout << "  addQuantity(10) returned " << std::any_cast<int>(addResult)
               << ", w.describe()=" << w.describe() << "\n";

    std::any described = widgetClass->method("describe")->invoke(&w, {});
    std::cout << "  describe() via reflection: " << std::any_cast<std::string>(described) << "\n";
}

void demoCreateInstance() {
    std::cout << "\n== Demo 5: Class::createInstance() (untyped) and TypedClass<T>::createInstanceTyped() ==\n";

    const Class* widgetClass = classinfo(typeid(Widget));

    // Untyped path - std::any in, std::any out. A caller here must already
    // know to std::any_cast<Widget*> the result; a wrong cast throws
    // std::bad_any_cast rather than the silent UB a void* would risk.
    std::any defaultAny = widgetClass->createInstance();
    Widget* defaultWidget = std::any_cast<Widget*>(defaultAny);
    std::cout << "  Class::createInstance() (untyped):        " << defaultWidget->describe() << "\n";

    // Typed path - classinfo<Widget>() resolves (via dynamic_cast, since
    // Class is polymorphic) the TypedClass<Widget> that ClassBuilder<Widget>
    // actually built in registerWidgetReflection() above.
    // createInstanceTyped() then hands back a Widget* directly, no
    // std::any_cast needed at the call site.
    const TypedClass<Widget>* typedWidgetClass = classinfo<Widget>();
    std::cout << "  classinfo<Widget>() resolved a TypedClass<Widget>: "
               << std::boolalpha << (typedWidgetClass != nullptr) << "\n";

    Widget* customWidget = typedWidgetClass->createInstanceTyped(
        { std::any(std::string("built-by-reflection")), std::any(2) });
    std::cout << "  TypedClass<Widget>::createInstanceTyped(2 args): " << customWidget->describe() << "\n";
    std::cout << "  distinct objects: " << std::boolalpha << (defaultWidget != customWidget) << "\n";

    // No 1-arg constructor was ever registered for Widget -
    // createInstanceTyped() returns nullptr rather than guessing at a
    // mismatched overload.
    Widget* noMatch = typedWidgetClass->createInstanceTyped({ std::any(42) });
    std::cout << "  no 1-arg constructor registered -> createInstanceTyped() returns null: "
               << std::boolalpha << (noMatch == nullptr) << "\n";

    delete defaultWidget;
    delete customWidget;
}

void demoCollectionProperties() {
    std::cout << "\n== Demo 6: collection properties (tags: vector, ratings_: map) ==\n";

    const Class* widgetClass = classinfo(typeid(Widget));
    Widget w("gadget", 3);
    w.tags = { "sale", "featured" };

    const Property* tagsProp = widgetClass->property("tags");
    const Property* ratingsProp = widgetClass->property("ratings_");

    std::cout << "  tags: isCollection=" << std::boolalpha << tagsProp->isCollection()
               << ", isAssociative=" << tagsProp->isAssociative() << "\n";
    std::cout << "  ratings_: isCollection=" << std::boolalpha << ratingsProp->isCollection()
               << ", isAssociative=" << ratingsProp->isAssociative() << "\n";

    // dynamic_cast to PropertyCollection is what unlocks element-level
    // access - element/key type + count()/get()/set() all work off a
    // std::any the caller already has (here, obtained from the base
    // Property::get(instance) whole-container accessor), never off a raw
    // ValueT the caller would have to already know at compile time.
    const auto* tagsCollection = dynamic_cast<const PropertyCollection*>(tagsProp);
    const auto* ratingsCollection = dynamic_cast<const PropertyCollection*>(ratingsProp);

    std::any tagsBoxed = tagsProp->get(&w);
    std::cout << "  tags count=" << tagsCollection->count(tagsBoxed) << "\n";
    for (std::size_t i = 0; i < tagsCollection->count(tagsBoxed); ++i) {
        std::cout << "    [" << i << "] " << std::any_cast<std::string>(tagsCollection->get(tagsBoxed, i)) << "\n";
    }
    tagsCollection->set(tagsBoxed, 0, std::any(std::string("clearance")));
    tagsProp->set(&w, tagsBoxed);  // write the mutated copy back into w
    std::cout << "  after set(0, \"clearance\"): tags[0]=" << w.tags[0] << "\n";

    std::any ratingsBoxed = ratingsProp->get(&w);
    std::cout << "  ratings_ count=" << ratingsCollection->count(ratingsBoxed) << "\n";
    std::cout << "  ratings_[\"quality\"] = "
               << std::any_cast<float>(ratingsCollection->get(ratingsBoxed, std::any(std::string("quality")))) << "\n";
    ratingsCollection->set(ratingsBoxed, std::any(std::string("value")), std::any(5.0f));
    ratingsProp->set(&w, ratingsBoxed);  // write the mutated copy back into w

    // Re-read through reflection to confirm the write landed - ratings_ is
    // private, there's no other legitimate way to see it from main().
    std::any confirmBoxed = ratingsProp->get(&w);
    std::cout << "  after set(\"value\", 5.0): ratings_[\"value\"] = "
               << std::any_cast<float>(ratingsCollection->get(confirmBoxed, std::any(std::string("value")))) << "\n";
}

void demoInheritance() {
    std::cout << "\n== Demo 7: base<BaseT>() links a derived class to its parent's Class ==\n";

    registerSuperWidgetReflection();

    const Class* widgetClass = classinfo(typeid(Widget));
    const Class* superWidgetClass = classinfo(typeid(SuperWidget));

    std::cout << "  SuperWidget isDerived(): " << std::boolalpha << superWidgetClass->isDerived() << "\n";
    std::cout << "  SuperWidget parentClass() is Widget's own Class: "
               << std::boolalpha << (superWidgetClass->parentClass() == widgetClass) << "\n";
    std::cout << "  parentClass()->name(): " << superWidgetClass->parentClass()->name() << "\n";
    std::cout << "  Widget itself has no parentClass() (never linked to one): "
               << std::boolalpha << (widgetClass->parentClass() == nullptr) << "\n";

    // base<BaseT>() throws if BaseT hasn't been registered yet - demonstrated
    // here against a plain local struct that's never gone through
    // ClassBuilder/registerClass() at all, rather than risking disturbing
    // real registration order above.
    struct NeverRegistered {};
    struct AlsoNeverRegistered : NeverRegistered {};
    try {
        ClassBuilder<AlsoNeverRegistered> unregisteredBuilder;
        unregisteredBuilder.clazz().base<NeverRegistered>();
        std::cout << "  (unexpected: base<BaseT>() didn't throw for an unregistered base)\n";
    } catch (const std::logic_error& e) {
        std::cout << "  base<BaseT>() on an unregistered base threw as expected: " << e.what() << "\n";
    }
}

int main() {
    std::cout << "newui " << newui::version() << " - reflection examples\n";

    demoRegisterAndLookup();
    demoEnumerateAndReadProperties();
    demoSetProperties();
    demoInvokeMethods();
    demoCreateInstance();
    demoCollectionProperties();
    demoInheritance();

    return 0;
}
