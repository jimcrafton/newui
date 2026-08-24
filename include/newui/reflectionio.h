
#include "newui/newui.h"
#include "newui/reflection.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>
#include <json5/json5_input.hpp>
#include <json5/json5_output.hpp>

#include <ctime>
#include <iostream>
#include <set>
#include <unordered_map>
#include <utility>

namespace newui::reflection {

    // Top-level metadata every ObjectWriter::write() stamps into the
    // output as a "meta" object sibling to the real payload's own "type"/
    // properties (see ObjectWriter::beginObject()'s own comment for
    // exactly where) - not nested inside it, and not part of the reflected
    // object graph itself (InstanceT never sees this; it's document-level,
    // like a file header). author/copyright are whatever the caller sets
    // on ObjectWriter::metadata before calling write() (blank by default -
    // there's nothing meaningful to default either to). date/version are
    // NOT settable this way - ObjectWriter::write() always overwrites both
    // itself (today's date, and newui::version() - the running framework
    // build that's actually producing this file) regardless of whatever a
    // caller left in those two fields, since the whole point of "version"
    // in particular is recording the truth of what produced the file, not
    // something write-time caller code should be able to spoof.
    //
    // ObjectReader::read() populates this same struct (its own `metadata`
    // member) from whatever "meta" object the file actually had - a
    // missing/malformed "meta" object just leaves every field blank,
    // same "absent data reads back as default" contract every other
    // ObjectReader field already has.
    struct DocumentMetadata {
        std::string author;
        std::string date;
        std::string copyright;
        std::string version;
    };

    // "YYYY-MM-DD", the local date ObjectWriter::write() is running at -
    // localtime_s (not localtime()) since MSVC deprecates/warns on the
    // latter as not thread-safe (it writes through a single static
    // buffer); std::strftime still needs a real broken-down std::tm to
    // format from either way.
    inline std::string currentDateString() {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);

