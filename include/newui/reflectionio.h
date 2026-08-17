
#include "newui/newui.h"
#include "newui/reflection.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>
#include <json5/json5_input.hpp>
#include <json5/json5_output.hpp>

#include <ctime>

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
            // properties, never nested inside a deeper object.
            if (depth_ == 0) {
                builder.push_object();
                builder["author"] = builder.new_string(metadata.author);
                builder["date"] = builder.new_string(currentDateString());
                builder["copyright"] = builder.new_string(metadata.copyright);
                builder["version"] = builder.new_string(newui::version());
                builder["meta"] = builder.pop();
            }

            builder["type"] = builder.new_string(clazz->name());

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
            clazz->write(inst, this, nullptr);

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

        int depth_ = 0;
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

            // Read directly off doc's own "meta" key, not through
            // beginObject()/readXXX() - "meta" is a sibling of the real
            // payload at the document root (see ObjectWriter::beginObject()'s
            // own comment), not a property any registered Class ever asks
            // for by name, so there's no property->read() call anywhere in
            // the walk above that would ever reach it.
            json5::value meta = doc["meta"];
            metadata.author = meta["author"].get_c_str("");
            metadata.date = meta["date"].get_c_str("");
            metadata.copyright = meta["copyright"].get_c_str("");
            metadata.version = meta["version"].get_c_str("");
        }

        json5::document doc;

        // Populated by read() from the file's own "meta" object - see its
        // own comment. Untouched (stays default-constructed/blank) until
        // read() actually runs.
        DocumentMetadata metadata;

    private:
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
    };


}

