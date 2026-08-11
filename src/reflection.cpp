#include "newui/reflection.h"

namespace newui::reflection {

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

}
