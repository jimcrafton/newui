#include "newui/serialization.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>
#include <json5/json5_input.hpp>
#include <json5/json5_output.hpp>

namespace newui {

    SerializationRegistry::SerializationRegistry() {
        registerTypeOnSelf<SubView>();

        registerTypeOnSelf<ViewStyle>();
        registerTypeOnSelf<ButtonStyle>();
        registerTypeOnSelf<LabelStyle>();
        registerTypeOnSelf<CheckBoxStyle>();

        registerTypeOnSelf<AnchorLayout>();
        registerTypeOnSelf<StackLayout>();
        registerTypeOnSelf<CardLayout>();

        registerTypeOnSelf<AnchorLayoutParams>();
        registerTypeOnSelf<StackLayoutParams>();
    }

    SerializationRegistry& SerializationRegistry::instance() {
        static SerializationRegistry registry;
        return registry;
    }

    std::unique_ptr<UIComponent> SerializationRegistry::create(const std::string& name) const {
        auto it = factories_.find(name);
        return it != factories_.end() ? it->second() : nullptr;
    }

    /*

    SubView* SerializationRegistry::createSubView(const std::string& name) {
        std::unique_ptr<UIComponent> c = instance().create(name);
        SubView* result = dynamic_cast<SubView*>(c.get());
        if (result == nullptr) {
            return nullptr;  // unregistered name, or registered as a different kind
        }
        c.release();  // ownership now the caller's, raw - matches SubView's own new/delete convention
        return result;
    }

    std::unique_ptr<ViewStyle> SerializationRegistry::createStyle(const std::string& name) {
        std::unique_ptr<UIComponent> c = instance().create(name);
        ViewStyle* result = dynamic_cast<ViewStyle*>(c.get());
        if (result == nullptr) {
            return nullptr;
        }
        c.release();
        return std::unique_ptr<ViewStyle>(result);
    }

    std::unique_ptr<Layout> SerializationRegistry::createLayout(const std::string& name) {
        std::unique_ptr<UIComponent> c = instance().create(name);
        Layout* result = dynamic_cast<Layout*>(c.get());
        if (result == nullptr) {
            return nullptr;
        }
        c.release();
        return std::unique_ptr<Layout>(result);
    }

    std::unique_ptr<LayoutParams> SerializationRegistry::createLayoutParams(const std::string& name) {
        std::unique_ptr<UIComponent> c = instance().create(name);
        LayoutParams* result = dynamic_cast<LayoutParams*>(c.get());
        if (result == nullptr) {
            return nullptr;
        }
        c.release();
        return std::unique_ptr<LayoutParams>(result);
    }
    */

}

namespace {

    using namespace newui;

    // Writes c's own fields (via its writeFields()) wrapped in an object
    // tagged with its typeName(). Used for style/layout/layoutParams -
    // anything that's just "one UIComponent, no children to recurse into".
    // w must have a scope currently open (this doesn't touch it) - opens
    // and closes its own nested scope for c.
    json5::value writeComponentNode(json5::builder& w, const UIComponent& c) {
        w.push_object();
        w["type"] = w.new_string(c.typeName());
        c.writeFields(w);
        return w.pop();
    }

    // Fills w's currently-open object scope with view's node content: own
    // fields (name/visible/bounds, via View::writeFields()), style,
    // layout (if attached), and a "children" array (each entry tagged
    // with "type", recursing into this same function, plus
    // "layoutParams" if the child has any). Caller opens/closes the
    // outer scope (push_object()/pop()) - this only fills it, so it works
    // identically for the top-level target (see writeViewNode() below,
    // no "type"/"layoutParams" of its own) and for each child (which gets
    // those added by the loop below, around the recursive call).
    void writeViewNodeFields(json5::builder& w, const View& view) {
        view.writeFields(w);
        w["style"] = writeComponentNode(w, view.style());

        if (view.layout() != nullptr) {
            w["layout"] = writeComponentNode(w, *view.layout());
        }

        w.push_array();
        for (const SubView* child : view.childViews()) {
            w.push_object();
            w["type"] = w.new_string(child->typeName());
            writeViewNodeFields(w, *child);
            if (child->layoutParams() != nullptr) {
                w["layoutParams"] = writeComponentNode(w, *child->layoutParams());
            }
            w += w.pop();
        }
        w["children"] = w.pop();
    }

    json5::value writeViewNode(json5::builder& w, const View& view) {
        w.push_object();
        writeViewNodeFields(w, view);
        return w.pop();
    }

