#include "newui/componentsettings.h"
#include "newui/win32/winheaders.h"
#include "newui/component.h"
#include "newui/controls.h"
#include "newui/utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace newui;

namespace {

// A throwaway folder under the real temp dir, removed again when the test ends - so no test ever
// touches the real %APPDATA%/%PROGRAMDATA%.
class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(SpecialFolders::temp() + "/newui_settings_test_" + name) {
        std::filesystem::remove_all(utf8ToWide(path_));
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(utf8ToWide(path_), ec);
    }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

std::string readFile(const std::string& path) {
    std::ifstream file(utf8ToWide(path), std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// Not reflected on purpose: only its hooks, not any property, take part in settings.
class HookedComponent : public Component {
public:
    int loaded = -1;
    int storeValue = 5;

    void loadSettings(SettingsAccess& access) override {
        if (const SettingValue* v = access.get("custom")) {
            loaded = static_cast<int>(std::get<std::int64_t>(*v));
        }
    }
    void storeSettings(SettingsAccess& access) override {
        access.set("custom", static_cast<std::int64_t>(storeValue));
    }
};

}  // namespace

// ---- SettingsStore ----------------------------------------------------------

TEST(SettingsStore, RoundTripsEveryValueType) {
    TempDir dir("roundtrip");
    SettingsStore out(dir.path() + "/a/b/settings.json");  // folders that don't exist yet
    out.set("comp", "flag", true);
    out.set("comp", "count", static_cast<std::int64_t>(-42));
    out.set("comp", "ratio", 0.25);
    out.set("comp", "title", std::string("hello"));
    out.set("other", "x", static_cast<std::int64_t>(1));
    ASSERT_TRUE(out.save());

    SettingsStore in(out.filePath());
    ASSERT_TRUE(in.load());
    EXPECT_EQ(std::get<bool>(*in.find("comp", "flag")), true);
    EXPECT_EQ(std::get<std::int64_t>(*in.find("comp", "count")), -42);
    EXPECT_DOUBLE_EQ(std::get<double>(*in.find("comp", "ratio")), 0.25);
    EXPECT_EQ(std::get<std::string>(*in.find("comp", "title")), "hello");
    EXPECT_EQ(std::get<std::int64_t>(*in.find("other", "x")), 1);
    EXPECT_EQ(in.find("comp", "missing"), nullptr);
    EXPECT_EQ(in.find("nope", "flag"), nullptr);
}

TEST(SettingsStore, StringEscapingRoundTrips) {
    TempDir dir("escape");
    const std::string tricky = "quote\" backslash\\ newline\n tab\t unicode \xC3\xA9";
    SettingsStore out(dir.path() + "/settings.json");
    out.set("c", "s", tricky);
    ASSERT_TRUE(out.save());

    SettingsStore in(out.filePath());
    ASSERT_TRUE(in.load());
    EXPECT_EQ(std::get<std::string>(*in.find("c", "s")), tricky);
}

TEST(SettingsStore, MissingFileLoadsEmptyAndReportsFalse) {
    TempDir dir("missing");
    SettingsStore store(dir.path() + "/settings.json");
    store.set("c", "k", true);
    EXPECT_FALSE(store.load());
    EXPECT_TRUE(store.empty());
}

TEST(SettingsStore, CorruptFileLoadsEmptyAndReportsFalse) {
    TempDir dir("corrupt");
    std::filesystem::create_directories(utf8ToWide(dir.path()));
    {
        std::ofstream f(utf8ToWide(dir.path() + "/settings.json"), std::ios::binary);
        f << "{ this is not json";
    }
    SettingsStore store(dir.path() + "/settings.json");
    EXPECT_FALSE(store.load());
    EXPECT_TRUE(store.empty());
}

TEST(SettingsStore, DirtyTracking) {
    TempDir dir("dirty");
    SettingsStore store(dir.path() + "/settings.json");
    EXPECT_FALSE(store.dirty());

    store.set("c", "k", false);  // a brand-new key counts even when its value is the variant's default
    EXPECT_TRUE(store.dirty());
    ASSERT_TRUE(store.save());
    EXPECT_FALSE(store.dirty());

    store.set("c", "k", false);  // same value again: nothing changed
    EXPECT_FALSE(store.dirty());
    store.set("c", "k", true);
    EXPECT_TRUE(store.dirty());
}

TEST(SettingsStore, SaveWithoutPathFails) {
    SettingsStore store;
    store.set("c", "k", true);
    EXPECT_FALSE(store.save());
}

TEST(SettingsStore, RemoveDropsEmptyComponentEntries) {
    SettingsStore store;
    store.set("c", "k", true);
    EXPECT_TRUE(store.remove("c", "k"));
    EXPECT_FALSE(store.remove("c", "k"));
    EXPECT_TRUE(store.empty());
}

// ---- Component settings declarations ----------------------------------------

TEST(ComponentSettings, AssignSettingTwiceUpdatesInsteadOfDuplicating) {
    Component c;
    c.assignSetting("value");
    c.assignSetting("value", SettingScope::Machine);
    ASSERT_EQ(c.settings().size(), 1u);
    EXPECT_EQ(c.settings()[0].scope, SettingScope::Machine);
    EXPECT_NE(c.findSetting("value"), nullptr);
    EXPECT_TRUE(c.removeSetting("value"));
    EXPECT_TRUE(c.settings().empty());
}

TEST(ComponentSettings, KeyCanDifferFromPropertyName) {
    Component c;
    c.assignSetting("value", SettingScope::User, "sliderPos");
    EXPECT_NE(c.findSetting("sliderPos"), nullptr);
    EXPECT_EQ(c.findSetting("value"), nullptr);
}

// ---- SettingsManager --------------------------------------------------------

TEST(SettingsManager, PropertiesRoundTripThroughFiles) {
    TempDir dir("manager");

    {
        SettingsManager manager;
        manager.setRootDirectoryOverride(dir.path());
        auto* slider = new Slider();
        slider->assignSetting("value");
        slider->assignSetting("horizontal");
        ASSERT_TRUE(manager.registerComponent(*slider, "slider1"));

        slider->setValue(42.0f);
        slider->setHorizontal(false);
        EXPECT_TRUE(manager.storeAll());
        manager.unregisterComponent(*slider);
        slider->destroy();
        delete slider;
    }

    EXPECT_TRUE(std::filesystem::exists(utf8ToWide(dir.path() + "/user/settings.json")));

    {
        SettingsManager manager;
        manager.setRootDirectoryOverride(dir.path());
        auto* slider = new Slider();
        slider->assignSetting("value");
        slider->assignSetting("horizontal");
        ASSERT_NE(slider->value(), 42.0f);

        ASSERT_TRUE(manager.registerComponent(*slider, "slider1"));  // loads on registration
        EXPECT_FLOAT_EQ(slider->value(), 42.0f);
        EXPECT_FALSE(slider->isHorizontal());

        manager.unregisterComponent(*slider);
        slider->destroy();
        delete slider;
    }
}

TEST(SettingsManager, MachineScopeGoesToItsOwnFile) {
    TempDir dir("scopes");
    SettingsManager manager;
    manager.setRootDirectoryOverride(dir.path());

    auto* slider = new Slider();
    slider->assignSetting("value", SettingScope::Machine);
    slider->assignSetting("horizontal", SettingScope::User);
    ASSERT_TRUE(manager.registerComponent(*slider, "s"));
    slider->setValue(7.0f);
    ASSERT_TRUE(manager.storeAll());

    const std::string machine = readFile(dir.path() + "/machine/settings.json");
    const std::string user = readFile(dir.path() + "/user/settings.json");
    EXPECT_NE(machine.find("\"value\""), std::string::npos);
    EXPECT_EQ(machine.find("\"horizontal\""), std::string::npos);
    EXPECT_NE(user.find("\"horizontal\""), std::string::npos);
    EXPECT_EQ(user.find("\"value\""), std::string::npos);

    manager.unregisterComponent(*slider);
    slider->destroy();
    delete slider;
}

TEST(SettingsManager, MissingAndUnknownSettingsAreHarmless) {
    TempDir dir("harmless");
    SettingsManager manager;
    manager.setRootDirectoryOverride(dir.path());

    auto* slider = new Slider();
    slider->setValue(3.0f);
    slider->assignSetting("value");                       // nothing stored yet
    slider->assignSetting("noSuchProperty");              // not a real property
    EXPECT_NO_THROW(manager.registerComponent(*slider, "s"));
    EXPECT_FLOAT_EQ(slider->value(), 3.0f);               // first run: property keeps its value
    EXPECT_NO_THROW(manager.storeAll());

    manager.unregisterComponent(*slider);
    slider->destroy();
    delete slider;
}

TEST(SettingsManager, RejectsEmptyAndDuplicateKeys) {
    SettingsManager manager;
    manager.setRootDirectoryOverride(SpecialFolders::temp() + "/newui_settings_test_keys");
    Component a, b;
    EXPECT_FALSE(manager.registerComponent(a, "", false));
    EXPECT_TRUE(manager.registerComponent(a, "same", false));
    EXPECT_FALSE(manager.registerComponent(b, "same", false));
    EXPECT_TRUE(manager.registerComponent(a, "renamed", false));  // re-registering renames
    EXPECT_TRUE(manager.registerComponent(b, "same", false));
    EXPECT_TRUE(manager.isRegistered(a));
    manager.unregisterComponent(a);
    EXPECT_FALSE(manager.isRegistered(a));
}

TEST(SettingsManager, CustomHooksSeeStoredValues) {
    TempDir dir("hooks");
    {
        SettingsManager manager;
        manager.setRootDirectoryOverride(dir.path());
        HookedComponent c;
        c.storeValue = 99;
        ASSERT_TRUE(manager.registerComponent(c, "hooked"));
        ASSERT_TRUE(manager.storeAll());
    }
    {
        SettingsManager manager;
        manager.setRootDirectoryOverride(dir.path());
        HookedComponent c;
        ASSERT_TRUE(manager.registerComponent(c, "hooked"));
        EXPECT_EQ(c.loaded, 99);
    }
}

TEST(SettingsManager, UnchangedStoresDoNotRewriteFiles) {
    TempDir dir("norewrite");
    SettingsManager manager;
    manager.setRootDirectoryOverride(dir.path());
    auto* slider = new Slider();
    slider->assignSetting("value");
    ASSERT_TRUE(manager.registerComponent(*slider, "s"));
    slider->setValue(5.0f);
    ASSERT_TRUE(manager.storeAll());

    const std::wstring file = utf8ToWide(dir.path() + "/user/settings.json");
    std::filesystem::remove(file);
    EXPECT_TRUE(manager.storeAll());            // nothing changed, so nothing is written...
    EXPECT_FALSE(std::filesystem::exists(file));

    manager.unregisterComponent(*slider);
    slider->destroy();
    delete slider;
}

TEST(SettingsManager, DefaultPathsUseCompanyAndAppFolders) {
    SettingsManager manager;
    manager.setAppName("MyApp");
    EXPECT_EQ(manager.settingsFilePath(SettingScope::User), SpecialFolders::roamingAppData() + "/MyApp/settings.json");
    EXPECT_EQ(manager.settingsFilePath(SettingScope::Machine), SpecialFolders::programData() + "/MyApp/settings.json");

    manager.setCompanyName("Acme: Corp");  // characters Windows forbids in a folder name become '_'
    EXPECT_EQ(manager.settingsFilePath(SettingScope::User), SpecialFolders::roamingAppData() + "/Acme_ Corp/MyApp/settings.json");
}
