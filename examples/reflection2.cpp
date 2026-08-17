// A second reflection tour - unlike reflection1.cpp's standalone Widget,
// this one runs the reflection API (reflection.h/reflectionio.h) against
// a real (if minimal) slice of newui's own Application/Frame/View/
// SubView/ViewStyle hierarchy, proving out reflection-driven
// serialization as the toolkit's real save/load mechanism - the
// hand-written writeFields()/readFields() overrides this replaced
// (formerly serialization.h/.cpp) are gone; ObjectWriter/ObjectReader
// (reflectionio.h) are the only serialization path now.
//
// Every class registered below is wired up using only EXISTING PUBLIC
// accessors (View::name()/bounds()/isVisible()/style()/childViews(),
// Frame::getTitle()/getBounds()/getView(), Application::getName()/
// getFrame(), ViewStyle::borderWidth/opacity) - no NEWUI_REFLECT_PRIVATE(),
// no core header changes, nothing here reaches past real C++ access
// control the way reflection1.cpp's Widget does for its private fields.
//
// ObjectWriter/ObjectReader know about exactly four "shapes" of Property:
//   - a small set of leaf value types (float/bool/std::string/Rect),
//     read/written directly via Property::get()/set().
//   - "style": a single nested, polymorphic-by-runtime-type object,
//     reached via Property::address() (a live pointer, not a boxed
//     std::any copy) and reconstructed via View::setStyle() on read -
//     never via a generic Property::set().
//   - "childViews": a polymorphic collection of SubView* - View has no
//     real, gettable/addressable std::vector<SubView*> to expose (no
//     NEWUI_REFLECT_PRIVATE() on View to reach childViews_ itself), so
//     this is registered via ClassBuilder::propertyCollection() (see
//     registerViewReflection()'s own comment): count/getAt enumerate for
//     write(), and add/remove (the real View::addChild()/removeChild())
//     are what read() drives directly, one child at a time - never a raw
//     container mutation.
//   - anything else nested (Frame's "rootView", Application's "frame") -
//     written (so the JSON5 output below shows the whole real tree) but
//     deliberately never reconstructed on read (Frame has no registered
//     constructor) - see ObjectReader::read()'s own comment for why.

#include "newui/newui.h"
#include "newui/reflection.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include "newui/reflectionio.h"

#include <any>
#include <iostream>
#include <string>
#include <vector>

using namespace newui;
using namespace newui::reflection;

// ---------------------------------------------------------------------
// Hand-wired reflection registration - see this file's header comment
// for why every property thunk below only ever goes through an existing
// public accessor.
// ---------------------------------------------------------------------

// Registered so the ObjectWriter/ObjectReader pipeline (reflectionio.h,
// see demoWriter()/demoRoundTrip() below) has something real to recurse
// into for every "bounds" property; without this, Property::writeValue()/
// readValue() have no case for Rect at all (it's not one of the small
// set of scalar types they know about, unlike float/bool/int*/string).
//
// x/y/width/height each pair a getter with Rect's own matching
// setLeft()/setTop()/setWidth()/setHeight() (geometry.h) - real methods,
// not a lambda wrapping setPos()/setSize() (which each replace both
// components of pos_/size_ at once, not the one field being written).
// Without a real setter, these were get-only - address()/set() both
// silently no-op for a getter-only TypedProperty (see its own comment on
// why that's deliberate, not a bug, for a property that's *supposed* to
// be read-only) - so every "bounds" ever read back through this pipeline
// stayed Rect()'s default (0,0,0,0) instead of the real written value.
void registerRectReflection() {
    ClassBuilder<Rect> builder;
    builder.clazz()
        .property("x", Scope::Public, &Rect::left, &Rect::setLeft)
        .property("y", Scope::Public, &Rect::top, &Rect::setTop)
        .property("width", Scope::Public, &Rect::width, &Rect::setWidth)
        .property("height", Scope::Public, &Rect::height, &Rect::setHeight);
    ReflectionRegistry::registerClass(builder);
}

void registerViewStyleReflection() {
    ClassBuilder<ViewStyle> builder;
    builder.clazz()
        .property("borderWidth", Scope::Public, &ViewStyle::borderWidth)
        .property("opacity", Scope::Public, &ViewStyle::opacity)
        .constructor<>();
    ReflectionRegistry::registerClass(builder);
}