    // Inverse of writeViewNodeFields(): populates target's own fields,
    // style, and layout from obj, then destroys target's existing
    // children and rebuilds them from obj["children"]. Doesn't touch
    // target's own "type" - target is already the correct concrete
    // object, supplied by the caller (see loadViewTree()). Returns false
    // (target left partially updated) on a malformed node or an
    // unrecognized style/layout/layoutParams/child "type".
    bool readViewNodeInto(View& target, const json5::value& obj) {
        if (!obj.is_object()) {
            return false;
        }

        target.readFields(obj);

        json5::value styleVal = obj["style"];
        if (!styleVal.is_object()) {
            return false;
        }
        
        std::unique_ptr<ViewStyle> style = SerializationRegistry::createInstance<ViewStyle>(styleVal["type"].get_c_str(""));

            //SerializationRegistry::createStyle(styleVal["type"].get_c_str(""));
        if (!style) {
            return false;
        }
        style->readFields(styleVal);
        target.setStyle(std::move(style));

        if (json5::value layoutVal = obj["layout"]; layoutVal.is_object()) {
            std::unique_ptr<Layout> layout = SerializationRegistry::createInstance<Layout>(layoutVal["type"].get_c_str(""));
            if (!layout) {
                return false;
            }
            layout->readFields(layoutVal);
            target.setLayout(std::move(layout));
        } else {
            target.setLayout(nullptr);
        }

        // Destroy existing children before rebuilding - same convention
        // as View::destroy(), just scoped to children only (target itself
        // isn't being destroyed, so its own onDestroyed doesn't fire).
        std::vector<SubView*> oldChildren(target.childViews().begin(), target.childViews().end());
        for (SubView* child : oldChildren) {
            target.removeChild(child);
            child->destroy();
            delete child;
        }

        json5::array_view children(obj["children"]);
        for (const json5::value& childVal : children) {
            if (!childVal.is_object()) {
                return false;
            }

            SubView* child = SerializationRegistry::createInstancePtr<SubView>(childVal["type"].get_c_str(""));
            if (child == nullptr) {
                return false;
            }

            if (!readViewNodeInto(*child, childVal)) {
                delete child;
                return false;
            }

            target.addChild(child);

            if (json5::value paramsVal = childVal["layoutParams"]; paramsVal.is_object()) {
                auto params =
                    SerializationRegistry::createInstance<LayoutParams>(paramsVal["type"].get_c_str(""));
                if (!params) {
                    return false;
                }
                params->readFields(paramsVal);
                child->setLayoutParams(std::move(params));
            }
        }

        return true;
    }

    // Frame's own fields + its RootView's node under "view" - the
    // composable primitive shared by saveFrame() (top-level document) and
    // saveApplication() (nested under "frame").
    json5::value writeFrameNode(json5::builder& w, const Frame& frame) {
        w.push_object();
        frame.writeFields(w);
        w["view"] = writeViewNode(w, frame.getView());
        return w.pop();
    }

    bool readFrameNodeInto(Frame& target, const json5::value& obj) {
        if (!obj.is_object()) {
            return false;
        }

        target.readFields(obj);
        return readViewNodeInto(target.getView(), obj["view"]);
    }

}

namespace newui {

    std::string saveViewTree(const View& view) {
        json5::document doc;
        json5::builder w(doc);
        writeViewNode(w, view);
        return json5::to_string(doc);
    }

    bool saveViewTreeToFile(const View& view, const std::string& path) {
        json5::document doc;
        json5::builder w(doc);
        writeViewNode(w, view);
        return json5::to_file(path, doc);
    }

    bool loadViewTree(View& target, const std::string& json5Text) {
        json5::document doc;
        if (json5::from_string(json5Text, doc)) {
            return false;
        }
        return readViewNodeInto(target, doc);
    }

    bool loadViewTreeFromFile(View& target, const std::string& path) {
        json5::document doc;
        if (json5::from_file(path, doc)) {
            return false;
        }
        return readViewNodeInto(target, doc);
    }

    std::string saveFrame(const Frame& frame) {
        json5::document doc;
        json5::builder w(doc);
        writeFrameNode(w, frame);
        return json5::to_string(doc);
    }

    bool saveFrameToFile(const Frame& frame, const std::string& path) {
        json5::document doc;
        json5::builder w(doc);
        writeFrameNode(w, frame);
        return json5::to_file(path, doc);
    }

    bool loadFrame(Frame& target, const std::string& json5Text) {
        json5::document doc;
        if (json5::from_string(json5Text, doc)) {
            return false;
        }
        return readFrameNodeInto(target, doc);
    }

    bool loadFrameToFile(Frame& target, const std::string& path) {
        json5::document doc;
        if (json5::from_file(path, doc)) {
            return false;
        }
        return readFrameNodeInto(target, doc);
    }

    std::string saveApplication(const Application& app) {
        json5::document doc;
        json5::builder w(doc);
        w.push_object();
        app.writeFields(w);
        if (app.getFrame() != nullptr) {
            w["frame"] = writeFrameNode(w, *app.getFrame());
        }
        w.pop();
        return json5::to_string(doc);
    }

    bool saveApplicationToFile(const Application& app, const std::string& path) {
        json5::document doc;
        json5::builder w(doc);
        w.push_object();
        app.writeFields(w);
        if (app.getFrame() != nullptr) {
            w["frame"] = writeFrameNode(w, *app.getFrame());
        }
        w.pop();
        return json5::to_file(path, doc);
    }

    bool loadApplication(Application& app, const std::string& json5Text) {
        json5::document doc;
        if (json5::from_string(json5Text, doc)) {
            return false;
        }

        app.readFields(doc);

        if (json5::value frameVal = doc["frame"]; frameVal.is_object()) {
            if (app.getFrame() == nullptr) {
                return false;
            }
            return readFrameNodeInto(*app.getFrame(), frameVal);
        }

        return true;
    }

    bool loadApplicationToFile(Application& app, const std::string& path) {
        json5::document doc;
        if (json5::from_file(path, doc)) {
            return false;
        }

        app.readFields(doc);

        if (json5::value frameVal = doc["frame"]; frameVal.is_object()) {
            if (app.getFrame() == nullptr) {
                return false;
            }
            return readFrameNodeInto(*app.getFrame(), frameVal);
        }

        return true;
    }

}
