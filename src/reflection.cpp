#include "newui/reflection.h"

namespace newui::reflection {

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

    const Property* Class::property(const std::string& propertyName) const {
        for (const auto* p : properties_) {
            if (p->name() == propertyName) {
                return p;
            }
        }
        return nullptr;
    }

    const Field* Class::field(const std::string& fieldName) const {
        for (const auto* f : fields_) {
            if (f->name() == fieldName) {
                return f;
            }
        }
        return nullptr;
    }

    const Method* Class::method(const std::string& methodName) const {
        for (const auto* m : methods_) {
            if (m->name() == methodName) {
                return m;
            }
        }
        return nullptr;
    }

    const Delegate* Class::delegate(const std::string& delegateName) const {
        for (const auto* d : delegates_) {
            if (d->name() == delegateName) {
                return d;
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

    void Property::readValue(const std::string& valName, const std::type_index& valType, std::any& val, void* instancePtr, ClassReader* reader)
    {
        const Class* valClazz = classinfo(valType);
        if (nullptr != valClazz) {
            // Was `std::any val;` here - shadowed the outer val parameter,
            // so a nested read's result never actually reached the
            // caller. Writes straight into the outer val now, same
            // in/out contract TypedProperty::read()'s own nested-Class
            // branches rely on (an already-populated val addresses
            // existing storage to write into; an empty one signals "build
            // a fresh heap instance").
            bool onHeap = false;
            valClazz->read(reader, valName, val, onHeap);
        }
        else if (const Enum* valEnum = ReflectionRegistry::getEnum(valType); valEnum != nullptr) {
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
