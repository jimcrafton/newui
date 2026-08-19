// Covers the enum-typed Property support ObjectWriter/ObjectReader gained
// via Property::writeValue()/readValue()'s new Enum branch (src/
// reflection.cpp) - a plain enum property round-trips as a single JSON5
// string; a "@reflect flags"-opted-in one (EnumBuilder<T>::flags(), see
// its own comment on why that's always explicit, never guessed from the
// enum's own shape) round-trips as an array of decomposed flag names,
// preferring a declared combo name over separately naming its individual
// bits. Neither case existed at all before this pass - an enum-typed
// property matched nothing in either dispatch function and was silently
// skipped.

#include "newui/newui.h"
#include "newui/reflection.h"
#include "newui/reflectionio.h"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace {

enum class Orientation {
    Horizontal,
    Vertical,
};

// Deliberately not power-of-two-only: BoldItalic is a declared combo of
// Bold|Italic, exercising decompose()'s own "prefer the combo name over
// separately naming its bits" tie-break.
enum class ButtonFlags : std::uint32_t {
    None = 0,
    Bold = 1u << 0,
    Italic = 1u << 1,
    Underline = 1u << 2,
    BoldItalic = Bold | Italic,
};

class Widget {
public:
    Orientation orientation() const { return orientation_; }
    void setOrientation(Orientation v) { orientation_ = v; }

    ButtonFlags flags() const { return flags_; }
    void setFlags(ButtonFlags v) { flags_ = v; }

private:
    Orientation orientation_ = Orientation::Horizontal;
    ButtonFlags flags_ = ButtonFlags::None;
};

void RegisterEnums() {
    static bool registered = [] {
        ReflectionRegistry::registerEnum(EnumBuilder<Orientation>("Orientation")
            .addValue("Horizontal", 0)
            .addValue("Vertical", 1)
            .build());

        ReflectionRegistry::registerEnum(EnumBuilder<ButtonFlags>("ButtonFlags")
            .addValue("None", 0)
            .addValue("Bold", 1)
            .addValue("Italic", 2)
            .addValue("Underline", 4)
            .addValue("BoldItalic", 3)
            .flags(true)
            .build());
        return true;
    }();
    (void)registered;
}

const Class* RegisterAndGetWidgetClass() {
    RegisterEnums();
    static const Class* registered = [] {
        ClassBuilder<Widget> builder;
        builder.clazz()
            .property("orientation", Scope::Public, &Widget::orientation, &Widget::setOrientation)
            .property("flags", Scope::Public, &Widget::flags, &Widget::setFlags)
            .constructor<>();
        ReflectionRegistry::registerClass(builder);
        return classinfo(typeid(Widget));
    }();
    return registered;
}

}  // namespace

// ---------------------------------------------------------------------
// Plain enum - a single JSON5 string, both directions.
// ---------------------------------------------------------------------

TEST(EnumSerialization, PlainEnumWritesAsASingleString) {
    RegisterAndGetWidgetClass();
    Widget w;
    w.setOrientation(Orientation::Vertical);

    ObjectWriter writer;
    writer.write(&w);

    json5::value node = writer.doc["orientation"];
    EXPECT_TRUE(node.is_string());
    EXPECT_EQ(std::string(node.get_c_str("")), "Vertical");
}

TEST(EnumSerialization, PlainEnumRoundTrips) {
    RegisterAndGetWidgetClass();
    Widget w;
    w.setOrientation(Orientation::Vertical);

    ObjectWriter writer;
    writer.write(&w);
    std::string text = json5::to_string(writer.doc);

    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    Widget fresh;
    reader.read(&fresh);
    EXPECT_EQ(fresh.orientation(), Orientation::Vertical);
}

TEST(EnumSerialization, PlainEnumWithNoDeclaredMatchFallsBackToHexAndRoundTrips) {
    RegisterAndGetWidgetClass();
    Widget w;
    // Never a real declared value - simulates data written by a future
    // version of this enum with a value this build doesn't know the name
    // of, or a value assigned some other way than through the two
    // declared enumerators.
    w.setOrientation(static_cast<Orientation>(99));

    ObjectWriter writer;
    writer.write(&w);

    json5::value node = writer.doc["orientation"];
    EXPECT_EQ(std::string(node.get_c_str("")), "0x63");

    std::string text = json5::to_string(writer.doc);
    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    Widget fresh;
    reader.read(&fresh);
    EXPECT_EQ(static_cast<int>(fresh.orientation()), 99);
}