void registerViewReflection() {
    ClassBuilder<View> builder;
    builder.clazz()
        .property("name", Scope::Public, &View::name, &View::setName)
        // visible/bounds are get-only here - View itself has no
        // setVisible()/setBounds(), only SubView/RootView do (see
        // registerSubViewReflection() below, which re-declares both
        // with a real setter; TypedClass<T>::allProperties() (reflection.h)
        // prefers whichever declaration is closer to the object's own
        // runtime class). Bare method pointers are unambiguous here
        // (neither is overloaded), so no lambda wrapper is needed.
        .property("visible", Scope::Public, &View::isVisible)
        .property("bounds", Scope::Public, &View::bounds)
        // Nested, polymorphic single object - View::style() is overloaded
        // on constness, so a bare &View::style is ambiguous (no target
        // type at the point of '&' to pick one); selectOverload<>() (see
        // its own comment) supplies that target type explicitly, picking
        // the mutable overload. Its ValueT&-returning shape makes
        // ClassBuilder::property() register it as addressable - see that
        // method's own comment - so address() hands back a live ViewStyle*
        // and (on read) View::setStyle() is what ever reconstructs it,
        // never a generic Property::set().
        .property("style", Scope::Public, selectOverload<ViewStyle&(View::*)()>(&View::style))
        // Polymorphic collection of SubView* - View::childViews() only
        // ever exposes a *const* reference (no NEWUI_REFLECT_PRIVATE() on
        // View to reach childViews_ itself), so there's no real, gettable/
        // addressable std::vector<SubView*> this could ever hand back -
        // propertyCollection() is exactly the mode for that: count/index
        // access for write() are derived internally from the one real
        // &View::childViews accessor (container_traits::count()/
        // getByIndex() - see propertyCollection()'s own comment), and
        // add/remove are literally &View::addChild/&View::removeChild -
        // real methods, called directly by read(), one child at a time -
        // never a raw container mutation that would skip whatever
        // invariants those methods maintain (parent/rootView linkage,
        // layout invalidation).
        .propertyCollection("childViews", Scope::Public,
            &View::childViews, &View::addChild, &View::removeChild);
    ReflectionRegistry::registerClass(builder);
}

void registerSubViewReflection() {
    ClassBuilder<SubView> builder;
    builder.clazz()
        .base<View>()
        // Re-declares bounds/visible with real setters (SubView::setBounds()/
        // setVisible()) - TypedClass<T>::allProperties() (reflection.h)
        // walks SubView before View, so these two shadow View's get-only
        // versions whenever the object's registered class is SubView
        // itself. Giving an explicit setter
        // always selects the by-value read/write-by-copy shape (see
        // ClassBuilder::property()'s comment), regardless of what the
        // getter itself returns.
        .property("bounds", Scope::Public, &SubView::bounds, &SubView::setBounds)
        .property("visible", Scope::Public, &SubView::isVisible, &SubView::setVisible)
        .constructor<>();
    ReflectionRegistry::registerClass(builder);
}

// No fields of its own for this minimal slice - registered purely so
// ObjectWriter can resolve Frame::getView()'s real runtime type
// (RootView, not just View) via ReflectionRegistry::getClass(typeid(RootView)).
// Nothing ever createInstance()s a RootView, so it gets no .constructor<>().
void registerRootViewReflection() {
    ClassBuilder<RootView> builder;
    builder.clazz().base<View>();
    ReflectionRegistry::registerClass(builder);
}

void registerFrameReflection() {
    ClassBuilder<Frame> builder;
    builder.clazz()
        .property("title", Scope::Public, &Frame::getTitle, &Frame::setTitle)
        .property("bounds", Scope::Public, &Frame::getBounds, &Frame::setBounds)
        // Frame::getView() is overloaded on constness, so it needs the
        // same selectOverload<>() treatment "style" does above - written,
        // never reconstructed, see ObjectReader::read()'s own comment
        // (reflectionio.h) and demoWriter()'s.
        .property("rootView", Scope::Public, selectOverload<RootView&(Frame::*)()>(&Frame::getView));
    ReflectionRegistry::registerClass(builder);
}

