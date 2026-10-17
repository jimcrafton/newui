#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace manyrows {
    namespace io {

        // A small push parser for the XML inside .xlsx files: bytes go in piece by piece (as they come out of
        // the decompressor), events come out through callbacks. Nothing holds the whole document.
        //
        // It handles what spreadsheet XML uses: elements, attributes (either quote), text, CDATA, the five
        // standard entities and numeric character references. Comments, processing instructions and a DOCTYPE
        // are skipped. Names are reported WITHOUT their namespace prefix ("x:row" -> "row", "r:id" -> "id"),
        // because writers differ in the prefix they choose. It does not validate: malformed input produces
        // odd events rather than an error.
        // @reflect ignore=true
        class XmlParser {
        public:
            // @reflect ignore=true
            struct Attribute {
                std::string name;
                std::string value;
            };
            typedef std::vector<Attribute> Attributes;

            // Each returns false to stop parsing (feed() then returns false too).
            std::function<bool(const std::string& name, const Attributes& attributes)> onStart;
            std::function<bool(const std::string& name)> onEnd;   // also fired right after onStart for <a/>
            std::function<bool(const std::string& text)> onText;  // entity-decoded; may arrive in several pieces

            // Parses what can be parsed of everything fed so far; an unfinished tag or text waits for more.
            // Returns false once a callback stopped the parse (further calls do nothing).
            bool feed(const std::uint8_t* data, std::size_t size);

            // The attribute's value, or "" when absent.
            static const std::string& attribute(const Attributes& attributes, const char* name);

            // "&lt;" -> "<" etc. Unknown entities are kept as they are.
            static std::string decodeEntities(const std::string& text);

        private:
            bool parseTag(const std::string& tag);

            std::string buffer_;
            bool stopped_ = false;
        };

    }
}
