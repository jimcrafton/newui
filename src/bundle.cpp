#include "newui/bundle.h"
#include "newui/animation.h"
#include "newui/application.h"
#include "newui/dialogs.h"
#include "newui/frame.h"
#include "newui/reflectionio.h"
#include "newui/rootview.h"
#include "newui/view.h"
#include "newui/viewpath.h"
#include "newui/viewstyle.h"

#include <json5/json5.hpp>
#include <json5/json5_input.hpp>

#include <any>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <typeindex>
#include <vector>

namespace {

    // GetModuleFileNameA's buffer-too-small signal is "returned length ==
    // buffer size" (and, since Vista, ERROR_INSUFFICIENT_BUFFER) - grow
    // and retry rather than guessing a size upfront.
    std::string getExecutablePath() {
        std::vector<char> buffer(MAX_PATH);

        for (;;) {
            DWORD len = ::GetModuleFileNameA(nullptr, buffer.data(), DWORD(buffer.size()));
            if (len == 0) {
                return std::string();
            }
            if (len < buffer.size()) {
                return std::string(buffer.data(), len);
            }
            if (buffer.size() > 32768) {
                return std::string();  // pathological - bail rather than growing forever
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::string directoryOf(const std::string& path) {
        size_t pos = path.find_last_of("\\/");
        return pos == std::string::npos ? std::string() : path.substr(0, pos);
    }

    bool fileExists(const std::string& path) {
        DWORD attrs = ::GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    // Writes clazz's own properties and public fields for instancePtr
    // into writer's currently-open object scope - the properties/fields
    // portion of what newui::reflection::TypedClass<T>::write()
    // (reflection.h) does, reusable here without a compile-time T (clazz
    // is resolved at runtime - see Bundle::writeFrame()/writeView(), both
    // of which need the instance's *real* runtime class, not whatever
    // static type the caller holds it as). Deliberately skips the
    // "delegates" block TypedClass<T>::write() also writes - a described,
    // reconnectable Delegate<> connection is a rare, code-driven feature
    // (see reflectionio.h's own comment on it), not something a saved UI
    // layout file normally carries; every real Bundle::write*() caller
    // today has nothing connected that way regardless, so there is
    // nothing to lose in practice, only unused complexity to skip.
    void writeInstanceBody(const newui::reflection::Class* clazz, void* instancePtr,
                             newui::reflection::ObjectWriter& writer) {
        // See newui::reflection::ClassWriter::enterInstance()'s own comment
        // - mirrors the same guard TypedClass<T>::write() (reflection.h)
        // applies around its own properties/fields walk, since this
        // function is deliberately a hand-inlined copy of that walk (see
        // this function's own header comment for why) rather than a call
        // through it.
        if (!writer.enterInstance(clazz, instancePtr)) {
            return;
        }

        std::vector<const newui::reflection::Property*> properties;
        clazz->allProperties(properties);

        // See newui::reflection::collectionElementAddresses()'s own
        // comment - mirrors the same dedup TypedClass<T>::write()
        // (reflection.h) applies, for the same reason this function's own
        // header comment gives for the enterInstance() guard above.
        std::set<void*> reachableElsewhere = newui::reflection::collectionElementAddresses(instancePtr, properties);
        for (const auto* property : properties) {
            void* val = property->addressableValue(instancePtr);
            if (val != nullptr && reachableElsewhere.count(val) != 0) {
                continue;
            }
            property->write(instancePtr, &writer);
        }

        std::vector<const newui::reflection::Field*> fields;
        clazz->allFields(fields);
        for (const auto* field : fields) {
            if (field->scope() == newui::reflection::Scope::Public) {
                field->write(instancePtr, &writer);
            }
        }

        writer.exitInstance(clazz, instancePtr);
    }

    // ---------------------------------------------------------------
    // Animation persistence - see HANDOFF.md's own entry on this pass for
    // the full design. AnimationManager::animations() (animation.h) has
    // no reflectgen registration of its own (a private-constructor
    // singleton, never meant to be createInstance()'d generically) and a
    // KeyValue's actual target - a live ObservableProperty<SourceT,
    // ValueT>* - has no serializable identity beyond what
    // PropertyBase::sourceType()/reflectionProperty() (property.h) now
    // expose, so this whole "animations" block is hand-written, the same
    // way TypedClass<T>::write()'s own "delegates" block (reflection.h) is
    // - not run through Class::write()'s generic per-property walk at all.
    //
    // Scope (see the approved plan): a KeyValue whose target lives
    // directly on a View/SubView, or on that View's own style() -
    // anything else (a field-backed property with no reflection identity
    // at all, or a reflection-backed one whose source isn't reachable this
    // way - e.g. shapes::Transform2D) is silently left out, the same
    // "unrecognized data is silently skipped" contract the rest of this
    // codebase's own reflection read/write paths already have.

    bool isKindOf(const newui::reflection::Class* clazz, const newui::reflection::Class* base) {
        for (const newui::reflection::Class* c = clazz; c != nullptr; c = c->parentClass()) {
            if (c == base) {
                return true;
            }
        }
        return false;
    }

    // "" (with a true return) for target == root itself, computeViewPath()
    // (viewpath.h) otherwise - resolves the "empty path is ambiguous
    // between 'root' and 'not found'" question viewpath.h's own comment
    // flags, since this caller needs to tell those apart.
    bool computeViewPathOrRoot(newui::RootView& root, newui::View* target, std::string& outPath) {
        if (target == static_cast<newui::View*>(&root)) {
            outPath.clear();
            return true;
        }
        outPath = newui::computeViewPath(root, target);
        return !outPath.empty();
    }

    // "<view-path>@<propertyName>" for a target directly on a View, or
    // "<view-path>@style.<propertyName>" for one on that View's own
    // style() - "" if this KeyValue's target isn't serializable at all
    // (field-backed - reflectionProperty() is nullptr - or reachable
    // through neither shape). No separate "which class actually declares
    // this property" field is written - Class::property(name) only checks
    // its own class's properties_, never a base's (see reflection.cpp), so
    // the *exact* dynamic type has to be rediscovered at read time anyway
    // (typeid(*view)/typeid(style) - always the object's own real runtime
    // type, View/ViewStyle both being polymorphic) - writing it here too
    // would just be redundant, never-checked data.
    std::string computeAnimationTargetDescriptor(newui::RootView& root, const newui::PropertyBase* pb) {
        using namespace newui;
        using namespace newui::reflection;

        const Property* reflProp = pb->reflectionProperty();
        if (reflProp == nullptr) {
            return std::string();
        }

        const Class* sourceClass = classinfo(pb->sourceType());
        if (sourceClass == nullptr) {
            return std::string();
        }

        const Class* viewClass = classinfo<View>();
        if (isKindOf(sourceClass, viewClass)) {
            View* view = static_cast<View*>(pb->source());
            std::string path;
            if (!computeViewPathOrRoot(root, view, path)) {
                return std::string();
            }
            return path + "@" + pb->name();
        }

        const Class* viewStyleClass = classinfo<ViewStyle>();
        if (viewStyleClass != nullptr && isKindOf(sourceClass, viewStyleClass)) {
            ViewStyle* style = static_cast<ViewStyle*>(pb->source());
            View* owner = style->view();
            if (owner == nullptr) {
                return std::string();
            }
            std::string path;
            if (!computeViewPathOrRoot(root, owner, path)) {
                return std::string();
            }
            return path + "@style." + pb->name();
        }

        return std::string();
    }

    struct ResolvedAnimationTarget {
        void* source = nullptr;
        std::type_index sourceType = std::type_index(typeid(void));
        std::string propertyName;
    };

    // Read-side mirror of computeAnimationTargetDescriptor() above - false
    // if descriptor is malformed, its view-path doesn't resolve under
    // root, or it names an unsupported hop (anything but "style").
    bool resolveAnimationTarget(newui::RootView& root, const std::string& descriptor, ResolvedAnimationTarget& out) {
        std::size_t at = descriptor.find('@');
        if (at == std::string::npos) {
            return false;
        }
        std::string viewPath = descriptor.substr(0, at);
        std::string propPath = descriptor.substr(at + 1);

        newui::View* view = newui::resolveViewPath(root, viewPath);
        if (view == nullptr) {
            return false;
        }

        std::size_t dot = propPath.find('.');
        if (dot == std::string::npos) {
            // dynamic_cast<void*>, not a plain View*-to-void* conversion -
            // the latter erases through View's own (base) address, and
            // AnimationTargetRegistry::buildKeyValue()'s factory later
            // does static_cast<SourceT*>(source) with SourceT the
            // *derived* class actually registered (e.g. Slider) - casting
            // a void* that was only ever really a View* through that
            // static_cast applies zero offset adjustment, silently wrong
            // (real, reproduced crash: garbage-filled ObservableProperty
            // member reads) unless the derived class's own subobject
            // genuinely starts at the same address as View's. dynamic_cast
            // <void*> instead resolves to the address of the *complete*
            // (most-derived) object via RTTI, which is exactly what a
            // later static_cast<Slider*> (Slider being that same complete
            // type - confirmed by the sourceType lookup below) needs.
            out.source = dynamic_cast<void*>(view);
            out.sourceType = std::type_index(typeid(*view));
            out.propertyName = propPath;
            return true;
        }

        if (propPath.compare(0, dot, "style") != 0) {
            return false;
        }
        newui::ViewStyle& style = view->style();
        out.source = dynamic_cast<void*>(&style);
        out.sourceType = std::type_index(typeid(style));
        out.propertyName = propPath.substr(dot + 1);
        return true;
    }

    // Small, closed dispatch over the ValueT shapes ObservableProperty<
    // SourceT,ValueT> actually supports today (see property.h's own
    // IsPodLike) - only float is exercised by any real call site as of
    // this writing (see the approved plan's own note on that), but this
    // stays open to whatever a future AnimationTargetRegistry::
    // registerTarget<SourceT,ValueT>() call needs. Mirrors reflection.cpp's
    // own Property::writeValue()/readValue() dispatch shape, just over
    // KeyValue::boxedValue()'s std::any instead of a reflection::Property.
    void writeBoxedAnimationValue(newui::reflection::ObjectWriter& writer, const std::string& name, const std::any& value) {
        if (value.type() == typeid(float)) {
            writer.writeFloat(name, std::any_cast<float>(value));
        } else if (value.type() == typeid(double)) {
            writer.writeDouble(name, std::any_cast<double>(value));
        } else if (value.type() == typeid(int)) {
            writer.writeInt32(name, static_cast<std::uint32_t>(std::any_cast<int>(value)), true);
        } else if (value.type() == typeid(bool)) {
            writer.writeBool(name, std::any_cast<bool>(value));
        } else {
            std::cerr << "Bundle::writeFrame(): animation key value '" << name << "' holds an unsupported "
                          "boxed type (" << value.type().name() << ") - skipped.\n";
        }
    }

    std::any readBoxedAnimationValue(newui::reflection::ObjectReader& reader, const std::string& name, std::type_index valueType) {
        if (valueType == typeid(float)) {
            float v = 0.0f;
            reader.readFloat(name, v);
            return std::any(v);
        }
        if (valueType == typeid(double)) {
            double v = 0.0;
            reader.readDouble(name, v);
            return std::any(v);
        }
        if (valueType == typeid(int)) {
            std::int32_t v = 0;
            reader.readInt(name, v);
            return std::any(static_cast<int>(v));
        }
        if (valueType == typeid(bool)) {
            bool v = false;
            reader.readBool(name, v);
            return std::any(v);
        }
        return std::any();
    }

    std::string interpolationKindToString(newui::InterpolationKind kind) {
        switch (kind) {
            case newui::InterpolationKind::EaseIn: return "EaseIn";
            case newui::InterpolationKind::EaseOut: return "EaseOut";
            case newui::InterpolationKind::EaseInOut: return "EaseInOut";
            case newui::InterpolationKind::Linear:
            default: return "Linear";
        }
    }

    newui::InterpolationKind interpolationKindFromString(const std::string& s) {
        if (s == "EaseIn") return newui::InterpolationKind::EaseIn;
        if (s == "EaseOut") return newui::InterpolationKind::EaseOut;
        if (s == "EaseInOut") return newui::InterpolationKind::EaseInOut;
        return newui::InterpolationKind::Linear;
    }

    // Writes every currently-registered Animation whose KeyValues are
    // *all* serializable (computeAnimationTargetDescriptor() succeeds for
    // each) as a sibling "animations" collection alongside frame's own
    // properties/fields - see writeInstanceBody()'s own header comment for
    // why this and that are both hand-written rather than going through
    // Class::write(). An Animation with even one out-of-scope KeyValue is
    // left out entirely rather than partially written - a partially-
    // reconstructable Animation (missing keyframes for whatever property
    // couldn't round-trip) would silently misbehave on load in a way
    // nothing here could detect after the fact.
    void writeAnimations(newui::Frame& frame, newui::reflection::ObjectWriter& writer) {
        using namespace newui;
        using namespace newui::reflection;

        std::vector<Animation*> qualifying;
        for (const auto& anim : AnimationManager::animations()) {
            bool ok = !anim->keys().empty();
            for (const auto& key : anim->keys()) {
                for (const auto& kv : key->values()) {
                    if (computeAnimationTargetDescriptor(frame.rootView(), kv->property()).empty()) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) {
                    break;
                }
            }
            if (ok) {
                qualifying.push_back(anim.get());
            }
        }
        if (qualifying.empty()) {
            return;
        }

        writer.beginCollection("animations");
        std::size_t animIndex = 0;
        for (Animation* anim : qualifying) {
            std::any animIdx(animIndex++);
            writer.beginElement(animIdx, std::any());
            writer.beginObject(std::string(), nullptr);

            writer.writeString("name", anim->name());
            writer.writeInt64("startTime", anim->startTime(), false);
            writer.writeInt64("duration", anim->duration(), false);
            writer.writeBool("looping", anim->looping());

            writer.beginCollection("keys");
            std::size_t keyIndex = 0;
            for (const auto& key : anim->keys()) {
                std::any keyIdx(keyIndex++);
                writer.beginElement(keyIdx, std::any());
                writer.beginObject(std::string(), nullptr);

                writer.writeString("name", key->name());
                writer.writeInt64("keyFrame", key->keyFrame(), false);

                writer.beginCollection("values");
                std::size_t valueIndex = 0;
                for (const auto& kv : key->values()) {
                    std::any valueIdx(valueIndex++);
                    writer.beginElement(valueIdx, std::any());
                    writer.beginObject(std::string(), nullptr);

                    writer.writeString("target", computeAnimationTargetDescriptor(frame.rootView(), kv->property()));
                    writeBoxedAnimationValue(writer, "value", kv->boxedValue());
                    writer.writeString("interpolation", interpolationKindToString(kv->interpolationKind()));

                    writer.endObject(std::string(), nullptr);
                    writer.endElement(valueIdx, std::any());
                }
                writer.endCollection("values");

                writer.endObject(std::string(), nullptr);
                writer.endElement(keyIdx, std::any());
            }
            writer.endCollection("keys");

            writer.endObject(std::string(), nullptr);
            writer.endElement(animIdx, std::any());
        }
        writer.endCollection("animations");
    }

    // Read-side mirror of writeAnimations() above - replays the real
    // AnimationManager::addAnimation()/Animation::addKey() calls
    // hand-written code already makes (see examples/shapes2.cpp), rather
    // than a generic reflection-driven construction AnimationManager's own
    // private-constructor/singleton shape was never built to support (see
    // this file's own header comment on the "animations" block for why).
    // Must run after frame's own rootView tree already exists (every
    // target's view-path only resolves once it does) - see
    // Bundle::loadFrame()'s own call site.
    bool readAnimations(newui::Frame& frame, newui::reflection::ObjectReader& reader) {
        using namespace newui;
        using namespace newui::reflection;

        std::size_t animCount = reader.beginCollection("animations");
        for (std::size_t animIndex = 0; animIndex < animCount; ++animIndex) {
            std::any animIdx(animIndex);
            reader.beginElement(animIdx, std::any());
            reader.beginObject(std::string());

            std::string name;
            reader.readString("name", name);
            std::int32_t startTime32 = 0;
            reader.readInt("startTime", startTime32);
            std::int32_t duration32 = 0;
            reader.readInt("duration", duration32);
            bool looping = false;
            reader.readBool("looping", looping);

            Animation* anim = AnimationManager::addAnimation(
                name, static_cast<std::uint64_t>(startTime32), static_cast<std::uint64_t>(duration32));
            anim->setLooping(looping);

            std::size_t keyCount = reader.beginCollection("keys");
            for (std::size_t keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
                std::any keyIdx(keyIndex);
                reader.beginElement(keyIdx, std::any());
                reader.beginObject(std::string());

                std::string keyName;
                reader.readString("name", keyName);
                std::int32_t keyFrame32 = 0;
                reader.readInt("keyFrame", keyFrame32);

                Key* key = anim->addKey(keyName, static_cast<std::uint64_t>(keyFrame32));

                std::size_t valueCount = reader.beginCollection("values");
                for (std::size_t valueIndex = 0; valueIndex < valueCount; ++valueIndex) {
                    std::any valueIdx(valueIndex);
                    reader.beginElement(valueIdx, std::any());
                    reader.beginObject(std::string());

                    std::string target;
                    reader.readString("target", target);
                    std::string interpolationStr;
                    reader.readString("interpolation", interpolationStr);

                    ResolvedAnimationTarget resolved;
                    if (resolveAnimationTarget(frame.rootView(), target, resolved)) {
                        const Class* sourceClass = classinfo(resolved.sourceType);
                        const Property* reflProp = sourceClass != nullptr
                            ? sourceClass->property(resolved.propertyName) : nullptr;
                        if (reflProp != nullptr) {
                            std::any boxedValue = readBoxedAnimationValue(reader, "value", reflProp->type());
                            if (boxedValue.has_value()) {
                                std::unique_ptr<KeyValue> kv = AnimationTargetRegistry::buildKeyValue(
                                    resolved.sourceType, reflProp->type(), resolved.source,
                                    resolved.propertyName, boxedValue, interpolationKindFromString(interpolationStr));
                                if (kv != nullptr) {
                                    key->addRawValue(std::move(kv));
                                } else {
                                    std::cerr << "Bundle::loadFrame(): animation target '" << target << "' has no "
                                                  "matching AnimationTargetRegistry::registerTarget<SourceT,"
                                                  "ValueT>() call - skipped.\n";
                                }
                            }
                        } else {
                            std::cerr << "Bundle::loadFrame(): animation target '" << target << "' names an "
                                          "unregistered property - skipped.\n";
                        }
                    } else {
                        std::cerr << "Bundle::loadFrame(): animation target '" << target << "' didn't resolve "
                                      "- skipped.\n";
                    }

                    reader.endObject(std::string(), nullptr);
                    reader.endElement(valueIdx, std::any());
                }
                reader.endCollection("values");

                reader.endObject(std::string(), nullptr);
                reader.endElement(keyIdx, std::any());
            }
            reader.endCollection("keys");

            reader.endObject(std::string(), nullptr);
            reader.endElement(animIdx, std::any());
        }
        reader.endCollection("animations");
        return animCount > 0;
    }

}

namespace newui {

    Bundle::Bundle() {
        executableDir_ = directoryOf(getExecutablePath());
        resourcesDir_ = executableDir_ + "\\Resources";
    }

    Bundle& Bundle::instance() {
        static Bundle bundle;
        return bundle;
    }

    std::string Bundle::resourcePath(const std::string& relativePath) const {
        std::string path = resourcesDir_ + "\\" + relativePath;
        return fileExists(path) ? path : std::string();
    }

    bool Bundle::loadImage(const std::string& relativePath, BLImage& outImage) const {
        std::string path = resourcePath(relativePath);
        if (path.empty()) {
            return false;
        }
        return outImage.read_from_file(path.c_str()) == BL_SUCCESS;
    }

    std::string Bundle::loadTextFile(const std::string& relativePath) const {
        std::string path = resourcePath(relativePath);
        if (path.empty()) {
            return std::string();
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::string();
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    // Shared by loadFrame()/loadRootView()/loadView() below - loads
    // "<name>.newui" (resolved via loadTextFile(), so relativePath is
    // exactly a Bundle-relative name, no extension) and parses it into
    // outReader.doc. Returns false (outReader.doc left however
    // json5::from_string() leaves a failed parse) if the file doesn't
    // resolve or isn't valid JSON5.
    bool Bundle::parseNewuiFile(const std::string& name, reflection::ObjectReader& outReader) const {
        std::string text = loadTextFile(name + ".newui");
        if (text.empty()) {
            return false;
        }
        return !json5::from_string(text, outReader.doc);
    }

    bool Bundle::loadFrame(Frame& frame) const {
        if (frame.getName().empty()) {
            return false;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(frame.getName(), reader)) {
            return false;
        }

        // Frame::rootView() is a registered "rootView" property (reflection.h's
        // TypedProperty AssumeCopyable override, see its own comment) - read()'s
        // normal per-property walk already reaches the file's "rootView" node on
        // its own now. A second, manual readNested("rootView", ...) call used to
        // sit here (from when reflectgen unconditionally refused to register a
        // non-copy-constructible ValueT, RootView included) - removed, since
        // calling it on top of the now-real property read would read the same
        // "rootView" node twice into the same already-live tree, duplicating
        // every child view via its childViews collection's add-only
        // reconstruction (see ObjectReader::read()'s own comment on that).
        reader.read(&frame);
        readAnimations(frame, reader);
        return true;
    }

    bool Bundle::loadDialog(Dialog& dialog) const {
        return loadFrame(dialog.frame());
    }

    bool Bundle::loadRootView(RootView& rootView) const {
        Frame* frame = rootView.getFrame();
        if (frame == nullptr || frame->getName().empty()) {
            return false;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(frame->getName(), reader)) {
            return false;
        }

        reader.readNested("rootView", &rootView);
        return true;
    }

    bool Bundle::loadAnimations(Frame& frame) const {
        if (frame.getName().empty()) {
            return false;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(frame.getName(), reader)) {
            return false;
        }

        // Unlike loadFrame(), deliberately no reader.read(&frame) here -
        // see this method's own doc comment (bundle.h) for why: frame's
        // rootView tree is assumed already live (built by hand, or by an
        // earlier loadFrame()/loadRootView() call), and re-reading it here
        // too would duplicate every child. beginAtRoot() alone provides
        // read()'s other job (positioning the reader at the document root)
        // without the Class::read() call that would do the duplicating.
        reader.beginAtRoot();
        return readAnimations(frame, reader);
    }

    View* Bundle::loadView(const std::string& name) const {
        if (name.empty()) {
            return nullptr;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(name, reader)) {
            return nullptr;
        }

        return reader.readNew<View>();
    }

    bool Bundle::writeTextFile(const std::string& relativePath, const std::string& contents) const {
        ::CreateDirectoryA(resourcesDir_.c_str(), nullptr);  // idempotent - already-exists is fine, checked next via the actual write

        std::ofstream file(resourcesDir_ + "\\" + relativePath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << contents;
        return bool(file);
    }

    bool Bundle::writeFrame(Frame& frame) const {
        if (frame.getName().empty()) {
            return false;
        }

        const reflection::Class* clazz = reflection::classinfo(typeid(frame));
        if (clazz == nullptr) {
            return false;
        }

        reflection::ObjectWriter writer;

        //writer.write(&frame);
        
        writer.beginObject(std::string(), clazz);
        writeInstanceBody(clazz, &frame, writer);
        // See loadFrame()'s own comment - writeInstanceBody()'s per-property
        // walk above already writes "rootView" now that it's a real
        // registered property; a second, manual writeNested() call used to
        // be needed here and has been removed (it would otherwise emit
        // "rootView" twice into the same object).
        writeAnimations(frame, writer);

        writer.endObject(std::string(), clazz);
        
        return writeTextFile(frame.getName() + ".newui", json5::to_string(writer.doc));
    }

    bool Bundle::writeDialog(Dialog& dialog) const {
        return writeFrame(dialog.frame());
    }

    bool Bundle::writeView(View& view, const std::string& name) const {
        if (name.empty()) {
            return false;
        }

        const reflection::Class* clazz = reflection::classinfo(typeid(view));
        if (clazz == nullptr) {
            return false;
        }

        reflection::ObjectWriter writer;
        clazz->write(&view, &writer, std::string());

        return writeTextFile(name + ".newui", json5::to_string(writer.doc));
    }

    void Bundle::ensureInfoLoaded() const {
        if (infoLoaded_) {
            return;
        }
        infoLoaded_ = true;

        json5::document doc;
        if (json5::from_file(executableDir_ + "\\Info.json", doc)) {
            return;  // missing or unparseable - appName_/appVersion_ stay ""
        }

        appName_ = doc["name"].get_c_str("");
        appVersion_ = doc["version"].get_c_str("");
    }

    const std::string& Bundle::appName() const {
        ensureInfoLoaded();
        if (!appName_.empty()) {
            return appName_;
        }
        // Application::getName() returns by value, and appName_ must stay
        // untouched (it's the "did Info.json provide one" signal) - so the
        // live fallback goes into its own scratch member instead, purely
        // to give this by-reference return something to point at that
        // outlives the call.
        resolvedAppName_ = Application::instance().getName();
        return resolvedAppName_;
    }

    const std::string& Bundle::appVersion() const {
        ensureInfoLoaded();
        return appVersion_;
    }

}