void registerApplicationReflection() {
    ClassBuilder<Application> builder;
    builder.clazz()
        .property("name", Scope::Public, &Application::getName, &Application::setName)
        // Application::getFrame() is overloaded on constness (needs
        // selectOverload<>(), same as "rootView" above) and returns a
        // possibly-null Frame* rather than a live Frame& - style()'s
        // RefGetter shape assumes an always-present object, which isn't
        // true here before setFrame() is ever called, so this getter's
        // plain-pointer return routes ClassBuilder::property() to
        // TypedProperty's PtrGetter shape instead (see its own comment) -
        // address()/get() are both null-safe there. setFrame() itself
        // really does reassign which Frame the Application points at (not
        // "copy a Frame value into the existing one"), so it's registered
        // as a real PtrSetter - ObjectReader::read() never actually calls
        // set() on "frame" today (Application is a singleton with no
        // fresh instance to build in the first place, and Frame has no
        // registered constructor regardless - see demoWriter()'s own
        // comment), but the machinery is real: Class::createInstance()
        // already boxes a freshly-built Frame* in exactly the shape
        // PtrSetter's set() expects, no extra unboxing needed.
        .property("frame", Scope::Public,
            selectOverload<Frame*(Application::*)()>(&Application::getFrame),
            &Application::setFrame);
    ReflectionRegistry::registerClass(builder);
}

void registerReflection() {
    registerRectReflection();
    registerViewStyleReflection();
    registerViewReflection();
    registerSubViewReflection();
    registerRootViewReflection();
    registerFrameReflection();
    registerApplicationReflection();
}


// Returns panel (still owned by root - not detached) so demoRoundTrip()
// (below) can compare a fresh reconstruction against it without having to
// rebuild the same tree a second time.
SubView* demoWriter()
{

    Application& app = Application::instance();
    app.setName("Reflection2Demo");

    Frame* frame = new Frame();
    frame->setTitle("Reflection2 Demo Window");
    frame->setBounds(Rect(100.0f, 100.0f, 640.0f, 480.0f));
    app.setFrame(frame);

    RootView& root = frame->getView();
    root.style().borderWidth = 2.0f;
    root.style().opacity = 1.0f;

    SubView* panel = new SubView();
    panel->setName("panel");
    panel->setBounds(Rect(10.0f, 10.0f, 300.0f, 200.0f));
    panel->style().borderWidth = 1.0f;
    panel->style().opacity = 0.9f;
    root.addChild(panel);

    SubView* child1 = new SubView();
    child1->setName("child1");
    child1->setBounds(Rect(0.0f, 0.0f, 100.0f, 40.0f));
    child1->style().borderWidth = 0.5f;
    panel->addChild(child1);

    SubView* child2 = new SubView();
    child2->setName("child2");
    child2->setBounds(Rect(0.0f, 50.0f, 100.0f, 40.0f));
    child2->style().borderWidth = 0.75f;
    panel->addChild(child2);

    SubView* sibling = new SubView();
    sibling->setName("sibling");
    sibling->setBounds(Rect(320.0f, 10.0f, 150.0f, 60.0f));
    root.addChild(sibling);

    // 2. Write the whole tree (Application -> frame -> rootView ->
    // childViews, recursively) via reflection.

    ObjectWriter objWrite;
    objWrite.metadata.author = "Jim Crafton";
    objWrite.metadata.copyright = "Copyright (c) 2026 Jim Crafton";
    objWrite.write(&app);

    std::string text = json5::to_string(objWrite.doc);

    // 3. Read it back - into the SAME live app/frame/root tree just built
    // above. Only Application's own "name" actually gets re-applied here:
    // "frame" is CreatedOnHeap (see TypedProperty::read()'s own comment -
    // any PtrGetter-backed property always is), and registerFrameReflection()
    // deliberately never registers a .constructor<>() for Frame - matching
    // this file's own long-standing "frame/rootView are written but never
    // reconstructed" design - so Class::createInstance() finds no matching
    // constructor, TypedClass<Frame>::read() bails out immediately, and
    // nothing under "frame" (title/bounds/rootView/childViews) is ever
    // touched. root's own child count below should therefore come back
    // completely unchanged, not duplicated - if a future pass gives Frame
    // a real constructor so that subtree becomes reachable, see
    // ObjectReader::read()'s own comment for what to expect from
    // "childViews" once it is: addChild()-only reconstruction duplicates
    // existing children rather than replacing them, since nothing here
    // clears root/panel first.
    ObjectReader objReader;
    json5::error err = json5::from_string(text, objReader.doc);
    if (err) {
        std::cout << "  ObjectReader: failed to re-parse written JSON5\n";
        return panel;
    }
    objReader.read(&app);

    std::cout << "  --- ObjectReader read back OK - app.getName()=\"" << app.getName()
               << "\", root still has " << root.childViews().size() << " children "
               << "(unchanged - \"frame\" was never reconstructed, see this function's own comment) ---\n";
    std::cout << "  --- metadata read back: author=\"" << objReader.metadata.author
               << "\", date=\"" << objReader.metadata.date
               << "\", copyright=\"" << objReader.metadata.copyright
               << "\", version=\"" << objReader.metadata.version << "\" ---\n";

    return panel;
}

