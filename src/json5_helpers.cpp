#include "newui/json5_helpers.h"
#include "newui/color.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>

namespace newui {

    void writeRect(json5::builder& w, const char* key, const Rect& rect) {
        w.push_object();
        w["x"] = rect.left();
        w["y"] = rect.top();
        w["width"] = rect.size().width;
        w["height"] = rect.size().height;
        w[key] = w.pop();
    }

    Rect readRect(const json5::value& v, const Rect& defaultValue) {
        if (!v.is_object()) {
            return defaultValue;
        }

        return Rect(
            v["x"].get<float>(defaultValue.left()),
            v["y"].get<float>(defaultValue.top()),
            v["width"].get<float>(defaultValue.size().width),
            v["height"].get<float>(defaultValue.size().height));
    }

    void writeSize(json5::builder& w, const char* key, const Size& size) {
        w.push_object();
        w["width"] = size.width;
        w["height"] = size.height;
        w[key] = w.pop();
    }

    Size readSize(const json5::value& v, const Size& defaultValue) {
        if (!v.is_object()) {
            return defaultValue;
        }

        return Size(
            v["width"].get<float>(defaultValue.width),
            v["height"].get<float>(defaultValue.height));
    }

    void writeColor(json5::builder& w, const char* key, const BLVar& color) {
        if (color.is_null() || (!color.is_rgba32() && !color.is_rgba64())) {
            return;
        }

        BLRgba32 rgba;
        color.to_rgba32(&rgba);
        w[key] = w.new_string(Color(rgba.value, true).toString());
    }

    void readColor(const json5::value& obj, const char* key, BLVar& outColor) {
        json5::value v = obj[key];
        if (!v.is_string()) {
            return;
        }

        Color c;
        if (Color::fromString(v.get_c_str(), c)) {
            outColor = c.toBLRgba32();
        }
    }

    void writeFont(json5::builder& w, const char* key, const Font& font) {
        w.push_object();
        w["name"] = w.new_string(font.name());
        w["size"] = font.size();
        w["bold"] = font.bold();
        w["italic"] = font.italic();
        w["strikeThrough"] = font.strikeThrough();
        w["underlined"] = font.underlined();
        w[key] = w.pop();
    }

    Font readFont(const json5::value& v, const Font& defaultValue) {
        if (!v.is_object()) {
            return defaultValue;
        }

        Font f(v["name"].get_c_str(defaultValue.name().c_str()), v["size"].get<float>(defaultValue.size()));
        f.setBold(v["bold"].get_bool(defaultValue.bold()));
        f.setItalic(v["italic"].get_bool(defaultValue.italic()));
        f.setStrikeThrough(v["strikeThrough"].get_bool(defaultValue.strikeThrough()));
        f.setUnderlined(v["underlined"].get_bool(defaultValue.underlined()));
        return f;
    }

}
