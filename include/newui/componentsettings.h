#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Component-level settings: a Component declares which of its reflected properties are "settings"
// (assignSetting() in component.h); a SettingsManager loads those values from, and stores them back to,
// small JSON5 files - one per scope - when the application starts and ends.
//
// Modelled on VCF's ComponentSetting/Component::initializeSettings()/storeSettings(), minus its
// Windows-registry-only storage: the user/machine distinction VCF made with HKCU vs HKLM is kept here
// as SettingScope, but the backing store is a plain file (see SettingsStore) so it is easy to inspect,
// reset and test.
//
// Troubleshooting - "my settings are never saved / restored":
//   Settings are read and written through the component's *reflected* properties. If the reflection
//   registry is empty, classinfo() returns null and every setting is skipped silently - no error, no
//   file. A standalone exe must therefore call registerReflectionData() once at the top of main():
//       extern void registerReflectionData();
//       int main() { registerReflectionData(); ... }
//   Other causes of the same symptom: the property name passed to assignSetting() isn't a reflected
//   property of the component's most-derived class; the component is registered under a different key
//   between runs; or the app name is empty (no folder to name the settings file after).

namespace newui {

    class Component;

    // Where a setting is persisted. User settings belong to the current user account; Machine settings
    // are shared by every user of the computer (and normally need write access to %PROGRAMDATA%).
    // @reflect ignore=true
    enum class SettingScope {
        User,
        Machine,
    };

    // The value types a setting can persist. Narrower integer types and float are widened to these on
    // the way into a SettingsStore and narrowed back to the property's own type on the way out.
    using SettingValue = std::variant<bool, std::int64_t, double, std::string>;

    // One persisted property of a Component.
    // @reflect ignore=true
    struct ComponentSetting {
        // The reflected property on the owning component this setting reads and writes. May be empty for
        // a setting that Component::loadSettings()/storeSettings() handles entirely by hand.
        std::string propertyName;

        // The name it is stored under within its component's entry. Defaults to propertyName, but need not
        // match it - the same way a VCF setting's dictionary key could differ from its property name.
        std::string key;

        SettingScope scope = SettingScope::User;
    };

    // One JSON5 file's worth of settings, held in memory: componentKey -> (setting key -> value).
    // Missing or unparseable files simply load as empty - a first run has no settings yet.
    // @reflect ignore=true
    class SettingsStore {
    public:
        SettingsStore() = default;
        explicit SettingsStore(std::string filePath) : filePath_(std::move(filePath)) {}

        const std::string& filePath() const { return filePath_; }
        void setFilePath(std::string filePath) { filePath_ = std::move(filePath); }

        // Replaces the in-memory contents with the file's. Returns false - with the store left empty -
        // when the file is missing, unreadable or isn't valid JSON5 of the shape save() writes.
        bool load();

        // Writes the in-memory contents to filePath(), creating its folders, via a temporary file that is
        // renamed over the target so a crash mid-write never leaves a half-written settings file.
        // Returns false if filePath() is empty or the write failed. A successful save clears dirty().
        bool save();

        const SettingValue* find(const std::string& componentKey, const std::string& key) const;
        void set(const std::string& componentKey, const std::string& key, SettingValue value);
        bool remove(const std::string& componentKey, const std::string& key);
        void clear();

        bool empty() const { return data_.empty(); }

        // True once anything was set/removed/cleared since the last load() or save() - lets the manager
        // skip rewriting an unchanged file.
        bool dirty() const { return dirty_; }

    private:
        std::string filePath_;
        std::map<std::string, std::map<std::string, SettingValue>> data_;
        bool dirty_ = false;
    };

    class SettingsManager;

    // What Component::loadSettings()/storeSettings() receive: this component's own entry in the manager's
    // stores, for settings that are not (only) a reflected property.
    // @reflect ignore=true
    class SettingsAccess {
    public:
        SettingsAccess(SettingsManager& manager, std::string componentKey)
            : manager_(manager), componentKey_(std::move(componentKey)) {}

        const std::string& componentKey() const { return componentKey_; }

        // nullptr if nothing is stored for key in scope.
        const SettingValue* get(const std::string& key, SettingScope scope = SettingScope::User) const;
        void set(const std::string& key, SettingValue value, SettingScope scope = SettingScope::User);

    private:
        SettingsManager& manager_;
        std::string componentKey_;
    };

    // Owns the two stores (user and machine), knows which components take part, and moves values between
    // them. Application owns one (Application::settings()); a test or tool may construct its own.
    //
    // Components are registered by an explicit, caller-chosen key (e.g. "mainFrame", "sidebar.splitter") -
    // that key is what appears in the file, so it must stay stable across runs and be unique per
    // component. A registered Component must outlive the manager's storeAll(), or be unregistered first.
    // @reflect ignore=true
    class SettingsManager {
    public:
        SettingsManager();
        ~SettingsManager();

        SettingsManager(const SettingsManager&) = delete;
        SettingsManager& operator=(const SettingsManager&) = delete;

        // Where the files live: <root>\<company>\<app>\settings.json, with root being %APPDATA% for the
        // user scope and %PROGRAMDATA% for the machine scope. The company folder is skipped when there is
        // none. Defaults come from Bundle (Info.json's "company" and "name", else Application's name).
        // (Paths are worked out when the files are first read or written, not here - the Application
        // singleton may still be under construction when its own SettingsManager is.)
        void setCompanyName(const std::string& company) { companyOverride_ = company; }
        void setAppName(const std::string& app) { appOverride_ = app; }

        // Replaces both roots (user -> <dir>\user, machine -> <dir>\machine, no company/app folders), so a
        // test never touches the real %APPDATA%/%PROGRAMDATA%. An empty dir goes back to the real folders.
        void setRootDirectoryOverride(const std::string& dir) { rootOverride_ = dir; loaded_ = false; }

        std::string settingsFilePath(SettingScope scope) const;

        // Adds component under key and, if loadNow, applies its stored values to it immediately. Registering
        // the same component again changes its key. Returns false if key is empty or already taken by a
        // different component.
        bool registerComponent(Component& component, const std::string& key, bool loadNow = true);
        void unregisterComponent(Component& component);
        bool isRegistered(const Component& component) const;

        // Reads both files from disk, then applies the stored values to every registered component.
        void loadAll();

        // Collects every registered component's current values into the stores and writes any file that
        // changed. Returns false if a file that needed writing could not be written.
        bool storeAll();

        // Per-component versions of the two above (neither touches the disk).
        void applyToComponent(Component& component);
        void collectFromComponent(Component& component);

        // Calls storeAll() every interval on the application run loop, so settings survive a crash. Zero
        // (the default) turns it off. Only takes effect once the run loop is running.
        void setAutosaveInterval(std::chrono::milliseconds interval);

        SettingsStore& store(SettingScope scope) { return scope == SettingScope::User ? user_ : machine_; }
        const SettingsStore& store(SettingScope scope) const { return scope == SettingScope::User ? user_ : machine_; }

    private:
        struct Entry {
            Component* component;
            std::string key;
        };

        struct AutosaveState;

        void refreshPaths();
        void ensureLoaded();
        const Entry* findEntry(const Component& component) const;

        std::vector<Entry> entries_;
        SettingsStore user_;
        SettingsStore machine_;
        std::string companyOverride_;
        std::string appOverride_;
        std::string rootOverride_;
        std::shared_ptr<AutosaveState> autosave_;
        bool loaded_ = false;
    };

}  // namespace newui