        char buf[11];  // "YYYY-MM-DD\0"
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
        return std::string(buf);
    }

    // Class::write()/Property::writeValue() hand every leaf/object/array a
    // propertyName (empty for exactly two cases: the true document root, and
    // a collection element - see Class::write()'s own comment). attach()
    // below is the one place that turns that into the right json5::builder
    // call: a non-empty name is a real key in whatever object scope is
    // currently open (builder[name] = v); an empty name inside a still-open
    // scope (depth_ > 0) is a collection element - append, don't key
    // (builder += v); an empty name with depth_ == 0 is the document root -
    // json5::builder::pop() already assigned it as such the moment its own
    // stack became empty, nothing left to do.
    class ObjectWriter : public ClassWriter {
    public:
        ObjectWriter() : builder(doc) {}

        // author/copyright are read as-is; date/version are overwritten
        // by write() itself - see DocumentMetadata's own comment for why.
        DocumentMetadata metadata;

        void beginObject(const std::string& name, const Class* clazz) override {
            builder.push_object();

            // depth_ == 0 is the true document root (see this class's own
            // header comment) - happens exactly once per write(), so
            // "meta" is written exactly once too, as a sibling key
            // alongside "type" and every one of InstanceT's own
            // properties, never nested inside a deeper object. writeObjects()
            // (below) stamps this itself instead, before its own named
            // objects start at depth_ 1, so a multi-object document still
            // only ever gets one "meta" too.
            if (depth_ == 0) {
                writeMeta();
            }

            // clazz is null for a synthetic grouping object that isn't a
            // reflected instance at all - e.g. the "delegates" wrapper
            // TypedClass<T>::write() opens (reflection.h) to hold each
            // Delegate's own array of connection descriptors. Such an
            // object gets no "type" tag (there's no Class to name it
            // with, and it isn't meant to look like a reconstructable
            // instance on read anyway).
            if (clazz) {
                builder["type"] = builder.new_string(clazz->name());
            }

            ++depth_;
        }
        void endObject(const std::string& name, const Class* clazz) override {
            --depth_;
            attach(name, builder.pop());
        }

        void writeInt8(const std::string& propertyName, std::uint8_t value, bool signedVal) override {
            attach(propertyName, json5::value(static_cast<double>(value)));
        }
        void writeInt16(const std::string& propertyName, std::uint16_t value, bool signedVal) override {
            attach(propertyName, json5::value(static_cast<double>(value)));
        }
        void writeInt32(const std::string& propertyName, std::uint32_t value, bool signedVal) override {
            attach(propertyName, json5::value(static_cast<double>(value)));
        }
        void writeInt64(const std::string& propertyName, std::uint64_t value, bool signedVal) override {
            attach(propertyName, json5::value(static_cast<double>(value)));
        }
        void writeString(const std::string& propertyName, const std::string& value) override {
            attach(propertyName, builder.new_string(value));
        }
        void writeFloat(const std::string& propertyName, float value) override {
            attach(propertyName, json5::value(value));
        }
        void writeDouble(const std::string& propertyName, double value) override {
            attach(propertyName, json5::value(value));
        }

        void writeBool(const std::string& propertyName, bool value) override {
            attach(propertyName, json5::value(value));
        }

        void beginElement(const std::any& index, const std::any& key) override {}
        void endElement(const std::any& index, const std::any& key) override {}

        // See ClassWriter::enterInstance()'s own comment - inProgress_ is
        // every (clazz, instancePtr) pair currently somewhere between its
        // own TypedClass<T>::write() beginObject()/endObject() bracket
        // (reflection.h) on *this* write() call chain. insert().second is
        // false exactly when that exact pair is already in the set - a
        // cyclic back-reference (or any other path reaching the same live
        // object, as the same type, twice on the same chain) - which is
        // when this refuses to recurse again. Keyed on the pair, not
        // instancePtr alone: a standard-layout type's address equals its
        // own first member's, so instancePtr alone would wrongly treat two
        // different, unrelated objects that happen to start at the same
        // address (e.g. ShapeStyle and its own first member gfx::Fill) as
        // the same one. nullptr is never meaningfully "in progress" (and
        // every real caller already checks for null before recursing into
        // a nested Class - see TypedProperty::write()'s own address()
        // checks) so it's passed through unconditionally rather than
        // tracked.
        bool enterInstance(const Class* clazz, void* instancePtr) override {
            return instancePtr == nullptr || inProgress_.insert({clazz, instancePtr}).second;
        }
        void exitInstance(const Class* clazz, void* instancePtr) override {
            if (instancePtr != nullptr) {
                inProgress_.erase({clazz, instancePtr});
            }
        }

        void beginCollection(const std::string& propertyName) override {
            builder.push_array();
            ++depth_;
        }
        void endCollection(const std::string& propertyName) override {
            --depth_;
            attach(propertyName, builder.pop());
        }

        template <typename InstanceT>
        void write(InstanceT* inst) {
            const Class* clazz = classinfo<InstanceT>();
            clazz->write(inst, this, std::string());

            //std::string text = json5::to_string(doc);
            //std::cout << "  --- ObjectWriter written JSON5 ---\n" << text << "  --- end JSON5 ---\n";
        }

        // Writes *inst as propertyName's own nested object, within
        // whatever scope this writer already has open - the write-side
        // mirror of ObjectReader::readNested() (reflectionio.h), for the
        // same reason that one exists: some nested link reflection
        // couldn't register as a real Property (e.g. Frame::rootView() -
        // see its own comment, frame.h) still needs to land at a
        // predictable key in the file. Only safe to call between a
        // caller's own beginObject()/endObject() pair (depth_ > 0) -
        // calling it as the very first thing on a fresh ObjectWriter
        // would wrongly stamp "meta" here instead of at the true root;
        // use write() for that case instead.
        template <typename T>
        void writeNested(const std::string& propertyName, T* inst) {
            const Class* clazz = classinfo<T>();
            if (clazz == nullptr) {
                return;
            }
            clazz->write(inst, this, propertyName);
        }

        // One already-live instance to write as a named sibling at a
        // multi-object document's own root - see writeObjects()'s own
        // comment. clazz must be instance's real, most-derived registered
        // Class (e.g. classinfo<FooBar>()) - same requirement write<T>()'s
        // classinfo<InstanceT>() call already satisfies implicitly for the
        // single-instance case.
        struct NamedObject {
            std::string name;
            const Class* clazz;
            void* instance;
        };

        // Writes a document whose root holds several independently-named
        // instances side by side - "foo": {...}, "bar": {...}, alongside
        // the usual "meta" - rather than write<T>()'s single anonymous
        // root instance. Needed for delegate connections that cross
        // between objects (a "<object>@<Class>.<method>" entry, see
        // Delegate::describedListeners()'s own comment) - resolving one
        // back on read requires every named instance in the file to
        // already exist, which only makes sense once a document can hold
        // more than one (see ObjectReader::readObjects(), the read-side
        // mirror of this). write<T>() itself is untouched - this is a
        // separate, additive entry point, not a replacement.
        void writeObjects(const std::vector<NamedObject>& objects) {
            builder.push_object();
            writeMeta();
            // Pretend a scope's already open at depth_ 1 (the "meta" one
            // above never nests, unlike a real object write) so each
            // object's own beginObject() call below - genuinely at depth_
            // 1, one level under the true root - doesn't re-trigger the
            // depth_==0 branch and stamp "meta" a second time per object.
            depth_ = 1;
            for (const NamedObject& obj : objects) {
                obj.clazz->write(obj.instance, this, obj.name);
            }
            depth_ = 0;
            // builder.pop() on this last, outermost scope assigns straight
            // into doc itself (json5::builder::pop()'s own _stack.empty()
            // branch) - same as the single-instance write<T>() path's own
            // outermost endObject()/pop() already does, just reached
            // directly here instead of through Class::write()'s own
            // beginObject()/endObject() bracketing (this root scope isn't
            // a reflected Class instance, so nothing calls those for it).
            builder.pop();

            std::string text = json5::to_string(doc);
            std::cout << "  --- ObjectWriter written JSON5 ---\n" << text << "  --- end JSON5 ---\n";
        }

        json5::document doc;
        json5::builder builder;

    private:
        void attach(const std::string& propertyName, json5::value v) {
            if (!propertyName.empty()) {
                builder[propertyName] = v;
            }
            else if (depth_ > 0) {
                builder += v;
            }
            // depth_ == 0 && propertyName.empty(): the true document root -
            // builder.pop() already assigned it, nothing further to do.
        }

        // Stamps "meta" as a sibling key in whatever object scope is
        // currently open - shared by beginObject()'s own depth_==0 branch
        // (the single-instance write<T>() path) and writeObjects() (which
        // opens its own root scope directly, without going through
        // beginObject() at all, since a multi-object root isn't itself a
        // reflected Class instance).
        void writeMeta() {
            builder.push_object();
            builder["author"] = builder.new_string(metadata.author);
            builder["date"] = builder.new_string(currentDateString());
            builder["copyright"] = builder.new_string(metadata.copyright);
            builder["version"] = builder.new_string(newui::version());
            builder["meta"] = builder.pop();
        }

        int depth_ = 0;
        std::set<std::pair<const Class*, void*>> inProgress_;
    };


    // The read-side mirror of ObjectWriter - navigates a parsed json5::value
    // tree (doc, populated by the caller via json5::from_string() before
    // calling read<InstanceT>()) using the exact same "current scope +
    // positional cursor" stack shape ObjectWriter's attach() logic implies on
    // the write side, just inverted:
    //
    //   - beginObject(name) with a non-empty name is a keyed nested-object
    //     lookup within the current scope (stack.back()[name]) - "style",
    //     "frame", "rootView", ....
    //   - beginObject("") is one of the two cases Class::write()'s own doc
    //     comment names for an empty name: either the very first call (the
    //     document root - stack is seeded with doc before any beginObject()
    //     happens, and the root case is simply "re-enter the current scope
    //     unchanged", detected by the current scope NOT being an array), or
    //     a collection element (the current scope IS the array
    //     beginCollection() pushed - consume its next not-yet-read element
    //     via the per-scope cursor, same as readXXX(propertyName="") below
    //     does for a *scalar* collection element).
    //   - readXXX(propertyName, val) with a non-empty name reads a keyed
    //     scalar within the current scope; with an empty name, consumes the
    //     current array scope's next element positionally, same cursor
    //     beginObject("") uses for an object-typed element.
    //
    // classinfo() is looked up from each object node's own "type" tag here
    // (the one place this file's read side actually consults it) - TypedClass
    // <T>::read() (reflection.h) currently discards beginObject()'s return
    // value, always trusting the *declared* C++ property type instead (see
    // its own comment on that gap), so this doesn't yet enable genuinely
    // polymorphic reconstruction - just resolves correctly for the exact
    // class write() already tagged the node with.
    class ObjectReader : public ClassReader {
    public:
        const Class* beginObject(const std::string& name) override {
            json5::value node;
            if (!name.empty()) {
                node = stack.back()[name];
            }
            else if (stack.back().is_array()) {
                std::size_t& i = cursorStack.back();
                node = stack.back()[i];
                ++i;
            }
            else {
                node = stack.back();
            }
            stack.push_back(node);
            cursorStack.push_back(0);
            return node.is_object() ? classinfo(std::string(node["type"].get_c_str(""))) : nullptr;
        }

        // See ClassReader::peekElementType()'s own doc comment - looks at
        // the current array position's own "type" tag without touching
        // cursorStack (a plain local i, not the `std::size_t&` beginObject()
        // itself uses to advance), so it's safe to call before the real
        // beginObject("") that actually consumes this same position.
        const Class* peekElementType() const override {
            if (!stack.back().is_array()) {
                return nullptr;
            }
            std::size_t i = cursorStack.back();
            json5::value node = stack.back()[i];
            return node.is_object() ? classinfo(std::string(node["type"].get_c_str(""))) : nullptr;
        }

        // See ClassReader::hasValue()'s own doc comment for why this
        // exists and who consults it. Only meaningful for a *keyed*
        // scalar/object within the current object scope - not called for
        // a collection element (those are always genuinely present, by
        // construction, or beginCollection()'s own count wouldn't include
        // them), so this doesn't need name.empty()'s positional-cursor
        // handling the way beginObject()/valueFor() do.
        bool hasValue(const std::string& propertyName) const override {
            // NOT `!stack.back()[propertyName].is_null()` - json5::value's
            // default constructor (json5.hpp) leaves its NaN-boxed
            // union genuinely *uninitialized* (no in-class member
            // initializer backs it), and object_view::operator[]'s own
            // "key not found" fallback is exactly that default-
            // constructed value() - so is_null() on a missing key is
            // reading uninitialized memory, not a reliable null check
            // (confirmed live: consistently, wrongly, true). find() !=
            // end() is the real "is this key actually present" answer
            // object_view exposes.
            json5::object_view obj(stack.back());
            return obj.is_valid() && obj.find(propertyName) != obj.end();
        }

        void endObject(const std::string&, const Class*) override {
            stack.pop_back();
            cursorStack.pop_back();
        }

        void readInt(const std::string& propertyName, std::int32_t& val) override {
            val = valueFor(propertyName).get<std::int32_t>(0);
        }
        void readString(const std::string& propertyName, std::string& val) override {
            val = valueFor(propertyName).get_c_str("");
        }
        void readFloat(const std::string& propertyName, float& val) override {
            val = valueFor(propertyName).get<float>(0.0f);
        }
        void readDouble(const std::string& propertyName, double& val) override {
            val = valueFor(propertyName).get<double>(0.0);
        }
        void readBool(const std::string& propertyName, bool& val) override {
            val = valueFor(propertyName).get_bool(false);
        }

        std::size_t beginCollection(const std::string& propertyName) override {
            json5::value node = stack.back()[propertyName];
            stack.push_back(node);
            cursorStack.push_back(0);
            return node.is_array() ? json5::array_view(node).size() : 0;
        }
        void beginElement(const std::any&, const std::any&) override {}
        void endElement(const std::any&, const std::any&) override {}
        void endCollection(const std::string&) override {
            stack.pop_back();
            cursorStack.pop_back();
        }

        // Read-side mirror of ObjectWriter::enterInstance()/exitInstance()
        // - see ClassReader::enterInstance()'s own comment. Same
        // (clazz, instancePtr)-keyed inProgress_ set idiom, same nullptr
        // pass-through.
        bool enterInstance(const Class* clazz, void* instancePtr) override {
            return instancePtr == nullptr || inProgress_.insert({clazz, instancePtr}).second;
        }
        void exitInstance(const Class* clazz, void* instancePtr) override {
            if (instancePtr != nullptr) {
                inProgress_.erase({clazz, instancePtr});
            }
        }

        // Reads doc (already parsed by the caller, e.g. via
        // json5::from_string(text, objReader.doc)) into *inst - an existing
        // live object, never a fresh one (mirrors ObjectWriter::write()'s own
        // InstanceT* + classinfo<InstanceT>() shape). Note this means a
        // "childViews"-shaped propertyCollection (add-only reconstruction,
        // see PropertyCollection::add()'s own comment) will call the real
        // addChild() for every element read, on top of whatever children
        // *inst already has - reading back into the same live tree that was
        // just written duplicates children rather than replacing them; a
        // real load path would read into a freshly createInstance()'d
        // (empty) tree instead.
        template <typename InstanceT>
        void read(InstanceT* inst) {
            const Class* clazz = classinfo<InstanceT>();
            if (clazz == nullptr) {
                return;
            }

            stack.clear();
            cursorStack.clear();
            stack.push_back(doc);
            cursorStack.push_back(0);

            std::any instVal(inst);
            bool onHeap = false;
            clazz->read(this, "", instVal, onHeap);

            // "meta" is a sibling of the real payload at the document root
            // (see ObjectWriter::beginObject()'s own comment), not a
            // property any registered Class ever asks for by name, so
            // there's no property->read() call anywhere in the walk above
            // that would ever reach it - readMetaFromDoc() reads it
            // directly off doc instead.
            readMetaFromDoc();
        }

        // Reads doc into a freshly Class::createInstance()'d instance of
        // whatever class its own root "type" tag names - unlike read()
        // above (which always reads into an already-live *inst), this
        // never touches an existing object. BaseT anchors the search: the
        // resolved class only wins if BaseT is somewhere in its own
        // parentClass() chain (the same polymorphic-dispatch check
        // TypedClass<T>::read() already does for a nested property, e.g. a
        // Shape-typed collection element tagged "type": "Circle" - see its
        // own comment) - pass whatever real base every instance in this
        // file could be (View, to accept a file written as a SubView, a
        // Button, ...). Returns nullptr if "type" is missing/unregistered,
        // doesn't derive from BaseT, or has no registered constructor
        // Class::createInstance() can actually call.
        template <typename BaseT>
        BaseT* readNew() {
            const Class* clazz = classinfo<BaseT>();
            if (clazz == nullptr) {
                return nullptr;
            }

            stack.clear();
            cursorStack.clear();
            stack.push_back(doc);
            cursorStack.push_back(0);

            std::any instVal;  // empty - clazz->read() below fresh-constructs into it
            bool onHeap = true;  // this call path always constructs fresh - see comment above
            void* raw = nullptr;
            clazz->read(this, "", instVal, onHeap, &raw);

            readMetaFromDoc();

            return static_cast<BaseT*>(raw);
        }

        // Reads the propertyName-keyed nested object at the document's own
        // root - e.g. "rootView" inside a file written with a Frame as the
        // true root (Frame::rootView() registered as its own "rootView"
        // property) - directly into *inst. Same existing-live-object
        // contract as read() above (see its own comment on "childViews"
        // add-only reconstruction), just starting one level below the
        // document root instead of at it.
        template <typename InstanceT>
        void readNested(const std::string& propertyName, InstanceT* inst) {
            const Class* clazz = classinfo<InstanceT>();
            if (clazz == nullptr) {
                return;
            }

            stack.clear();
            cursorStack.clear();
            stack.push_back(doc);
            cursorStack.push_back(0);

            std::any instVal(inst);
            bool onHeap = false;
            clazz->read(this, propertyName, instVal, onHeap);

            readMetaFromDoc();
        }

        // Positions this reader at the document root, ready for a caller
        // with no InstanceT/Class of its own to route through (e.g.
        // Bundle::loadAnimations(), bundle.cpp, reading a hand-written
        // "animations" block directly via beginCollection()/beginObject()/
        // readXXX() rather than through Class::read()) to navigate from
        // there. Same stack-seeding preamble read()/readNew()/
        // readNested() above already each do themselves before their own
        // Class::read() call - factored out here since this caller has no
        // such call to attach it to. Without this, e.g. beginCollection()'s
        // own stack.back() runs on a never-seeded (so still empty) stack -
        // real, reproduced crash ("back() called on an empty vector") this
        // exists to fix, not a hypothetical.
        void beginAtRoot() {
            stack.clear();
            cursorStack.clear();
            stack.push_back(doc);
            cursorStack.push_back(0);
        }

        // One instance read back from a multi-object document - see
        // readObjects()'s own comment. `instance` owns the object
        // (createInstance()'d fresh by Class::read(), same "caller now
        // owns this" convention Class::createInstance() itself already
        // implies); `rawInstance` is the same pointer type-erased to
        // void* (Class::read()'s new outRawInstance out-param, reflection.h)
        // - readObjects()'s own pass 2 needs a raw pointer to hand to
        // Delegate::connectListener()/Method::invoke(), neither of which
        // can be reached generically through `instance`'s std::any without
        // already knowing its concrete type.
        struct NamedObject {
            std::string name;
            const Class* clazz = nullptr;
            std::any instance;
            void* rawInstance = nullptr;
        };

        // The read-side mirror of ObjectWriter::writeObjects() - reads a
        // document whose root holds several independently-named instances
        // (see that method's own comment for why: reconnecting a
        // "<object>@<Class>.<method>" delegate connection needs every
        // named instance in the file to already exist). Two passes:
        //
        //   1. Construct and read every top-level named object (skipping
        //      "meta") via the exact same Class::read() every other read
        //      path already goes through - no separate construction logic
        //      here, just a new outer loop over the document's own keys.
        //      A "delegates" block is read as an ordinary property would
        //      be at this point (TypedClass<T>::read() doesn't touch it -
        //      see reflection.h; delegate resolution is entirely pass 2's
        //      own concern, not Class::read()'s), so pass 1 alone leaves
        //      every connection unresolved.
        //   2. Re-visit each object's own "delegates" block directly
        //      (bypassing Class::read() this time - it was never taught
        //      about delegates at all, deliberately, since resolving one
        //      needs the *whole* name->instance map pass 1 just built,
        //      not just this one object) and reconnect each descriptor
        //      found there via reflection, now that every referenced
        //      object actually exists.
        //
        // Returns every object created - the caller owns them (same
        // convention as Class::createInstance()) and is responsible for
        // eventually destroying each one via its own known concrete type.
        std::vector<NamedObject> readObjects() {
            std::vector<NamedObject> objects;
            std::unordered_map<std::string, std::size_t> indexByName;

            stack.clear();
            cursorStack.clear();
            stack.push_back(doc);
            cursorStack.push_back(0);

            for (auto [key, node] : json5::object_view(doc)) {
                std::string name(key);
                if (name == "meta" || !node.is_object()) {
                    continue;
                }

                const Class* clazz = classinfo(std::string(node["type"].get_c_str("")));
                if (clazz == nullptr) {
                    std::cerr << "ObjectReader::readObjects(): '" << name
                              << "' has an unknown or missing \"type\" - skipping\n";
                    continue;
                }

                std::any instVal;
                bool onHeap = false;
                void* raw = nullptr;
                clazz->read(this, name, instVal, onHeap, &raw);

                indexByName[name] = objects.size();
                objects.push_back(NamedObject{ name, clazz, std::move(instVal), raw });
            }

            for (const NamedObject& obj : objects) {
                if (obj.rawInstance == nullptr) {
                    continue;
                }

                beginObject(obj.name);
                beginObject("delegates");

                std::vector<const Delegate*> delegatesOrdered;
                obj.clazz->allDelegates(delegatesOrdered);

                for (const Delegate* d : delegatesOrdered) {
                    std::size_t n = beginCollection(d->name());
                    for (std::size_t i = 0; i < n; ++i) {
                        std::string descriptor;
                        readString("", descriptor);
                        resolveDelegateConnection(obj, d, descriptor, indexByName, objects);
                    }
                    endCollection(d->name());
                }

                endObject("delegates", nullptr);
                endObject(obj.name, obj.clazz);
            }

            json5::value meta = doc["meta"];
            metadata.author = meta["author"].get_c_str("");
            metadata.date = meta["date"].get_c_str("");
            metadata.copyright = meta["copyright"].get_c_str("");
            metadata.version = meta["version"].get_c_str("");

            return objects;
        }

        json5::document doc;

        // Populated by read() from the file's own "meta" object - see its
        // own comment. Untouched (stays default-constructed/blank) until
        // read() actually runs.
        DocumentMetadata metadata;

    private:
        // One entry of a "delegates" array read back for `sender` (an
        // object readObjects() itself already constructed) - `descriptor`
        // is exactly one string readObjects() just read out of that
        // array, in the two forms Delegate::describedListeners()'s own
        // comment (reflection.h) describes:
        //   - no '@' at all: a free/static function name - reconnecting
        //     this isn't supported yet (see this project's own README/
        //     HANDOFF notes on why - free-function name resolution needs
        //     a name->address registry nothing populates today); logged
        //     and skipped, not treated as an error.
        //   - "<object>@<Class>.<method>": looked up in `indexByName`/
        //     `objects` (built by readObjects()'s own pass 1, so every
        //     name is already resolvable by the time pass 2 calls this),
        //     then `<method>` is searched for on the target's own Class
        //     - walking its base chain manually (Class::method() itself
        //     only checks direct members) - and, if found, handed to
        //     Delegate::connectListener() (reflection.h) to do the actual
        //     type-checked reconnect. `<Class>` itself is never enforced
        //     strictly (a mismatch just gets a warning) - the connection
        //     always uses the target's own *real* class, since requiring
        //     an exact match would make this needlessly brittle against
        //     e.g. a subclass that wasn't the exact type originally
        //     written.
        void resolveDelegateConnection(const NamedObject& sender, const Delegate* delegate,
                                         const std::string& descriptor,
                                         const std::unordered_map<std::string, std::size_t>& indexByName,
                                         std::vector<NamedObject>& objects) {
            std::size_t at = descriptor.find('@');
            if (at == std::string::npos) {
                std::cerr << "ObjectReader::readObjects(): skipping delegate connection '" << descriptor
                          << "' on " << sender.name << "." << delegate->name()
                          << " - free-function reconnection isn't supported yet\n";
                return;
            }

            std::string targetName = descriptor.substr(0, at);
            std::string rest = descriptor.substr(at + 1);
            std::size_t dot = rest.find('.');
            if (dot == std::string::npos) {
                std::cerr << "ObjectReader::readObjects(): malformed delegate connection '" << descriptor
                          << "' on " << sender.name << "." << delegate->name()
                          << " - expected '<object>@<Class>.<method>'\n";
                return;
            }

            std::string className = rest.substr(0, dot);
            std::string methodName = rest.substr(dot + 1);

            auto it = indexByName.find(targetName);
            if (it == indexByName.end()) {
                std::cerr << "ObjectReader::readObjects(): delegate connection '" << descriptor << "' on "
                          << sender.name << "." << delegate->name() << " refers to unknown object '"
                          << targetName << "'\n";
                return;
            }

            NamedObject& target = objects[it->second];
            if (target.clazz->name() != className) {
                std::cerr << "ObjectReader::readObjects(): warning - '" << targetName << "' is a '"
                          << target.clazz->name() << "', not '" << className << "' as '" << descriptor
                          << "' claims - connecting anyway\n";
            }

            const Method* method = nullptr;
            for (const Class* c = target.clazz; c != nullptr && method == nullptr; c = c->parentClass()) {
                method = c->method(methodName);
            }
            if (method == nullptr) {
                std::cerr << "ObjectReader::readObjects(): delegate connection '" << descriptor << "' on "
                          << sender.name << "." << delegate->name() << " - no method '" << methodName
                          << "' on '" << target.clazz->name() << "'\n";
                return;
            }

            if (!delegate->connectListener(sender.rawInstance, descriptor, target.rawInstance, method)) {
                std::cerr << "ObjectReader::readObjects(): delegate connection '" << descriptor << "' on "
                          << sender.name << "." << delegate->name()
                          << " - signature mismatch, not connected\n";
            }
        }

        // Shared by readNew()/readNested() below - see read()'s own comment
        // for why this reads doc["meta"] directly rather than through
        // beginObject()/readXXX().
        void readMetaFromDoc() {
            json5::value meta = doc["meta"];
            metadata.author = meta["author"].get_c_str("");
            metadata.date = meta["date"].get_c_str("");
            metadata.copyright = meta["copyright"].get_c_str("");
            metadata.version = meta["version"].get_c_str("");
        }

        // propertyName non-empty: a keyed scalar within the current object
        // scope. Empty: the current array scope's next not-yet-read element,
        // consumed positionally - same cursor beginObject("") uses for an
        // object-typed element, just for a plain scalar one instead.
        json5::value valueFor(const std::string& propertyName) {
            if (!propertyName.empty()) {
                return stack.back()[propertyName];
            }
            std::size_t& i = cursorStack.back();
            json5::value v = stack.back()[i];
            ++i;
            return v;
        }

        std::vector<json5::value> stack;
        std::vector<std::size_t> cursorStack;
        std::set<std::pair<const Class*, void*>> inProgress_;
    };


}