// ---------------------------------------------------------------------
// Demos
// ---------------------------------------------------------------------

void demoRegisterReflection() {
    std::cout << "\n== Demo 1: register Application/Frame/View/SubView/RootView/ViewStyle ==\n";

    

    for (const char* name : { "Application", "Frame", "View", "SubView", "RootView", "ViewStyle" }) {
        const Class* cls = classinfo(name);
        std::cout << "  " << name << ": registered=" << std::boolalpha << (cls != nullptr)
                   << ", properties=" << (cls ? cls->properties().size() : 0) << "\n";
    }
}

// Real field-level round-trip, via the generic ObjectWriter/ObjectReader
// pipeline (reflectionio.h) end to end - unlike demoWriter()'s own
// read-back (which reads into the *same* live app/frame/root, and never
// even reaches "rootView" since "frame" has no registered constructor -
// see its own comment), this writes `panel` (still root's child - not
// detached) *standalone*, so the written JSON5's own root is "panel"
// itself, and reads it back into a brand new, disconnected SubView -
// genuinely exercising every read path this session's work touched:
// Rect's new setLeft()/setTop()/setWidth()/setHeight()-backed properties
// ("bounds"), an addressable single nested property read in place
// ("style", via View::style()'s live ViewStyle&), and - the one this
// whole design exists for - "childViews"' accessor-fn (CountFn/GetAtFn/
// AddFn/RemoveFn) reconstruction, driving freshPanel.addChild() for real,
// one child at a time, exactly the way the live tree itself was built.
void demoRoundTrip(SubView* panel) {
    std::cout << "\n== Demo 2: write `panel` standalone, read it back into a "
                  "fresh SubView, verify field by field ==\n";

    ObjectWriter panelWriter;
    panelWriter.write(panel);
    std::string text = json5::to_string(panelWriter.doc);

    ObjectReader panelReader;
    json5::error err = json5::from_string(text, panelReader.doc);
    if (err) {
        std::cout << "  ObjectReader: failed to re-parse written JSON5\n";
        return;
    }

    // A fresh, disconnected SubView - not addChild()'d to anything, so
    // panelReader.read() below is the only thing that ever populates it.
    SubView freshPanel;
    panelReader.read(&freshPanel);

    auto check = [](const char* label, bool ok) {
        std::cout << "  [" << (ok ? "PASS" : "FAIL") << "] " << label << "\n";
    };

    check("name matches", freshPanel.name() == panel->name());
    check("bounds matches", freshPanel.bounds() == panel->bounds());
    check("style.borderWidth matches", freshPanel.style().borderWidth == panel->style().borderWidth);
    check("style.opacity matches", freshPanel.style().opacity == panel->style().opacity);
    check("child count matches", freshPanel.childViews().size() == panel->childViews().size());
    check("grandchild names match",
        freshPanel.childViews().size() == 2 &&
        freshPanel.childViews()[0]->name() == "child1" && freshPanel.childViews()[1]->name() == "child2");
    check("grandchild bounds match",
        freshPanel.childViews().size() == 2 &&
        freshPanel.childViews()[0]->bounds() == panel->childViews()[0]->bounds() &&
        freshPanel.childViews()[1]->bounds() == panel->childViews()[1]->bounds());

    // freshPanel is a stack local, never addChild()'d anywhere -
    // View::destroy() only ever deletes *children* (see its own comment),
    // never `this`, so this is safe and just tears down the two heap
    // SubViews readAndAddItem() created via addChild() above.
    freshPanel.destroy();
}

int main() {
    std::cout << "newui " << newui::version() << " - reflection2: prototype reflection-driven serialization\n";
    
    registerReflection();

    demoRegisterReflection();

    SubView* panel = demoWriter();
    demoRoundTrip(panel);

    return 0;
}
