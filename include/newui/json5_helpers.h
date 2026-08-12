#pragma once

#include <newui/geometry.h>
#include <newui/font.h>

#include <blend2d/blend2d.h>

namespace json5 {
    class builder;
    class value;
}

namespace newui {

    // Low-level JSON5 <-> geometry/color/font helpers shared by every
    // writeFields()/readFields() override (View, ViewStyle, Layout,
    // Frame, ...) - kept separate from serialization.h so those overrides
    // (in view.cpp, viewstyle.cpp, layout.cpp, frame.cpp, ...) don't need
    // to depend on serialization.h's tree-walker/registry, which itself
    // depends on all of those headers - would otherwise be circular.

    // Writes rect as a nested {x,y,width,height} object under w[key].
    void writeRect(json5::builder& w, const char* key, const Rect& rect);

    // Reads a {x,y,width,height} object back into a Rect. Returns
    // defaultValue unchanged if v isn't an object (e.g. the key was absent).
    Rect readRect(const json5::value& v, const Rect& defaultValue = Rect());

    // Writes size as a nested {width,height} object under w[key] - same
    // shape as writeRect(), just the position-less half of it.
    void writeSize(json5::builder& w, const char* key, const Size& size);

    // Reads a {width,height} object back into a Size. Returns defaultValue
    // unchanged if v isn't an object (e.g. the key was absent).
    Size readSize(const json5::value& v, const Size& defaultValue = Size());

    // Writes color as w[key] = "#RRGGBBAA" (via Color::toString()) if it
    // holds a solid RGBA32/64 color. Omits the key entirely if color is
    // null, or holds a gradient/image pattern - solid-color-only scope,
    // see the serialization plan.
    void writeColor(json5::builder& w, const char* key, const BLVar& color);

    // Reads obj[key] (a "#RRGGBBAA"/named-color string) back into a solid
    // color, via Color::fromString(). Leaves outColor untouched if the key
    // is absent or isn't a valid color string.
    void readColor(const json5::value& obj, const char* key, BLVar& outColor);

    // Writes font as a nested {name,size,bold,italic,strikeThrough,
    // underlined} object under w[key].
    void writeFont(json5::builder& w, const char* key, const Font& font);

    // Reads a font object back. Returns defaultValue unchanged if v isn't
    // an object.
    Font readFont(const json5::value& v, const Font& defaultValue = Font());

}
