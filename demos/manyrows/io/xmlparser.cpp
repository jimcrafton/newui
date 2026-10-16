#include "io/xmlparser.h"

#include <cstdlib>

namespace manyrows {
    namespace io {

        namespace {
            void appendUtf8(std::string& out, unsigned long cp) {
                if (cp < 0x80) {
                    out += char(cp);
                } else if (cp < 0x800) {
                    out += char(0xC0 | (cp >> 6));
                    out += char(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += char(0xE0 | (cp >> 12));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                } else if (cp < 0x110000) {
                    out += char(0xF0 | (cp >> 18));
                    out += char(0x80 | ((cp >> 12) & 0x3F));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                }
            }

            bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

            // "x:row" -> "row".
            std::string localName(const std::string& name) {
                std::size_t colon = name.rfind(':');
                return colon == std::string::npos ? name : name.substr(colon + 1);
            }
        }

        const std::string& XmlParser::attribute(const Attributes& attributes, const char* name) {
            static const std::string none;
            for (const Attribute& attribute : attributes) {
                if (attribute.name == name) {
                    return attribute.value;
                }
            }
            return none;
        }

        std::string XmlParser::decodeEntities(const std::string& text) {
            if (text.find('&') == std::string::npos) {
                return text;
            }
            std::string out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '&') {
                    out += text[i];
                    continue;
                }
                std::size_t semicolon = text.find(';', i + 1);
                if (semicolon == std::string::npos || semicolon - i > 10) {
                    out += text[i];
                    continue;
                }
                std::string name = text.substr(i + 1, semicolon - i - 1);
                if (name == "lt") out += '<';
                else if (name == "gt") out += '>';
                else if (name == "amp") out += '&';
                else if (name == "quot") out += '"';
                else if (name == "apos") out += '\'';
                else if (name.size() > 1 && name[0] == '#') {
                    bool hex = name[1] == 'x' || name[1] == 'X';
                    char* end = nullptr;
                    unsigned long cp = std::strtoul(name.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
                    if (end == name.c_str() + name.size() && cp != 0) {
                        appendUtf8(out, cp);
                    } else {
                        out += text.substr(i, semicolon - i + 1);
                    }
                } else {
                    out += text.substr(i, semicolon - i + 1);
                }
                i = semicolon;
            }
            return out;
        }

        bool XmlParser::feed(const std::uint8_t* data, std::size_t size) {
            if (stopped_) {
                return false;
            }
            buffer_.append(reinterpret_cast<const char*>(data), size);
            std::size_t pos = 0;
            while (pos < buffer_.size()) {
                if (buffer_[pos] != '<') {
                    std::size_t next = buffer_.find('<', pos);
                    if (next == std::string::npos) {
                        break;   // the text may continue in the next piece
                    }
                    if (onText && !onText(decodeEntities(buffer_.substr(pos, next - pos)))) {
                        stopped_ = true;
                        return false;
                    }
                    pos = next;
                    continue;
                }
                // a markup construct: find where it ends
                std::size_t end;
                bool special = false;
                if (buffer_.compare(pos, 4, "<!--") == 0) {
                    end = buffer_.find("-->", pos + 4);
                    end = end == std::string::npos ? end : end + 3;
                    special = true;
                } else if (buffer_.compare(pos, 9, "<![CDATA[") == 0) {
                    std::size_t close = buffer_.find("]]>", pos + 9);
                    if (close == std::string::npos) {
                        break;
                    }
                    if (onText && !onText(buffer_.substr(pos + 9, close - pos - 9))) {
                        stopped_ = true;
                        return false;
                    }
                    pos = close + 3;
                    continue;
                } else if (buffer_.compare(pos, 2, "<?") == 0) {
                    end = buffer_.find("?>", pos + 2);
                    end = end == std::string::npos ? end : end + 2;
                    special = true;
                } else if (buffer_.compare(pos, 2, "<!") == 0) {
                    end = buffer_.find('>', pos + 2);   // DOCTYPE (an internal subset in [] is not supported)
                    end = end == std::string::npos ? end : end + 1;
                    special = true;
                } else {
                    // an ordinary tag: '>' not inside a quoted attribute value
                    char quote = 0;
                    end = std::string::npos;
                    for (std::size_t i = pos + 1; i < buffer_.size(); ++i) {
                        char c = buffer_[i];
                        if (quote != 0) {
                            if (c == quote) quote = 0;
                        } else if (c == '"' || c == '\'') {
                            quote = c;
                        } else if (c == '>') {
                            end = i + 1;
                            break;
                        }
                    }
                }
                if (end == std::string::npos) {
                    break;   // incomplete: wait for more
                }
                if (!special && !parseTag(buffer_.substr(pos + 1, end - pos - 2))) {
                    stopped_ = true;
                    return false;
                }
                pos = end;
            }
            buffer_.erase(0, pos);
            return true;
        }

        // `tag` is what lies between '<' and '>': "/row", "c r=\"A1\" t=\"s\"", "dimension ref=\"A1\"/".
        bool XmlParser::parseTag(const std::string& tag) {
            if (tag.empty()) {
                return true;
            }
            if (tag[0] == '/') {
                std::size_t i = 1;
                while (i < tag.size() && !isSpace(tag[i])) {
                    ++i;
                }
                return !onEnd || onEnd(localName(tag.substr(1, i - 1)));
            }
            bool selfClosing = tag.back() == '/';
            std::size_t limit = selfClosing ? tag.size() - 1 : tag.size();
            std::size_t i = 0;
            while (i < limit && !isSpace(tag[i])) {
                ++i;
            }
            std::string name = localName(tag.substr(0, i));
            Attributes attributes;
            while (i < limit) {
                while (i < limit && isSpace(tag[i])) {
                    ++i;
                }
                std::size_t nameStart = i;
                while (i < limit && tag[i] != '=' && !isSpace(tag[i])) {
                    ++i;
                }
                if (i == nameStart) {
                    break;
                }
                Attribute attribute;
                attribute.name = localName(tag.substr(nameStart, i - nameStart));
                while (i < limit && isSpace(tag[i])) {
                    ++i;
                }
                if (i < limit && tag[i] == '=') {
                    ++i;
                    while (i < limit && isSpace(tag[i])) {
                        ++i;
                    }
                    if (i < limit && (tag[i] == '"' || tag[i] == '\'')) {
                        char quote = tag[i++];
                        std::size_t valueStart = i;
                        while (i < limit && tag[i] != quote) {
                            ++i;
                        }
                        attribute.value = decodeEntities(tag.substr(valueStart, i - valueStart));
                        if (i < limit) {
                            ++i;
                        }
                    }
                }
                attributes.push_back(std::move(attribute));
            }
            if (onStart && !onStart(name, attributes)) {
                return false;
            }
            return !selfClosing || !onEnd || onEnd(name);
        }

    }
}
