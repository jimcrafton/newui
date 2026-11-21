#include "newui/reflection.h"
#include "newui/utils.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace newui::reflection {

    namespace {
        // True for a type Property::writeValue() can actually emit: a registered Class or Enum, a
        // string, bool, or an arithmetic type it has a case for. Anything else (a raw BLFont, ...) is
        // never written, so it can never make a value differ from its default.
        bool isWritableType(const std::type_index& t) {
            if (classinfo(t) != nullptr || ReflectionRegistry::getEnum(t) != nullptr) {
                return true;
            }
            for (const std::type_index& known : {
                     std::type_index(typeid(std::string)), std::type_index(typeid(std::wstring)),
                     std::type_index(typeid(bool)), std::type_index(typeid(float)),
                     std::type_index(typeid(double)), std::type_index(typeid(std::uint8_t)),
                     std::type_index(typeid(std::int8_t)), std::type_index(typeid(char)),
                     std::type_index(typeid(std::uint16_t)), std::type_index(typeid(std::int16_t)),
                     std::type_index(typeid(std::uint32_t)), std::type_index(typeid(std::int32_t)),
                     std::type_index(typeid(std::uint64_t)), std::type_index(typeid(std::int64_t)),
                     std::type_index(typeid(unsigned long long)), std::type_index(typeid(long long))}) {
                if (t == known) {
                    return true;
                }
            }
            return false;
        }
    }

    // What a default-constructed instance of one concrete Class looked like - see
    // Class::isDefaultValue(). Built once per class and then only read.
    class DefaultSnapshot {
    public:
        // Nested objects deeper than this are treated as "can't tell", i.e. always written - a guard
        // against a getter graph that loops back on itself.
        static constexpr int kMaxDepth = 8;

        static std::shared_ptr<DefaultSnapshot> capture(const Class* clazz, void* instance, int depth) {
            auto snap = std::make_shared<DefaultSnapshot>();
            snap->clazz_ = clazz;

            std::vector<const Field*> fields;
            clazz->allFields(fields);
            for (const Field* f : fields) {
                try {
                    snap->captureField(f, instance, depth);
                } catch (...) {
                    // Left untracked; fieldMatches() then reports "not default" for it.
                    snap->brokenFields_.insert(f->name());
                }
            }

            std::vector<const Property*> props;
            clazz->allProperties(props);
            for (const Property* p : props) {
                try {
                    snap->captureProperty(p, instance, depth);
                } catch (...) {
                    // Left untracked, so the property is simply always written.
                }
            }
            return snap;
        }

        bool propertyMatches(const Property* p, void* instance) const {
            try {
                const std::string& name = p->name();
                if (auto it = counts_.find(name); it != counts_.end()) {
                    const auto* pc = dynamic_cast<const PropertyCollection*>(p);
                    std::any boxed = p->get(instance);
                    if (pc == nullptr || pc->count(boxed) != it->second) {
                        return false;
                    }
                    if (it->second == 0) {
                        return true;
                    }
                    // Same non-zero count as a default instance (constructor-built children, like a
                    // Slider's thumb): default only if every element is itself still at its default.
                    auto el = elements_.find(name);
                    if (el == elements_.end() || el->second.size() != it->second) {
                        return false;
                    }
                    for (std::size_t i = 0; i < it->second; ++i) {
                        void* addr = pc->elementAddress(boxed, i);
                        if (addr == nullptr || pc->elementRuntimeClass(boxed, i) != el->second[i]->clazz_ ||
                            !el->second[i]->matches(addr)) {
                            return false;
                        }
                    }
                    return true;
                }
                if (auto it = leaves_.find(name); it != leaves_.end()) {
                    return p->valuesEqual(p->get(instance), it->second);
                }
                if (nulls_.count(name) != 0) {
                    return p->isAddressable() && p->address(instance) == nullptr;
                }
                if (opaque_.count(name) != 0) {
                    return true;
                }
                if (auto it = nested_.find(name); it != nested_.end()) {
                    if (!p->isAddressable()) {
                        std::any boxed = p->get(instance);
                        void* addr = boxed.has_value() ? p->boxedValueAddress(boxed) : nullptr;
                        return addr != nullptr && it->second->matches(addr);
                    }
                    void* addr = p->address(instance);
                    return addr != nullptr && p->getClass(instance) == it->second->clazz_ &&
                           it->second->matches(addr);
                }
            } catch (...) {
            }
            return false;
        }

        bool fieldMatches(const Field* f, void* instance) const {
            try {
                if (f->isCollection() || brokenFields_.count(f->name()) != 0) {
                    return false;
                }
                const std::string key = "f:" + f->name();
                if (auto it = leaves_.find(key); it != leaves_.end()) {
                    return f->valuesEqual(f->get(instance), it->second);
                }
                if (auto it = nested_.find(key); it != nested_.end()) {
                    void* addr = f->address(instance);
                    return addr != nullptr && it->second->matches(addr);
                }
                // Nothing tracked: a member write() can't emit (a delegate/pointer member), so
                // there is nothing to differ.
                return true;
            } catch (...) {
            }
            return false;
        }

        // True when every property and public field of the object at `instance` is still at its default.
        bool matches(void* instance) const {
            std::vector<const Delegate*> delegates;
            clazz_->allDelegates(delegates);
            for (const Delegate* d : delegates) {
                if (!d->describedListeners(instance).empty()) {
                    return false;
                }
            }
            std::vector<const Field*> fields;
            clazz_->allFields(fields);
            for (const Field* f : fields) {
                if (f->scope() == Scope::Public && !fieldMatches(f, instance)) {
                    return false;
                }
            }
            std::vector<const Property*> props;
            clazz_->allProperties(props);
            for (const Property* p : props) {
                if (!propertyMatches(p, instance)) {
                    return false;
                }
            }
            return true;
        }

    private:
        void captureField(const Field* f, void* instance, int depth) {
            if (f->scope() != Scope::Public || f->isCollection()) {
                return;
            }
            const std::string key = "f:" + f->name();
            if (f->canCompareValues()) {
                if (std::any v = f->get(instance); v.has_value()) {
                    leaves_[key] = std::move(v);
                }
            } else if (depth < kMaxDepth && !f->isStatic() && classinfo(f->type()) != nullptr) {
                if (void* addr = f->address(instance)) {
                    nested_[key] = capture(classinfo(f->type()), addr, depth + 1);
                }
            }
        }

        void captureProperty(const Property* p, void* instance, int depth) {
            const std::string& name = p->name();
            if (const auto* pc = dynamic_cast<const PropertyCollection*>(p)) {
                std::any boxed = p->get(instance);
                const std::size_t n = pc->count(boxed);
                counts_[name] = n;
                if (depth < kMaxDepth) {
                    std::vector<std::shared_ptr<DefaultSnapshot>> elems;
                    for (std::size_t i = 0; i < n; ++i) {
                        void* addr = pc->elementAddress(boxed, i);
                        const Class* rc = pc->elementRuntimeClass(boxed, i);
                        if (addr == nullptr || rc == nullptr) {
                            elems.clear();
                            break;
                        }
                        elems.push_back(capture(rc, addr, depth + 1));
                    }
                    if (elems.size() == n && n != 0) {
                        elements_[name] = std::move(elems);
                    }
                }
            } else if (!isWritableType(p->type())) {
                // A type Property::writeValue() has no case for (e.g. a raw BLFont): nothing is
                // ever written for it, so there is nothing that could differ from the default.
                opaque_.insert(name);
            } else if (p->canCompareValues()) {
                if (std::any v = p->get(instance); v.has_value()) {
                    leaves_[name] = std::move(v);
                }
            } else if (depth < kMaxDepth) {
                if (p->isAddressable()) {
                    void* addr = p->address(instance);
                    if (addr == nullptr) {
                        nulls_.insert(name);
                    } else if (const Class* rc = p->getClass(instance); rc != nullptr) {
                        nested_[name] = capture(rc, addr, depth + 1);
                    }
                } else {
                    // By-value getter: inspect the returned copy (alive until this returns).
                    std::any boxed = p->get(instance);
                    if (void* addr = boxed.has_value() ? p->boxedValueAddress(boxed) : nullptr) {
                        nested_[name] = capture(classinfo(p->type()), addr, depth + 1);
                    }
                }
            }
        }

        const Class* clazz_ = nullptr;
        std::set<std::string> brokenFields_;
        std::map<std::string, std::any> leaves_;
        std::map<std::string, std::size_t> counts_;
        std::set<std::string> nulls_;
        std::map<std::string, std::vector<std::shared_ptr<DefaultSnapshot>>> elements_;
        std::set<std::string> opaque_;
        std::map<std::string, std::shared_ptr<DefaultSnapshot>> nested_;
    };

    bool Class::isDefaultValue(const Property* property, void* instance) const {
        try {
            if (property->canCompareValues() && property->defaultVal().has_value()) {
                return property->valuesEqual(property->get(instance), property->defaultVal());
            }
            ensureDefaults();
            return defaults_ != nullptr && defaults_->propertyMatches(property, instance);
        } catch (...) {
            return false;
        }
    }

    bool Class::isDefaultField(const Field* field, void* instance) const {
        try {
            if (field->canCompareValues() && field->defaultVal().has_value()) {
                return field->valuesEqual(field->get(instance), field->defaultVal());
            }
            ensureDefaults();
            return defaults_ != nullptr && defaults_->fieldMatches(field, instance);
        } catch (...) {
            return false;
        }
    }

    void Class::ensureDefaults() const {
        if (defaultsTried_) {
            return;
        }
        defaultsTried_ = true;
        const bool excluded = isSingleton() || isAbstract() ||
            std::find(tags_.begin(), tags_.end(), "nodefaults") != tags_.end();
        void* raw = nullptr;
        if (!excluded) {
            createInstance(&raw);
        }
        if (raw == nullptr) {
            return;
        }
        try {
            defaults_ = DefaultSnapshot::capture(this, raw, 0);
        } catch (...) {
        }
        // A View's destroy() releases its subtree; everything else just needs deleting.
        if (const Method* m = method("destroy"); m != nullptr) {
            m->invoke(raw, {});
        }
        deleteInstance(raw);
    }

    // Reads back a "0x..." hex token - the fallback Property::writeValue()
    // (below) emits for a value/leftover bits nothing in the Enum's own
    // declared values could name (see Enum::decompose()'s own comment) -
    // std::stoull throwing (a malformed/truncated token, e.g. hand-edited)
    // just means "not one of these", same as tryParse() failing.
    namespace {
        bool tryParseHexToken(const std::string& token, std::uint64_t& outValue) {
            if (token.size() <= 2 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) {
                return false;
            }
            try {
                std::size_t consumed = 0;
                outValue = std::stoull(token.substr(2), &consumed, 16);
                return consumed == token.size() - 2;
            } catch (...) {
                return false;
            }
        }
    }

    // Walks parentClass() (most-derived first, i.e. starting at `this`) so a
    // single-name lookup finds an inherited member too, not just one
    // declared directly on this class - the same "most-derived-first"
    // resolution allProperties()/allFields()/allDelegates() already do via
    // their own merge, just without building a whole merged vector for a
    // single lookup. TypedClass<T>::read() (below) now drives its per-
    // object read loop from the source document's own keys and resolves
    // each one through exactly this call - a Button node's "visible" key
    // (a View property, never redeclared on Button) has to resolve here or
    // reading anything but a class's own directly-declared properties would
    // silently do nothing for every inherited one.
    const Property* Class::property(const std::string& propertyName) const {
        for (const Class* c = this; c != nullptr; c = c->parentClass()) {
            for (const auto* p : c->properties_) {
                if (p->name() == propertyName) {
                    return p;
                }
            }
        }
        return nullptr;
    }

    const Field* Class::field(const std::string& fieldName) const {
        for (const Class* c = this; c != nullptr; c = c->parentClass()) {
            for (const auto* f : c->fields_) {
                if (f->name() == fieldName) {
                    return f;
                }
            }
        }
        return nullptr;
    }

    const Method* Class::method(const std::string& methodName) const {
        for (const Class* c = this; c != nullptr; c = c->parentClass()) {
            for (const auto* m : c->methods_) {
                if (m->name() == methodName) {
                    return m;
                }
            }
        }
        return nullptr;
    }

    const Delegate* Class::delegate(const std::string& delegateName) const {
        for (const Class* c = this; c != nullptr; c = c->parentClass()) {
            for (const auto* d : c->delegates_) {
                if (d->name() == delegateName) {
                    return d;
                }
            }
        }
        return nullptr;
    }

    std::any Class::createInstance() const {
        for (const auto* ctor : constructors_) {
            if (ctor->arguments().empty()) {
                return ctor->invoke({});
            }
        }
        return std::any();
    }

    std::any Class::createInstance(const std::vector<std::any>& args) const {
        for (const auto* ctor : constructors_) {
            if (ctor->arguments().size() == args.size()) {
                return ctor->invoke(args);
            }
        }
        return std::any();
    }

    std::any Class::createInstance(void** outRaw) const {
        if (outRaw != nullptr) {
            *outRaw = nullptr;
        }
        for (const auto* ctor : constructors_) {
            if (ctor->arguments().empty()) {
                return ctor->invoke({}, outRaw);
            }
        }
        return std::any();
    }

    Class::~Class() {
        for (auto* p : properties_) {
            delete p;
        }
        for (auto* f : fields_) {
            delete f;
        }
        for (auto* m : methods_) {
            delete m;
        }
        for (auto* d : delegates_) {
            delete d;
        }
        for (auto* c : constructors_) {
            delete c;
        }
    }

    ReflectionRegistry::~ReflectionRegistry() {
        for (auto& [type, classPtr] : classesByType_) {
            delete classPtr;
        }
    }

    ReflectionRegistry& ReflectionRegistry::instance() {
        static ReflectionRegistry registry;
        return registry;
    }

    void ReflectionRegistry::registerClass(Class* classInfo) {
        auto& reg = instance();
        std::type_index type = classInfo->type();
        reg.classNameToType_.insert_or_assign(classInfo->name(), type);

        // Also index the fully-qualified name ("newui::Rect") alongside
        // the bare one ("Rect") so getClass()/classinfo() can resolve
        // either - only when they actually differ (a global-namespace
        // class has an empty namespaceName(), making qualifiedName() ==
        // name(), so this would otherwise just repeat the insert above).
        std::string qualified = classInfo->qualifiedName();
        if (qualified != classInfo->name()) {
            reg.classNameToType_.insert_or_assign(std::move(qualified), type);
        }

        auto existing = reg.classesByType_.find(type);
        if (existing != reg.classesByType_.end()) {
            delete existing->second;
            existing->second = classInfo;
        } else {
            reg.classesByType_.emplace(type, classInfo);
        }
    }

    void ReflectionRegistry::registerEnum(Enum enumInfo) {
        auto& reg = instance();
        std::type_index type = enumInfo.type();
        reg.enumNameToType_.insert_or_assign(enumInfo.name(), type);

        // Same fully-qualified-alongside-bare indexing as registerClass()
        // above - only when they actually differ (a global-scope enum has
        // an empty namespaceName(), making qualifiedName() == name()).
        std::string qualified = enumInfo.qualifiedName();
        if (qualified != enumInfo.name()) {
            reg.enumNameToType_.insert_or_assign(std::move(qualified), type);
        }

        reg.enumsByType_.insert_or_assign(type, std::move(enumInfo));
    }

    const Class* ReflectionRegistry::getClass(std::type_index type) {
        auto& reg = instance();
        auto found = reg.classesByType_.find(type);
        return found != reg.classesByType_.end() ? found->second : nullptr;
    }

    const Class* ReflectionRegistry::getClass(const std::string& name) {
        auto& reg = instance();
        auto found = reg.classNameToType_.find(name);
        if (found == reg.classNameToType_.end()) {
            return nullptr;
        }
        return getClass(found->second);
    }

    std::vector<const Class*> ReflectionRegistry::allClasses() {
        auto& reg = instance();
        std::vector<const Class*> result;
        result.reserve(reg.classesByType_.size());
        for (auto& [type, classPtr] : reg.classesByType_) {
            result.push_back(classPtr);
        }
        return result;
    }

    std::vector<const Class*> ReflectionRegistry::classesWithTag(const std::string& tag) {
        std::vector<const Class*> result;
        for (const Class* clazz : allClasses()) {
            const auto& tags = clazz->tags();
            if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                result.push_back(clazz);
            }
        }
        return result;
    }

    std::vector<const Class*> ReflectionRegistry::classesWithCategory(const std::string& category) {
        std::vector<const Class*> result;
        for (const Class* clazz : allClasses()) {
            const auto& categories = clazz->categories();
            if (std::find(categories.begin(), categories.end(), category) != categories.end()) {
                result.push_back(clazz);
            }
        }
        return result;
    }

    namespace {
        std::vector<std::string> uniqueSorted(const std::vector<const Class*>& classes,
                const std::vector<std::string>& (Class::*accessor)() const) {
            std::set<std::string> unique;
            for (const Class* clazz : classes) {
                for (const std::string& value : (clazz->*accessor)()) {
                    unique.insert(value);
                }
            }
            return std::vector<std::string>(unique.begin(), unique.end());
        }
    }

    std::vector<std::string> ReflectionRegistry::allTags() {
        return uniqueSorted(allClasses(), &Class::tags);
    }

    std::vector<std::string> ReflectionRegistry::allCategories() {
        return uniqueSorted(allClasses(), &Class::categories);
    }

    const Enum* ReflectionRegistry::getEnum(std::type_index type) {
        auto& reg = instance();
        auto found = reg.enumsByType_.find(type);
        return found != reg.enumsByType_.end() ? &found->second : nullptr;
    }

    const Enum* ReflectionRegistry::getEnum(const std::string& name) {
        auto& reg = instance();
        auto found = reg.enumNameToType_.find(name);
        if (found == reg.enumNameToType_.end()) {
            return nullptr;
        }
        return getEnum(found->second);
    }

    void ReflectionRegistry::addInitFunction(InitFunc initFunc)
    {
        auto& reg = instance();
        reg.initList_.push_back(initFunc);
    }

    void ReflectionRegistry::init()
    {
        auto& reg = instance();
        for (auto initFunc : reg.initList_) {
            initFunc();
        }
    }

    // Scalar/enum leaf values only - every real caller (TypedProperty::
    // read()'s by-value fallback, TypedMemberField::read()'s fallback,
    // TypedPropertyCollection::readFreshElement()'s scalar-container
    // branch) already checks classinfo(valType) itself and only reaches
    // this function when it's null, i.e. valType is never a registered
    // Class here - that recursion goes through ClassReader::readInto()
    // directly at each of those call sites instead (see their own
    // comments), not through here.
    void Property::readValue(const std::string& valName, const std::type_index& valType, std::any& val, void* instancePtr, ClassReader* reader)
    {
        if (const Enum* valEnum = ReflectionRegistry::getEnum(valType); valEnum != nullptr) {
            // Mirrors writeValue()'s own enum branch below - one string
            // (plain enum) or an array of them (flags enum, Enum::
            // isFlags()) - see EnumBuilder<T>::flags()'s own comment for
            // why that's always an explicit registration-time choice, not
            // guessed here from valType alone.
            std::uint64_t combined = 0;
            if (valEnum->isFlags()) {
                std::size_t n = reader->beginCollection(valName);
                for (std::size_t i = 0; i < n; ++i) {
                    std::string token;
                    reader->readString("", token);
                    std::uint64_t flagValue = 0;
                    if (valEnum->tryParse(token, flagValue) || tryParseHexToken(token, flagValue)) {
                        combined |= flagValue;
                    }
                    // An unrecognized token (a flag name from a newer
                    // version of this enum, or a hand-edit typo) is
                    // silently dropped rather than failing the whole
                    // read - same "absent/unrecognized data reads back as
                    // default" contract every other ObjectReader field
                    // already has (see reflectionio.h's own DocumentMetadata
                    // comment).
                }
                reader->endCollection(valName);
            }
            else {
                std::string token;
                reader->readString(valName, token);
                if (!valEnum->tryParse(token, combined)) {
                    tryParseHexToken(token, combined);
                }
            }
            val = valEnum->fromUInt64(combined);
        }
        else {
            if (valType == typeid(std::string)) {
                std::string str;
                reader->readString(valName, str);
                val = str;
            }
            else if (valType == typeid(std::wstring)) {
                // e.g. TextField::text() - the reader's own readString()/
                // writeString() are UTF-8-only, so a wide-string property
                // round-trips through newui::utf8ToWide()/wideToUtf8()
                // (utils.h) at this one boundary rather than needing its
                // own reader/writer primitives.
                std::string str;
                reader->readString(valName, str);
                val = utf8ToWide(str);
            }
            else if (valType == typeid(bool)) {
                bool b;
                reader->readBool(valName, b);
                val = b;
            }
            else if (valType == typeid(float)) {
                float f;
                reader->readFloat(valName, f);
                val = f;
            }
            else if (valType == typeid(double)) {
                double d;
                reader->readDouble(valName, d);
                val = d;
            }
            else if (valType == typeid(std::uint8_t) || valType == typeid(unsigned char)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::uint8_t)i;
            }
            else if (valType == typeid(std::int8_t) || valType == typeid(char)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::int8_t)i;
            }
            else if (valType == typeid(std::uint16_t) || valType == typeid(unsigned short)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::uint16_t)i;
            }
            else if (valType == typeid(std::int16_t) || valType == typeid(short)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::int16_t)i;
            }
            else if (valType == typeid(std::uint32_t) || valType == typeid(unsigned int)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::uint32_t)i;
            }
            else if (valType == typeid(std::int32_t) || valType == typeid(int)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::int32_t)i;
            }
            else if (valType == typeid(std::uint64_t) || valType == typeid(unsigned long long)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::uint64_t)i;
            }
            else if (valType == typeid(std::int64_t) || valType == typeid(long long)) {
                std::int32_t i;
                reader->readInt(valName, i);
                val = (std::int64_t)i;
            }
        }
    }

    void Property::writeValue(const std::string& valName, const std::type_index& valType, const std::any& val, void* instancePtr, ClassWriter* writer)
    {
        // A registered nested Class with a live instancePtr - the case
        // TypedPropertyCollection::writeItem() hits for a polymorphic
        // collection element (e.g. a SubView* in "childViews", resolved to
        // its own runtime type) - recurses the same way the Property-based
        // writeValue() overload above does for a plain nested property.
        // valName is always "" here (a collection element is positional,
        // not keyed - see Class::write()'s own doc comment), so this
        // passes an empty name straight through, same as that overload's
        // own owningProperty == nullptr case does.
        if (const Class* valClazz = classinfo(valType); valClazz != nullptr && instancePtr != nullptr) {
            valClazz->write(instancePtr, writer, std::string());
            return;
        }
        if (const Enum* valEnum = ReflectionRegistry::getEnum(valType); valEnum != nullptr) {
            // Mirrors readValue()'s own enum branch - one string (plain
            // enum) or an array of them (flags enum, Enum::isFlags()) -
            // see EnumBuilder<T>::flags()'s own comment for why that's
            // always an explicit registration-time choice, not guessed
            // here from valType alone. A value with no exact declared
            // match (plain enum) or leftover bits nothing declared can
            // name (flags enum) falls back to a "0x..." hex token rather
            // than silently writing nothing - see Enum::decompose()'s own
            // comment.
            std::uint64_t raw = valEnum->toUInt64(val);
            if (valEnum->isFlags()) {
                writer->beginCollection(valName);
                for (const std::string& flagName : valEnum->decompose(raw)) {
                    writer->writeString("", flagName);
                }
                writer->endCollection(valName);
            }
            else {
                std::string name;
                if (!valEnum->tryToString(raw, name)) {
                    std::ostringstream hex;
                    hex << "0x" << std::hex << raw;
                    name = hex.str();
                }
                writer->writeString(valName, name);
            }
            return;
        }
        if (valType == typeid(std::string)) {
            writer->writeString(valName, std::any_cast<std::string>(val));
        }
        else if (valType == typeid(std::wstring)) {
            writer->writeString(valName, wideToUtf8(std::any_cast<std::wstring>(val)));
        }
        else if (valType == typeid(bool)) {
            writer->writeBool(valName, std::any_cast<bool>(val));
        }
        else if (valType == typeid(float)) {
            writer->writeFloat(valName, std::any_cast<float>(val));
        }
        else if (valType == typeid(double)) {
            writer->writeDouble(valName, std::any_cast<double>(val));
        }
        else if (valType == typeid(std::uint8_t) || valType == typeid(unsigned char)) {
            writer->writeInt8(valName, std::any_cast<std::uint8_t>(val), false);
        }
        else if (valType == typeid(std::int8_t) || valType == typeid(char)) {
            writer->writeInt8(valName, std::any_cast<std::int8_t>(val), true);
        }
        else if (valType == typeid(std::uint16_t) || valType == typeid(unsigned short)) {
            writer->writeInt16(valName, std::any_cast<std::uint16_t>(val), false);
        }
        else if (valType == typeid(std::int16_t) || valType == typeid(short)) {
            writer->writeInt16(valName, std::any_cast<std::int16_t>(val), true);
        }
        else if (valType == typeid(std::uint32_t) || valType == typeid(unsigned int)) {
            writer->writeInt32(valName, std::any_cast<std::uint32_t>(val), false);
        }
        else if (valType == typeid(std::int32_t) || valType == typeid(int)) {
            writer->writeInt32(valName, std::any_cast<std::int32_t>(val), true);
        }
        else if (valType == typeid(std::uint64_t) || valType == typeid(unsigned long long)) {
            writer->writeInt64(valName, std::any_cast<std::uint64_t>(val), false);
        }
        else if (valType == typeid(std::int64_t) || valType == typeid(long long)) {
            writer->writeInt64(valName, std::any_cast<std::int64_t>(val), true);
        }
    }

    void Property::writeValue(const Property* property, const std::any& val, void* instancePtr, ClassWriter* writer)
    {
        auto valType = property->type();
        auto valName = property->name();
        const Class* valClazz = classinfo(valType);
        // A registered nested Class with a currently-null instancePtr
        // (TypedProperty::PtrGetter mode - e.g. Application's "frame"
        // before any Frame has been set) has nothing to recurse into;
        // silently skipped, same as the scalar branches below skip a
        // std::any that doesn't hold what val() looks like.
        if (nullptr != valClazz && nullptr != instancePtr) {
            valClazz->write(instancePtr, writer, valName);
        }
        else if (nullptr == valClazz) {
            
            Property::writeValue(valName, valType, val, instancePtr, writer);
        }
    }

}