// ---------------------------------------------------------------------
// Flags enum - an array of decomposed flag names.
// ---------------------------------------------------------------------

TEST(EnumSerialization, FlagsEnumPrefersADeclaredComboNameOverItsBits) {
    RegisterAndGetWidgetClass();
    Widget w;
    w.setFlags(ButtonFlags::BoldItalic);  // == Bold | Italic

    ObjectWriter writer;
    writer.write(&w);

    json5::value node = writer.doc["flags"];
    ASSERT_TRUE(node.is_array());
    json5::array_view names(node);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(std::string(names.begin()[0].get_c_str("")), "BoldItalic");
}

TEST(EnumSerialization, FlagsEnumDecomposesAnUndeclaredCombination) {
    RegisterAndGetWidgetClass();
    Widget w;
    w.setFlags(static_cast<ButtonFlags>(
        static_cast<std::uint32_t>(ButtonFlags::Bold) | static_cast<std::uint32_t>(ButtonFlags::Underline)));

    ObjectWriter writer;
    writer.write(&w);

    json5::array_view names(writer.doc["flags"]);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(std::string(names.begin()[0].get_c_str("")), "Bold");
    EXPECT_EQ(std::string(names.begin()[1].get_c_str("")), "Underline");
}

TEST(EnumSerialization, FlagsEnumRoundTripsAnUndeclaredCombination) {
    RegisterAndGetWidgetClass();
    Widget w;
    w.setFlags(static_cast<ButtonFlags>(
        static_cast<std::uint32_t>(ButtonFlags::Bold) | static_cast<std::uint32_t>(ButtonFlags::Underline)));

    ObjectWriter writer;
    writer.write(&w);
    std::string text = json5::to_string(writer.doc);

    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    Widget fresh;
    reader.read(&fresh);
    EXPECT_EQ(fresh.flags(), static_cast<ButtonFlags>(0b101u));
}

TEST(EnumSerialization, FlagsEnumWithUndeclaredBitsFallsBackToHexTokenAndRoundTrips) {
    RegisterAndGetWidgetClass();
    Widget w;
    // Bold (declared) combined with an entirely undeclared bit (1<<3) -
    // decompose() should still name what it can and fall back to a hex
    // token for the rest, rather than dropping it.
    w.setFlags(static_cast<ButtonFlags>(static_cast<std::uint32_t>(ButtonFlags::Bold) | (1u << 3)));

    ObjectWriter writer;
    writer.write(&w);

    json5::array_view names(writer.doc["flags"]);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(std::string(names.begin()[0].get_c_str("")), "Bold");
    EXPECT_EQ(std::string(names.begin()[1].get_c_str("")), "0x8");

    std::string text = json5::to_string(writer.doc);
    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse written JSON5";

    Widget fresh;
    reader.read(&fresh);
    EXPECT_EQ(static_cast<std::uint32_t>(fresh.flags()), 0b1001u);
}

TEST(EnumSerialization, FlagsEnumWithNoFlagsSetWritesAnEmptyArray) {
    RegisterAndGetWidgetClass();
    Widget w;  // flags_ defaults to ButtonFlags::None

    ObjectWriter writer;
    writer.write(&w);

    json5::value node = writer.doc["flags"];
    ASSERT_TRUE(node.is_array());
    EXPECT_EQ(json5::array_view(node).size(), 0u);
}

TEST(EnumSerialization, FlagsEnumSkipsAnUnrecognizedTokenOnRead) {
    RegisterAndGetWidgetClass();

    // Hand-written JSON5 - "Bold" is real, "NotAFlag" isn't declared at
    // all and isn't a valid hex token either. Should read back with just
    // Bold set, not throw or crash.
    std::string text =
        "{ meta: {}, type: \"Widget\", orientation: \"Horizontal\", "
        "flags: [\"Bold\", \"NotAFlag\"] }";

    ObjectReader reader;
    json5::error err = json5::from_string(text, reader.doc);
    ASSERT_FALSE(err) << "failed to parse hand-written JSON5";

    Widget fresh;
    reader.read(&fresh);
    EXPECT_EQ(fresh.flags(), ButtonFlags::Bold);
}
