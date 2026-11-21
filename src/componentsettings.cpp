#include "newui/componentsettings.h"
#include "newui/bundle.h"
#include "newui/component.h"
#include "newui/reflection.h"
#include "newui/runloop.h"
#include "newui/utils.h"

#include <json5/json5.hpp>
#include <json5/json5_input.hpp>
#include <json5/json5_output.hpp>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <sstream>
#include <typeindex>

namespace newui {

    // ---------------------------------------------------------------------
    // SettingsStore
    // ---------------------------------------------------------------------

    namespace {

        void appendJsonString(std::string& out, const std::string& text) {
            out += '"';
            for (unsigned char c : text) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            out += buf;
                        }
                        else {
                            out += static_cast<char>(c);  // UTF-8 bytes pass through unchanged
                        }
                }
            }
            out += '"';
        }

        // False for a value JSON can't represent (NaN/infinity) - the caller leaves that setting out.
        bool appendJsonValue(std::string& out, const SettingValue& value) {
            if (const bool* b = std::get_if<bool>(&value)) {
                out += *b ? "true" : "false";
            }
            else if (const std::int64_t* i = std::get_if<std::int64_t>(&value)) {
                out += std::to_string(*i);
            }
            else if (const double* d = std::get_if<double>(&value)) {
                if (!std::isfinite(*d)) {
                    return false;
                }
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.17g", *d);
                out += buf;
            }
            else {
                appendJsonString(out, std::get<std::string>(value));
            }
            return true;
        }

        std::wstring widePath(const std::string& path) {
            return utf8ToWide(path);
        }

        void createParentDirectories(const std::string& filePath) {
            std::size_t slash = filePath.find_last_of("/\\");
            if (slash == std::string::npos) {
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(widePath(filePath.substr(0, slash))), ec);
        }

    }  // namespace

    bool SettingsStore::load() {
        data_.clear();
        dirty_ = false;

        if (filePath_.empty()) {
            return false;
        }

        std::ifstream file(widePath(filePath_), std::ios::binary);
        if (!file) {
            return false;
        }
        std::ostringstream contents;
        contents << file.rdbuf();

        json5::document doc;
        if (json5::from_string(contents.str(), doc)) {
            return false;  // not valid JSON5
        }
        if (!doc.is_object()) {
            return false;
        }

        for (const auto& [componentKey, componentValue] : json5::object_view(doc)) {
            if (!componentValue.is_object()) {
                continue;
            }
            auto& entry = data_[std::string(componentKey)];
            for (const auto& [key, value] : json5::object_view(componentValue)) {
                if (value.is_boolean()) {
                    entry[std::string(key)] = value.get_bool();
                }
                else if (value.is_string()) {
                    entry[std::string(key)] = std::string(value.get_c_str());
                }
                else if (value.is_number()) {
                    // JSON has one number type: whole numbers come back as integers, anything else as
                    // double - applying a value to a property converts to whatever type it really is.
                    double d = value.get<double>();
                    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15) {
                        entry[std::string(key)] = static_cast<std::int64_t>(d);
                    }
                    else {
                        entry[std::string(key)] = d;
                    }
                }
                // null, arrays and nested objects aren't something this store ever writes - ignored.
            }
        }
        return true;
    }

    bool SettingsStore::save() {
        if (filePath_.empty()) {
            return false;
        }

        std::string text = "{\n";
        bool firstComponent = true;
        for (const auto& [componentKey, entry] : data_) {
            if (!firstComponent) {
                text += ",\n";
            }
            firstComponent = false;
            text += "  ";
            appendJsonString(text, componentKey);
            text += ": {";
            bool firstValue = true;
            for (const auto& [key, value] : entry) {
                std::string line;
                if (!appendJsonValue(line, value)) {
                    continue;
                }
                text += firstValue ? "\n    " : ",\n    ";
                firstValue = false;
                appendJsonString(text, key);
                text += ": ";
                text += line;
            }
            text += firstValue ? "}" : "\n  }";
        }
        text += firstComponent ? "}\n" : "\n}\n";

        createParentDirectories(filePath_);

        const std::wstring target = widePath(filePath_);
        const std::wstring temp = target + L".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }
            file << text;
            file.flush();
            if (!file) {
                return false;
            }
        }
        if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ::DeleteFileW(temp.c_str());
            return false;
        }

        dirty_ = false;
        return true;
    }

    const SettingValue* SettingsStore::find(const std::string& componentKey, const std::string& key) const {
        auto component = data_.find(componentKey);
        if (component == data_.end()) {
            return nullptr;
        }
        auto value = component->second.find(key);
        return value == component->second.end() ? nullptr : &value->second;
    }

    void SettingsStore::set(const std::string& componentKey, const std::string& key, SettingValue value) {
        auto& entry = data_[componentKey];
        auto existing = entry.find(key);
        if (existing == entry.end()) {
            entry.emplace(key, std::move(value));
            dirty_ = true;
        }
        else if (existing->second != value) {
            existing->second = std::move(value);
            dirty_ = true;
        }
    }

    bool SettingsStore::remove(const std::string& componentKey, const std::string& key) {
        auto component = data_.find(componentKey);
        if (component == data_.end() || component->second.erase(key) == 0) {
            return false;
        }
        if (component->second.empty()) {
            data_.erase(component);
        }
        dirty_ = true;
        return true;
    }

    void SettingsStore::clear() {
        if (!data_.empty()) {
            dirty_ = true;
        }
        data_.clear();
    }

    // ---------------------------------------------------------------------
    // Property <-> SettingValue conversion
    // ---------------------------------------------------------------------

    namespace {

        bool asInt64(const SettingValue& v, std::int64_t& out) {
            if (const auto* i = std::get_if<std::int64_t>(&v)) { out = *i; return true; }
            if (const auto* d = std::get_if<double>(&v)) {
                if (!std::isfinite(*d)) return false;
                out = static_cast<std::int64_t>(std::llround(*d));
                return true;
            }
            if (const auto* b = std::get_if<bool>(&v)) { out = *b ? 1 : 0; return true; }
            return false;
        }

        bool asDouble(const SettingValue& v, double& out) {
            if (const auto* d = std::get_if<double>(&v)) { out = *d; return true; }
            if (const auto* i = std::get_if<std::int64_t>(&v)) { out = static_cast<double>(*i); return true; }
            return false;
        }

        bool asBool(const SettingValue& v, bool& out) {
            if (const auto* b = std::get_if<bool>(&v)) { out = *b; return true; }
            if (const auto* i = std::get_if<std::int64_t>(&v)) { out = *i != 0; return true; }
            return false;
        }

        template<typename T>
        bool readIntegral(const std::any& a, SettingValue& out) {
            if (a.type() != typeid(T)) {
                return false;
            }
            out = static_cast<std::int64_t>(std::any_cast<T>(a));
            return true;
        }

        template<typename T>
        bool makeIntegral(std::type_index type, const SettingValue& v, std::any& out) {
            if (type != typeid(T)) {
                return false;
            }
            std::int64_t i = 0;
            if (asInt64(v, i)) {
                out = static_cast<T>(i);
            }
            return true;  // matched the type even if v didn't convert (out stays empty)
        }

        // Empty optional: the value's type isn't one a setting can persist.
        bool toSettingValue(const std::any& a, SettingValue& out) {
            if (a.type() == typeid(bool)) { out = std::any_cast<bool>(a); return true; }
            if (a.type() == typeid(float)) { out = static_cast<double>(std::any_cast<float>(a)); return true; }
            if (a.type() == typeid(double)) { out = std::any_cast<double>(a); return true; }
            if (a.type() == typeid(std::string)) { out = std::any_cast<std::string>(a); return true; }
            return readIntegral<int>(a, out) || readIntegral<unsigned int>(a, out)
                || readIntegral<long>(a, out) || readIntegral<unsigned long>(a, out)
                || readIntegral<long long>(a, out) || readIntegral<unsigned long long>(a, out)
                || readIntegral<short>(a, out) || readIntegral<unsigned short>(a, out)
                || readIntegral<signed char>(a, out) || readIntegral<unsigned char>(a, out);
        }

        // Empty any on return: couldn't convert (wrong kind of value for the property's type).
        std::any fromSettingValue(std::type_index type, const SettingValue& v) {
            std::any out;
            if (type == typeid(bool)) {
                bool b = false;
                if (asBool(v, b)) out = b;
                return out;
            }
            if (type == typeid(float)) {
                double d = 0;
                if (asDouble(v, d)) out = static_cast<float>(d);
                return out;
            }
            if (type == typeid(double)) {
                double d = 0;
                if (asDouble(v, d)) out = d;
                return out;
            }
            if (type == typeid(std::string)) {
                if (const auto* s = std::get_if<std::string>(&v)) out = *s;
                return out;
            }
            (void)(makeIntegral<int>(type, v, out) || makeIntegral<unsigned int>(type, v, out)
                || makeIntegral<long>(type, v, out) || makeIntegral<unsigned long>(type, v, out)
                || makeIntegral<long long>(type, v, out) || makeIntegral<unsigned long long>(type, v, out)
                || makeIntegral<short>(type, v, out) || makeIntegral<unsigned short>(type, v, out)
                || makeIntegral<signed char>(type, v, out) || makeIntegral<unsigned char>(type, v, out));
            return out;
        }

        // The pointer the reflected property expects: the most-derived object, not the Component subobject.
        void* instanceOf(Component& component) {
            return dynamic_cast<void*>(&component);
        }

        const reflection::Class* classOf(Component& component) {
            return reflection::classinfo(std::type_index(typeid(component)));
        }

        std::string effectiveKey(const ComponentSetting& setting) {
            return setting.key.empty() ? setting.propertyName : setting.key;
        }

        std::string safeFolderName(std::string name) {
            for (char& c : name) {
                if (std::string("\\/:*?\"<>|").find(c) != std::string::npos) {
                    c = '_';
                }
            }
            return name;
        }

    }  // namespace

    // ---------------------------------------------------------------------
    // Component (settings part)
    // ---------------------------------------------------------------------

    ComponentSetting& Component::assignSetting(const std::string& propertyName, SettingScope scope,
                                               const std::string& key) {
        const std::string effective = key.empty() ? propertyName : key;
        for (ComponentSetting& existing : settings_) {
            if (effectiveKey(existing) == effective) {
                existing.propertyName = propertyName;
                existing.key = key;
                existing.scope = scope;
                return existing;
            }
        }
        settings_.push_back(ComponentSetting{propertyName, key, scope});
        return settings_.back();
    }

    const ComponentSetting* Component::findSetting(const std::string& key) const {
        for (const ComponentSetting& setting : settings_) {
            if (effectiveKey(setting) == key) {
                return &setting;
            }
        }
        return nullptr;
    }

    bool Component::removeSetting(const std::string& key) {
        auto it = std::find_if(settings_.begin(), settings_.end(),
            [&](const ComponentSetting& s) { return effectiveKey(s) == key; });
        if (it == settings_.end()) {
            return false;
        }
        settings_.erase(it);
        return true;
    }

    // ---------------------------------------------------------------------
    // SettingsAccess
    // ---------------------------------------------------------------------

    const SettingValue* SettingsAccess::get(const std::string& key, SettingScope scope) const {
        return manager_.store(scope).find(componentKey_, key);
    }

    void SettingsAccess::set(const std::string& key, SettingValue value, SettingScope scope) {
        manager_.store(scope).set(componentKey_, key, std::move(value));
    }

    // ---------------------------------------------------------------------
    // SettingsManager
    // ---------------------------------------------------------------------

    // Shared with the autosave timer task, which can outlive the manager: alive is cleared in the
    // destructor, and generation is bumped by every setAutosaveInterval() so a superseded timer retires
    // itself the next time it fires.
    struct SettingsManager::AutosaveState {
        bool alive = true;
        int generation = 0;
    };

    SettingsManager::SettingsManager() = default;

    SettingsManager::~SettingsManager() {
        if (autosave_) {
            autosave_->alive = false;
        }
    }

    std::string SettingsManager::settingsFilePath(SettingScope scope) const {
        if (!rootOverride_.empty()) {
            return normalizePath(rootOverride_) + (scope == SettingScope::User ? "/user" : "/machine") + "/settings.json";
        }

        std::string root = scope == SettingScope::User ? SpecialFolders::roamingAppData() : SpecialFolders::programData();
        if (root.empty()) {
            return std::string();
        }

        const std::string company = !companyOverride_.empty() ? companyOverride_ : Bundle::instance().company();
        const std::string app = !appOverride_.empty() ? appOverride_ : Bundle::instance().appName();
        if (app.empty()) {
            return std::string();  // nothing to name the folder after
        }

        if (!company.empty()) {
            root += "/" + safeFolderName(company);
        }
        return root + "/" + safeFolderName(app) + "/settings.json";
    }

    void SettingsManager::refreshPaths() {
        user_.setFilePath(settingsFilePath(SettingScope::User));
        machine_.setFilePath(settingsFilePath(SettingScope::Machine));
    }

    void SettingsManager::ensureLoaded() {
        if (!loaded_) {
            refreshPaths();
            user_.load();
            machine_.load();
            loaded_ = true;
        }
    }

    const SettingsManager::Entry* SettingsManager::findEntry(const Component& component) const {
        for (const Entry& entry : entries_) {
            if (entry.component == &component) {
                return &entry;
            }
        }
        return nullptr;
    }

    bool SettingsManager::isRegistered(const Component& component) const {
        return findEntry(component) != nullptr;
    }

    bool SettingsManager::registerComponent(Component& component, const std::string& key, bool loadNow) {
        if (key.empty()) {
            return false;
        }
        for (const Entry& entry : entries_) {
            if (entry.key == key && entry.component != &component) {
                return false;
            }
        }

        bool found = false;
        for (Entry& entry : entries_) {
            if (entry.component == &component) {
                entry.key = key;
                found = true;
            }
        }
        if (!found) {
            entries_.push_back(Entry{&component, key});
        }

        if (loadNow) {
            ensureLoaded();
            applyToComponent(component);
        }
        return true;
    }

    void SettingsManager::unregisterComponent(Component& component) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [&](const Entry& e) { return e.component == &component; }), entries_.end());
    }

    void SettingsManager::applyToComponent(Component& component) {
        const Entry* entry = findEntry(component);
        if (entry == nullptr) {
            return;
        }
        const std::string componentKey = entry->key;

        if (!component.settings().empty()) {
            const reflection::Class* clazz = classOf(component);
            void* instance = instanceOf(component);

            for (const ComponentSetting& setting : component.settings()) {
                if (setting.propertyName.empty()) {
                    continue;
                }
                const SettingValue* stored = store(setting.scope).find(componentKey, effectiveKey(setting));
                if (stored == nullptr) {
                    continue;  // first run, or never stored: the property keeps its current value
                }
                const reflection::Property* property = clazz != nullptr ? clazz->property(setting.propertyName) : nullptr;
                if (property == nullptr) {
                    continue;  // class isn't reflected, or the property no longer exists
                }
                std::any value = fromSettingValue(property->type(), *stored);
                if (!value.has_value()) {
                    continue;  // stored value's kind doesn't fit the property's type
                }
                try {
                    property->set(instance, value);
                }
                catch (const std::exception&) {
                    // A property whose setter rejects/throws on this value keeps its current one.
                }
            }
        }

        SettingsAccess access(*this, componentKey);
        component.loadSettings(access);
    }

    void SettingsManager::collectFromComponent(Component& component) {
        const Entry* entry = findEntry(component);
        if (entry == nullptr) {
            return;
        }
        const std::string componentKey = entry->key;

        SettingsAccess access(*this, componentKey);
        component.storeSettings(access);

        if (component.settings().empty()) {
            return;
        }
        const reflection::Class* clazz = classOf(component);
        void* instance = instanceOf(component);
        for (const ComponentSetting& setting : component.settings()) {
            if (setting.propertyName.empty() || clazz == nullptr) {
                continue;
            }
            const reflection::Property* property = clazz->property(setting.propertyName);
            if (property == nullptr) {
                continue;
            }
            SettingValue value;
            try {
                if (toSettingValue(property->get(instance), value)) {
                    store(setting.scope).set(componentKey, effectiveKey(setting), std::move(value));
                }
            }
            catch (const std::exception&) {
                // Unreadable property: leave whatever was stored before.
            }
        }
    }

    void SettingsManager::loadAll() {
        refreshPaths();
        user_.load();
        machine_.load();
        loaded_ = true;
        for (const Entry& entry : std::vector<Entry>(entries_)) {
            applyToComponent(*entry.component);
        }
    }

    bool SettingsManager::storeAll() {
        ensureLoaded();  // never overwrite a file with a store that was never read from it
        for (const Entry& entry : std::vector<Entry>(entries_)) {
            collectFromComponent(*entry.component);
        }

        bool ok = true;
        if (user_.dirty()) {
            ok = user_.save() && ok;
        }
        if (machine_.dirty()) {
            ok = machine_.save() && ok;
        }
        return ok;
    }

    void SettingsManager::setAutosaveInterval(std::chrono::milliseconds interval) {
        if (!autosave_) {
            autosave_ = std::make_shared<AutosaveState>();
        }
        const int generation = ++autosave_->generation;
        if (interval.count() <= 0) {
            return;  // any running timer sees the new generation on its next tick and stops
        }

        // The run loop may not be pumping yet (Application::onStartup runs before it does), and
        // postDelayed() needs one that is - so arm the timer from an idle task, which is queued either way.
        std::shared_ptr<AutosaveState> state = autosave_;
        SettingsManager* self = this;
        RunLoop::current().postIdle([state, self, generation, interval]() {
            if (!state->alive || state->generation != generation) {
                return true;
            }
            try {
                RunLoop::current().postDelayed(interval, [state, self, generation]() {
                    if (!state->alive || state->generation != generation) {
                        return true;   // superseded or manager gone: this timer is done
                    }
                    self->storeAll();
                    return false;      // keep firing
                });
            }
            catch (const std::exception&) {
                // No running loop after all - autosave just stays off.
            }
            return true;
        });
    }

}  // namespace newui
