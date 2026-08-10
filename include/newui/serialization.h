#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>

#include <newui/subview.h>
#include <newui/viewstyle.h>
#include <newui/layout.h>
#include <newui/frame.h>
#include <newui/application.h>

namespace newui {

    // Name -> factory registry for the polymorphic types that can appear
    // as JSON5 nodes: SubView, ViewStyle, Layout, LayoutParams. Not needed
    // for View/RootView/Frame/Application themselves - the caller loading
    // a file always already has the concrete target object in hand (see
    // loadViewTree()/loadFrame()/loadApplication()); the registry's job is
    // only to decide *which* concrete type to construct for the
    // recursively-nested, actually-polymorphic parts (style, layout,
    // layoutParams, and children).
    class SerializationRegistry {
    public:
        // T must derive from exactly one of SubView/ViewStyle/Layout/
        // LayoutParams, and be default-constructible - both enforced by
        // static_assert, checked at the call site's instantiation. The
        // registered name is T's own class name (via demangleTypeName()),
        // not a caller-supplied string - see uicomponent.h's typeName()
        // comment for why that can't drift out of sync.
        template <typename T>
        static void registerType() {
            instance().registerTypeOnSelf<T>();
        }

        // Internal - used by the tree walker in serialization.cpp. Return
        // nullptr/empty unique_ptr if name isn't registered.
        static SubView* createSubView(const std::string& name);
        static std::unique_ptr<ViewStyle> createStyle(const std::string& name);
        static std::unique_ptr<Layout> createLayout(const std::string& name);
        static std::unique_ptr<LayoutParams> createLayoutParams(const std::string& name);


        template< typename T>
        static T* createInstancePtr(const std::string& name) {
            T* result = nullptr;

            auto& inst = SerializationRegistry::instance();
            auto found = inst.factories_.find(name);
            if (found != inst.factories_.end()) {
                auto createFn = found->second;
                auto ptr = createFn();
                result = dynamic_cast<T*>(ptr.get());
                ptr.release();
            }

            return result;
        }

        template< typename T>
        static std::unique_ptr<T> createInstance(const std::string& name) {
            std::unique_ptr<T> result;

            T* p = SerializationRegistry::createInstancePtr<T>(name);
            if (nullptr != p) {
                result = std::unique_ptr<T>(p);
            }


            return result;
        }

    private:
        // Registers the library's own concrete types (SubView, ViewStyle/
        // ButtonStyle/LabelStyle/CheckBoxStyle, AnchorLayout/StackLayout/
        // CardLayout, AnchorLayoutParams/StackLayoutParams) directly on
        // *this - see serialization.cpp. Deliberately doesn't go through
        // the public registerType<T>() (which calls instance()) to avoid
        // recursively re-entering instance() while its function-local
        // static is still being constructed (undefined behavior).
        SerializationRegistry();

        static SerializationRegistry& instance();  // Meyer's singleton, private - an implementation detail

        // Single map, single UIComponent-typed factory shape - every
        // registered type ultimately just needs "make a blank one of
        // these"; which of SubView/ViewStyle/Layout/LayoutParams it turns
        // out to be is resolved by the create*() functions below via
        // dynamic_cast (UIComponent is polymorphic), not by which map it
        // was inserted into.
        template <typename T>
        void registerTypeOnSelf() {
            static_assert(std::is_default_constructible_v<T>,
                "SerializationRegistry::registerType<T>: T must be default-constructible");
            static_assert(
                std::is_base_of_v<SubView, T> || std::is_base_of_v<ViewStyle, T> ||
                std::is_base_of_v<Layout, T> || std::is_base_of_v<LayoutParams, T>,
                "SerializationRegistry::registerType<T>: T must derive from "
                "SubView, ViewStyle, Layout, or LayoutParams");

            factories_[demangleTypeName(typeid(T))] = [] { return std::unique_ptr<UIComponent>(new T()); };
        }


        

        // Default-constructs the type registered under name, or nullptr if
        // name isn't registered. create*() below downcast this via
        // dynamic_cast to the kind they need.
        std::unique_ptr<UIComponent> create(const std::string& name) const;

        std::unordered_map<std::string, std::function<std::unique_ptr<UIComponent>()>> factories_;
    };

    // Serializes view (and its childViews(), recursively) to JSON5 text -
    // own fields (name/visible/bounds) + style + layout (if attached) +
    // children (each tagged with "type", and "layoutParams" if set).
    std::string saveViewTree(const View& view);
    bool saveViewTreeToFile(const View& view, const std::string& path);

    // Populates target's own fields/style/layout and rebuilds its children
    // from parsed JSON5 text (existing children are destroyed/replaced
    // first). target itself is never reconstructed - a RootView can't be
    // (it owns a live HWND tied to its Frame) - so this always applies
    // onto an already-initialize()'d node the caller already has.
    // Returns false (leaving target only partially updated) on a parse
    // error or an unrecognized "type" anywhere in the tree.
    bool loadViewTree(View& target, const std::string& json5Text);
    bool loadViewTreeFromFile(View& target, const std::string& path);

    // Frame's own fields (title/bounds/show-state) + its RootView's node
    // under "view" - see Frame::writeFields()/readFields() for the
    // WINDOWPLACEMENT-based show-state handling and its live-HWND
    // requirement (readFields()'s show-state half only works once
    // frame.initialize() has run - see Frame::onCreated).
    std::string saveFrame(const Frame& frame);
    bool saveFrameToFile(const Frame& frame, const std::string& path);
    bool loadFrame(Frame& target, const std::string& json5Text);
    bool loadFrameToFile(Frame& target, const std::string& path);

    // Application's own fields (the custom-data bag) + its Frame (if set)
    // under "frame".
    std::string saveApplication(const Application& app);
    bool saveApplicationToFile(const Application& app, const std::string& path);
    bool loadApplication(Application& app, const std::string& json5Text);
    bool loadApplicationToFile(Application& app, const std::string& path);

}
