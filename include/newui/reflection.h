#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <newui/delegate.h>
#include <newui/uicomponent.h>

namespace newui::reflection {

    // Forward-declared, never given a generic body - referencing
    // ClassAccess<T> for a T nobody ever explicitly specialized is a hard
    // "incomplete type" compile error, not silent UB. NEWUI_REFLECT_FRIEND()
    // (below) friends this template - friending a template (not one
    // specialization) grants friendship to every specialization, including
    // ones written later in a different translation unit, so a class only
    // has to say "trust newui::reflection's accessor template" once, not
    // once per field.
    //
    // One friended struct per class, not one per field: a
    // TypedProperty<T,ValueT>/TypedDelegate<T,Args...>/TypedField<ValueT>
    // that needs access to a private member gets it by adding one named
    // static method to that class's own ClassAccess<T> specialization -
    // &T::field_ is only legal to *evaluate* inside that specialization's
    // own (friended) body, but the pointer-to-member/pointer value it
    // returns isn't itself privileged, so anything can read it back out
    // afterwards. No per-field index to track - just give each field's
    // accessor method whatever name is convenient (typically the field's
    // own name).
    namespace detail {
        template<typename T> struct ClassAccess;
    }

    // Placed once in a class body to grant newui::reflection's ClassAccess<T>
    // template access to this class's private/protected members - nothing
    // more. There's no opt-in/exclude marker here: reflectgen (once it
    // exists) scans every class it's pointed at unconditionally, rather than
    // checking for an annotation first. Without this macro, a class's
    // private/protected members simply can't be reached by a ClassAccess<T>
    // specialization (a hand-written one, or a generated one later) - only
    // its public members can, via ordinary access rules. Real under any
    // compiler (unlike a class-level annotation for an AST walk to find,
    // this has to actually take effect in the real build for generated/
    // hand-written accessor code to compile at all).
#define NEWUI_REFLECT_FRIEND() \
    template<typename NewuiReflectT_> friend struct newui::reflection::detail::ClassAccess

    enum class Scope {
        Public,
        Protected,
        Private,
    };

    // One parameter of a Method/Delegate/Constructor. name is best-effort
    // (may be empty - not every call site has one available) and never
    // affects invoke(); only position within the argument list and type do.
    struct Argument {
        std::string name;
        std::type_index type;
    };

    // A static class variable. No instance is involved anywhere in this
    // class's API - address()/get()/set() all act on the one fixed storage
    // location the static variable already has. Concrete (not abstract) -
    // the constructor below wires up the same std::any-thunk-based
    // implementation this class always had; TypedField<ValueT> (below)
    // overrides address()/get()/set() instead of using it.
    class Field {
    public:
        Field(std::string name, std::type_index type, void* address,
               std::any (*get)(), void (*set)(const std::any&))
            : name_(std::move(name)), type_(type), address_(address), get_(get), set_(set) {}

        virtual ~Field() = default;

        const std::string& name() const { return name_; }
        std::type_index type() const { return type_; }

        virtual void* address() const { return address_; }
        virtual std::any get() const { return get_ ? get_() : std::any(); }
        virtual void set(const std::any& value) const { if (set_) set_(value); }

        // Convenience for a caller that already knows T at compile time -
        // just std::any_cast<T> on top of get(), still throws
        // std::bad_any_cast on a mismatch rather than silently misreading.
        template<typename T>
        T getAs() const {
            return std::any_cast<T>(get());
        }

    private:
        template<typename T> friend class ClassBuilder;

        using GetFn = std::any (*)();
        using SetFn = void (*)(const std::any&);

        std::string name_;
        std::type_index type_;
        void* address_;
        GetFn get_;
        SetFn set_;
    };

    // T-aware TypedField<ValueT> - the static variable's real address is
    // known and fixed at registration time (no instance involved, unlike
    // Property below), so there's no accessor-function indirection at all:
    // just a real ValueT* stored directly. Adds no std::any-thunk state of
    // its own; address()/get()/set() are overridden outright.
    template<typename ValueT>
    class TypedField : public Field {
    public:
        TypedField(std::string name, ValueT* address)
            : Field(std::move(name), typeid(ValueT), nullptr, nullptr, nullptr), address_(address) {}

        void* address() const override { return address_; }
        std::any get() const override { return std::any(*address_); }
        void set(const std::any& value) const override { *address_ = std::any_cast<ValueT>(value); }

    private:
        ValueT* address_;
    };

    // A member variable, reflected regardless of its real C++ access level
    // (see NEWUI_REFLECT_FRIEND()'s friend declaration) - scope() records the
    // real access level as metadata, it doesn't gate whether address()/get()/
    // set() work. address() is the zero-copy live pointer into instance;
    // get()/set() are a boxed convenience layer on top for callers that only
    // have a name string and no compile-time type to cast address() with.
    class Property {
    public:
        Property(std::string name, std::type_index type, Scope scope,
                  void* (*address)(void*), std::any (*get)(void*), void (*set)(void*, const std::any&))
            : name_(std::move(name)), type_(type), scope_(scope), address_(address), get_(get), set_(set) {}

        virtual ~Property() = default;

        const std::string& name() const { return name_; }
        std::type_index type() const { return type_; }
        Scope scope() const { return scope_; }

        virtual void* address(void* instance) const { return address_ ? address_(instance) : nullptr; }
        virtual std::any get(void* instance) const { return get_ ? get_(instance) : std::any(); }
        virtual void set(void* instance, const std::any& value) const { if (set_) set_(instance, value); }

        // Convenience for a caller that already knows T at compile time -
        // just std::any_cast<T> on top of get(), still throws
        // std::bad_any_cast on a mismatch rather than silently misreading.
        template<typename T>
        T getAs(void* instance) const {
            return std::any_cast<T>(get(instance));
        }

    private:
        template<typename T> friend class ClassBuilder;

        using AddressFn = void* (*)(void*);
        using GetFn = std::any (*)(void*);
        using SetFn = void (*)(void*, const std::any&);

        std::string name_;
        std::type_index type_;
        Scope scope_;
        AddressFn address_;
        GetFn get_;
        SetFn set_;
    };

    // T-aware TypedProperty<SourceT,ValueT> - stores a real pointer-to-
    // data-member (ValueT SourceT::*) instead of a hand-written void*-
    // returning accessor function. instance->*member_ is valid C++ for any
    // access level as long as *evaluating* &SourceT::field_ was legal at
    // the point this pointer-to-member value was obtained - for a private
    // field that means going through a detail::ClassAccess<T> specialization
    // the same way the untyped path did (see reflection.h's top comment),
    // just handing back a pointer-to-member instead of a void*(*)(void*)
    // function. address()/get()/set() are overridden to work straight off
    // member_, so ClassBuilder<T>::property()'s typed overload doesn't need
    // any separately hand-written thunk functions at all - compare to the
    // untyped overload, which still needs one each for address/get/set.
    template<typename SourceT, typename ValueT>
    class TypedProperty : public Property {
    public:
        using MemberPtr = ValueT SourceT::*;

        TypedProperty(std::string name, Scope scope, MemberPtr member)
            : Property(std::move(name), typeid(ValueT), scope, nullptr, nullptr, nullptr), member_(member) {}

        void* address(void* instance) const override {
            return &(static_cast<SourceT*>(instance)->*member_);
        }
        std::any get(void* instance) const override {
            return std::any(static_cast<SourceT*>(instance)->*member_);
        }
        void set(void* instance, const std::any& value) const override {
            static_cast<SourceT*>(instance)->*member_ = std::any_cast<ValueT>(value);
        }

        // Same idea as Property::getAs<T>(), but no std::any round-trip at
        // all - a live reference straight through the pointer-to-member.
        ValueT& getTyped(SourceT& instance) const { return instance.*member_; }
        void setTyped(SourceT& instance, const ValueT& value) const { instance.*member_ = value; }

    private:
        MemberPtr member_;
    };

    // A member function. Unlike Property/Field/Delegate, invoke() only ever
    // works for genuinely public methods - constructing or calling code is a
    // bigger trust boundary than reading/writing an existing field, so
    // (unlike Property/Field/Delegate) this deliberately doesn't reach past
    // real C++ access control. A private/protected Method still appears in
    // Class::methods() with correct name/scope/signature metadata (useful
    // for introspection/tooling), but invoke_ stays null and invoke() throws
    // - same idiom Property<SourceT,ValueT>::interpolate(t) already uses in
    // property.h for an unsupported operation, rather than a silent no-op.
    class Method {
    public:
        Method(std::string name, Scope scope, bool isVirtual, bool isAbstract,
                bool hasReturnValue, std::type_index returnType, std::vector<Argument> arguments,
                std::any (*invoke)(void*, const std::vector<std::any>&))
            : name_(std::move(name)), scope_(scope), isVirtual_(isVirtual), isAbstract_(isAbstract),
              hasReturnValue_(hasReturnValue), returnType_(returnType), arguments_(std::move(arguments)),
              invoke_(invoke) {}

        virtual ~Method() = default;

        const std::string& name() const { return name_; }
        Scope scope() const { return scope_; }
        bool isVirtual() const { return isVirtual_; }
        bool isAbstract() const { return isAbstract_; }
        bool hasReturnValue() const { return hasReturnValue_; }
        std::type_index returnType() const { return returnType_; }
        const std::vector<Argument>& arguments() const { return arguments_; }

        virtual std::any invoke(void* instance, const std::vector<std::any>& args) const {
            if (invoke_ == nullptr) {
                throw std::logic_error("Method::invoke(): '" + name_ + "' is not publicly invocable through reflection");
            }
            return invoke_(instance, args);
        }

    private:
        template<typename T> friend class ClassBuilder;

        using InvokeFn = std::any (*)(void*, const std::vector<std::any>&);

        std::string name_;
        Scope scope_;
        bool isVirtual_;
        bool isAbstract_;
        bool hasReturnValue_;
        std::type_index returnType_;
        std::vector<Argument> arguments_;
        InvokeFn invoke_;
    };

    // T-aware TypedMethod<SourceT,RetT,Args...> - stores a real pointer-to-
    // member-function instead of a hand-written invoker thunk, and unpacks
    // the std::any argument vector itself via std::index_sequence. Only
    // ever built from a genuinely callable (public) member function -
    // &SourceT::method needs no friend access at all when method is
    // public, unlike TypedProperty's private-field case above. Two
    // constructors (not one) since RetT(SourceT::*)(Args...) and
    // RetT(SourceT::*)(Args...) const are unrelated pointer-to-member
    // types in C++ - fn_/constFn_ are both nullable so invoke() just calls
    // whichever one was actually set.
    template<typename SourceT, typename RetT, typename... Args>
    class TypedMethod : public Method {
    public:
        using MemberFn = RetT (SourceT::*)(Args...);
        using ConstMemberFn = RetT (SourceT::*)(Args...) const;

        TypedMethod(std::string name, Scope scope, MemberFn fn)
            : Method(std::move(name), scope, false, false, !std::is_void_v<RetT>, typeid(RetT),
                      { Argument{"", typeid(Args)}... }, nullptr),
              fn_(fn), constFn_(nullptr) {}

        TypedMethod(std::string name, Scope scope, ConstMemberFn fn)
            : Method(std::move(name), scope, false, false, !std::is_void_v<RetT>, typeid(RetT),
                      { Argument{"", typeid(Args)}... }, nullptr),
              fn_(nullptr), constFn_(fn) {}

        std::any invoke(void* instance, const std::vector<std::any>& args) const override {
            return invokeImpl(instance, args, std::index_sequence_for<Args...>{});
        }

    private:
        template<std::size_t... I>
        std::any invokeImpl(void* instance, const std::vector<std::any>& args, std::index_sequence<I...>) const {
            SourceT* self = static_cast<SourceT*>(instance);
            if constexpr (std::is_void_v<RetT>) {
                if (fn_) {
                    (self->*fn_)(std::any_cast<Args>(args.at(I))...);
                } else {
                    (self->*constFn_)(std::any_cast<Args>(args.at(I))...);
                }
                return std::any();
            } else {
                if (fn_) {
                    return std::any((self->*fn_)(std::any_cast<Args>(args.at(I))...));
                }
                return std::any((self->*constFn_)(std::any_cast<Args>(args.at(I))...));
            }
        }

        MemberFn fn_;
        ConstMemberFn constFn_;
    };

    // A newui::Delegate<SenderT, Args...> member specifically - never
    // represented as a Property even though it's a member variable. Reached
    // via the same friend-bypass as Property (see class comment there):
    // invoking a delegate runs whatever callbacks are already subscribed to
    // it, which is closer to "use existing state" than "call arbitrary
    // private implementation code", so unlike Method it isn't restricted to
    // public delegates.
    class Delegate {
    public:
        Delegate(std::string name, Scope scope, std::type_index senderType, std::vector<Argument> arguments,
                  void* (*address)(void*), std::any (*invoke)(void*, const std::vector<std::any>&))
            : name_(std::move(name)), scope_(scope), senderType_(senderType), arguments_(std::move(arguments)),
              address_(address), invoke_(invoke) {}

        virtual ~Delegate() = default;

        const std::string& name() const { return name_; }
        Scope scope() const { return scope_; }
        std::type_index senderType() const { return senderType_; }
        const std::vector<Argument>& arguments() const { return arguments_; }

        virtual void* address(void* instance) const { return address_ ? address_(instance) : nullptr; }

        // Boxes args and triggers the delegate's syncCall(instance, args...)
        // - always returns an empty std::any (syncCall itself returns void;
        // use syncCallFirst()'s SyncReturn directly if that's ever needed -
        // out of scope for this pass).
        virtual std::any invoke(void* instance, const std::vector<std::any>& args) const {
            return invoke_ ? invoke_(instance, args) : std::any();
        }

    private:
        template<typename T> friend class ClassBuilder;

        using AddressFn = void* (*)(void*);
        using InvokeFn = std::any (*)(void*, const std::vector<std::any>&);

        std::string name_;
        Scope scope_;
        std::type_index senderType_;
        std::vector<Argument> arguments_;
        AddressFn address_;
        InvokeFn invoke_;
    };

    // T-aware TypedDelegate<SourceT,Args...> - stores a pointer-to-member
    // for the newui::Delegate<SourceT,Args...> field itself (obtained the
    // same friended way as TypedProperty's member_, for a private
    // delegate), and unpacks std::any args to call syncCall() directly -
    // same std::index_sequence trick as TypedMethod.
    template<typename SourceT, typename... Args>
    class TypedDelegate : public Delegate {
    public:
        using MemberPtr = newui::Delegate<SourceT, Args...> SourceT::*;

        TypedDelegate(std::string name, Scope scope, MemberPtr member)
            : Delegate(std::move(name), scope, typeid(SourceT), { Argument{"", typeid(Args)}... }, nullptr, nullptr),
              member_(member) {}

        void* address(void* instance) const override {
            return &(static_cast<SourceT*>(instance)->*member_);
        }

        std::any invoke(void* instance, const std::vector<std::any>& args) const override {
            return invokeImpl(instance, args, std::index_sequence_for<Args...>{});
        }

    private:
        template<std::size_t... I>
        std::any invokeImpl(void* instance, const std::vector<std::any>& args, std::index_sequence<I...>) const {
            SourceT* self = static_cast<SourceT*>(instance);
            (self->*member_).syncCall(*self, std::any_cast<Args>(args.at(I))...);
            return std::any();
        }

        MemberPtr member_;
    };

    // One name/value pair of a reflected enum. value is always normalized to
    // int64_t regardless of the enum's real underlying type, so Enum doesn't
    // need to be templated on it.
    struct EnumValue {
        std::string name;
        std::int64_t value;
    };

    class Enum {
    public:
        Enum(std::type_index type, std::string name) : type_(type), name_(std::move(name)) {}

        const std::string& name() const { return name_; }
        std::type_index type() const { return type_; }
        const std::vector<EnumValue>& values() const { return values_; }

        bool tryParse(const std::string& valueName, std::int64_t& outValue) const {
            for (const auto& v : values_) {
                if (v.name == valueName) {
                    outValue = v.value;
                    return true;
                }
            }
            return false;
        }

        bool tryToString(std::int64_t value, std::string& outName) const {
            for (const auto& v : values_) {
                if (v.value == value) {
                    outName = v.name;
                    return true;
                }
            }
            return false;
        }

    private:
        friend class EnumBuilder;

        std::type_index type_;
        std::string name_;
        std::vector<EnumValue> values_;
    };

    // One constructor overload. Unlike Property/Field/Delegate, and same as
    // Method, only ever built from a genuinely public constructor - see
    // Class::createInstance()'s comment.
    class Constructor {
    public:
        Constructor(std::vector<Argument> arguments, std::any (*invoke)(const std::vector<std::any>&))
            : arguments_(std::move(arguments)), invoke_(invoke) {}

        virtual ~Constructor() = default;

        const std::vector<Argument>& arguments() const { return arguments_; }

        virtual std::any invoke(const std::vector<std::any>& args) const {
            return invoke_ ? invoke_(args) : std::any();
        }

    private:
        template<typename T> friend class ClassBuilder;

        using InvokeFn = std::any (*)(const std::vector<std::any>&);

        std::vector<Argument> arguments_;
        InvokeFn invoke_;
    };

    // T-aware TypedConstructor<SourceT,Args...> - no hand-written invoker
    // function at all: invoke() unpacks the std::any argument vector via
    // std::index_sequence and calls `new SourceT(args...)` directly. If
    // SourceT has no matching real constructor, this fails to *compile*
    // (not just to run) - a correctness win over the untyped path, where a
    // hand-written invoker's arguments could silently drift out of sync
    // with SourceT's real constructor.
    template<typename SourceT, typename... Args>
    class TypedConstructor : public Constructor {
    public:
        TypedConstructor() : Constructor({ Argument{"", typeid(Args)}... }, nullptr) {}

        std::any invoke(const std::vector<std::any>& args) const override {
            return invokeImpl(args, std::index_sequence_for<Args...>{});
        }

    private:
        template<std::size_t... I>
        std::any invokeImpl(const std::vector<std::any>& args, std::index_sequence<I...>) const {
            return std::any(new SourceT(std::any_cast<Args>(args.at(I))...));
        }
    };

    // Metadata for one C++ class/struct. Holds every reflected
    // Property/Field/Method/Delegate plus its (public-only) constructor
    // overloads - built once via ClassBuilder<T> and handed to
    // ReflectionRegistry::registerClass(), never mutated afterwards.
    //
    // Polymorphic (virtual destructor) so TypedClass<T> (below) can add
    // T-aware construction convenience on top without its own storage -
    // ReflectionRegistry stores/deletes through a base Class* uniformly,
    // whether what's actually there is a plain Class or a TypedClass<T>.
    //
    // properties_/fields_/methods_/delegates_/constructors_ hold owning raw
    // pointers (Property* etc., not Property by value) for the same reason
    // Class itself needs to be polymorphic: a vector<Property> couldn't
    // hold a mix of plain Property and TypedProperty<T,ValueT> entries
    // (slicing), but vector<Property*> can, dispatching through the base's
    // virtual address()/get()/set()/invoke(). ~Class() deletes every
    // pointer it owns; copying a Class is therefore deleted (would either
    // shallow-copy the pointers into a double-free, or need a deep-copy
    // Property::clone() nobody needs) - moving is still fine, it just
    // transfers ownership of the same pointers.
    class Class {
    public:
        Class(std::type_index type, std::string name, std::string namespaceName)
            : type_(type), name_(std::move(name)), namespaceName_(std::move(namespaceName)) {}

        virtual ~Class();
        Class(const Class&) = delete;
        Class& operator=(const Class&) = delete;
        Class(Class&&) = default;
        Class& operator=(Class&&) = default;

        bool isDerived() const { return isDerived_; }
        bool isAbstract() const { return isAbstract_; }
        bool isStruct() const { return isStruct_; }
        bool isSingleton() const { return isSingleton_; }

        std::type_index type() const { return type_; }
        const std::string& name() const { return name_; }
        const std::string& namespaceName() const { return namespaceName_; }

        const std::vector<Property*>& properties() const { return properties_; }
        const std::vector<Field*>& fields() const { return fields_; }
        const std::vector<Method*>& methods() const { return methods_; }
        const std::vector<Delegate*>& delegates() const { return delegates_; }

        const Property* property(const std::string& propertyName) const;
        const Field* field(const std::string& fieldName) const;
        const Method* method(const std::string& methodName) const;
        const Delegate* delegate(const std::string& delegateName) const;

        // Constructors that weren't genuinely public were never added to
        // constructors_ in the first place (see ClassBuilder::addConstructor()'s
        // comment) - so a private/protected-only class simply has no
        // matching entry here, and these return an empty std::any rather
        // than bypassing C++ access control the way Property/Field/Delegate
        // do. Overload resolution is arity-only (first constructor whose
        // argument count matches) - a genuine type mismatch surfaces as
        // whatever invoke() throws (std::bad_any_cast for the untyped path,
        // same for TypedConstructor - though TypedConstructor also rejects
        // an outright wrong Args... list at compile time, before this ever
        // runs). Consistent with every other value crossing the reflection
        // boundary being std::any-based (Property::get()/Method::invoke()),
        // and safer than the void* this used to return: a caller who
        // mis-casts gets std::bad_any_cast, not silent UB. TypedClass<T>::
        // createInstanceTyped() (below) is the T-aware convenience layer on
        // top for callers who already know T.
        std::any createInstance() const;
        std::any createInstance(const std::vector<std::any>& args) const;

    private:
        template<typename T> friend class ClassBuilder;

        std::type_index type_;
        std::string name_;
        std::string namespaceName_;
        bool isDerived_ = false;
        bool isAbstract_ = false;
        bool isStruct_ = false;
        bool isSingleton_ = false;
        std::vector<Property*> properties_;
        std::vector<Field*> fields_;
        std::vector<Method*> methods_;
        std::vector<Delegate*> delegates_;
        std::vector<Constructor*> constructors_;
    };

    // Adds T-aware construction convenience on top of Class's type-erased
    // createInstance() - not required to use reflection (a plain Class is
    // enough for the fully runtime-typed case, e.g. a caller that only has
    // a name string and never names T in source), but the natural choice
    // whenever the concrete type is already known at the registration
    // site, which is always true when going through ClassBuilder<T>
    // (below) - so that's the only way one of these gets built. Adds no
    // data members of its own, purely behavioral sugar over the inherited
    // Class state.
    template<typename T>
    class TypedClass : public Class {
    public:
        TypedClass(std::string name, std::string namespaceName)
            : Class(typeid(T), std::move(name), std::move(namespaceName)) {}

        // nullptr if no matching constructor was ever registered (see
        // Class::createInstance()'s comment) - a genuine type mismatch on a
        // matching-arity overload still throws std::bad_any_cast, same as
        // misusing createInstance() directly would.
        T* createInstanceTyped() const {
            std::any result = createInstance();
            return result.has_value() ? std::any_cast<T*>(result) : nullptr;
        }

        T* createInstanceTyped(const std::vector<std::any>& args) const {
            std::any result = createInstance(args);
            return result.has_value() ? std::any_cast<T*>(result) : nullptr;
        }
    };

    // Fluent assembly for a Class - both hand-written registration and,
    // later, reflectgen-generated registration call the same add*()/build()
    // surface, so hand-wiring a class today is written in exactly the shape
    // generated code will produce, not a throwaway prototype API. Always
    // builds a TypedClass<T> (T deduced from the template argument, not
    // passed as a typeid() - can't be gotten wrong or drift from what's
    // actually being registered), handed back as a base Class* - see
    // ReflectionRegistry::registerClass()'s comment for why that's a raw,
    // registry-owned pointer rather than e.g. std::unique_ptr.
    //
    // Two overloads for each kind of member: an untyped one, which needs a
    // hand-written std::any-based thunk function per accessor (see
    // examples/reflection1.cpp for what that looks like) - and a typed one,
    // which takes a real pointer-to-member(-function) or pointer and needs
    // no separate thunk at all (see TypedProperty/TypedField/TypedMethod/
    // TypedDelegate/TypedConstructor above for how). The untyped path still
    // matters for reflectgen output that would rather emit one generic
    // thunk shape than lean on pointer-to-member syntax - both paths
    // produce ordinary Property/Field/Method/Delegate/Constructor entries
    // in the same Class, indistinguishable to a caller just enumerating
    // Class::properties() etc.
    //
    // For a private/protected member via the untyped path, the AddressFn/
    // GetFn/SetFn passed in must come from a detail::ClassAccess<T>
    // explicit specialization (see reflection.h's top comment) - e.g.
    // &detail::ClassAccess<Foo>::name - since ClassBuilder itself has no
    // special access of its own. Same for a private member's pointer-to-
    // member via the typed path - the specialization just exposes it as a
    // plain static method instead.
    template<typename T>
    class ClassBuilder {
    public:

        typedef  TypedClass<T>  ClassT;

        ClassBuilder()
            //    : class_(std::move(name), std::move(namespaceName)) {} 
        {
            std::string name = demangleTypeName(typeid(T));
            std::string namespaceName = extractNamespace(typeid(T));
            class_.reset( new TypedClass<T>(name, namespaceName) );
        }

        ClassBuilder& clazz() {
            return *this;
        }

        ClassBuilder& derived(bool value = true) { class_->isDerived_ = value; return *this; }
        ClassBuilder& abstract(bool value = true) { class_->isAbstract_ = value; return *this; }
        ClassBuilder& isStruct(bool value = true) { class_->isStruct_ = value; return *this; }
        ClassBuilder& singleton(bool value = true) { class_->isSingleton_ = value; return *this; }

        ClassBuilder& property(std::string name, std::type_index type, Scope scope,
                                   Property::AddressFn address, Property::GetFn get, Property::SetFn set) {
            class_->properties_.push_back(new Property(std::move(name), type, scope, address, get, set));
            return *this;
        }

        template<typename ValueT>
        ClassBuilder& property(std::string name, Scope scope, ValueT T::* member) {
            class_->properties_.push_back(new TypedProperty<T, ValueT>(std::move(name), scope, member));
            return *this;
        }

        ClassBuilder& field(std::string name, std::type_index type, void* address,
                                 Field::GetFn get, Field::SetFn set) {
            class_->fields_.push_back(new Field(std::move(name), type, address, get, set));
            return *this;
        }

        template<typename ValueT>
        ClassBuilder& field(std::string name, ValueT* address) {
            class_->fields_.push_back(new TypedField<ValueT>(std::move(name), address));
            return *this;
        }

        // invoke may be nullptr - a private/protected method still gets a
        // Method entry (for scope()/signature introspection), it just can't
        // be invoked; see Method::invoke()'s comment.
        ClassBuilder& method(std::string name, Scope scope, bool isVirtual, bool isAbstract,
                                  bool hasReturnValue, std::type_index returnType,
                                  std::vector<Argument> arguments, Method::InvokeFn invoke) {
            class_->methods_.push_back(new Method(std::move(name), scope, isVirtual, isAbstract,
                hasReturnValue, returnType, std::move(arguments), invoke));
            return *this;
        }

        template<typename RetT, typename... Args>
        ClassBuilder& method(std::string name, Scope scope, RetT (T::* fn)(Args...)) {
            class_->methods_.push_back(new TypedMethod<T, RetT, Args...>(std::move(name), scope, fn));
            return *this;
        }

        template<typename RetT, typename... Args>
        ClassBuilder& method(std::string name, Scope scope, RetT (T::* fn)(Args...) const) {
            class_->methods_.push_back(new TypedMethod<T, RetT, Args...>(std::move(name), scope, fn));
            return *this;
        }

        ClassBuilder& delegate(std::string name, Scope scope, std::type_index senderType,
                                    std::vector<Argument> arguments,
                                    Delegate::AddressFn address, Delegate::InvokeFn invoke) {
            class_->delegates_.push_back(new Delegate(std::move(name), scope, senderType, std::move(arguments), address, invoke));
            return *this;
        }

        template<typename... Args>
        ClassBuilder& delegate(std::string name, Scope scope, newui::Delegate<T, Args...> T::* member) {
            class_->delegates_.push_back(new TypedDelegate<T, Args...>(std::move(name), scope, member));
            return *this;
        }

        // Only ever call this for a genuinely public constructor - see
        // Class::createInstance()'s comment. A private/protected constructor
        // simply gets no addConstructor()/addTypedConstructor() call at all,
        // not one with invoke == nullptr (unlike addMethod(), there's no
        // metadata-only Constructor entry - a class's constructor list is
        // only ever "things you can actually call").
        ClassBuilder& constructor(std::vector<Argument> arguments, Constructor::InvokeFn invoke) {
            class_->constructors_.push_back(new Constructor(std::move(arguments), invoke));
            return *this;
        }

        // Args... must name a real SourceT constructor - see
        // TypedConstructor's comment.
        template<typename... Args>
        ClassBuilder& constructor() {
            class_->constructors_.push_back(new TypedConstructor<T, Args...>());
            return *this;
        }

        // Heap-allocates once, transferring ownership to whatever calls
        // ReflectionRegistry::registerClass(build()) with the result.
        Class* build() { return new TypedClass<T>(std::move(*class_)); }

        operator Class*  () {
            return new TypedClass<T>(std::move(*class_));
        }

    private:
        std::unique_ptr< TypedClass<T> > class_;
    };

    class EnumBuilder {
    public:
        EnumBuilder(std::type_index type, std::string name) : enum_(type, std::move(name)) {}

        EnumBuilder& addValue(std::string name, std::int64_t value) {
            enum_.values_.push_back(EnumValue{std::move(name), value});
            return *this;
        }

        Enum build() { return std::move(enum_); }

    private:
        Enum enum_;
    };

    // Process-wide registry of every reflected Class/Enum, keyed by
    // std::type_index (and, secondarily, by name for lookups that only have
    // a string - e.g. a save/load format or a script binding). Not coupled
    // to UIComponent - reflection works on any class, not just the
    // View/ViewStyle/Layout hierarchy SerializationRegistry (serialization.h)
    // covers - same Meyer's-singleton shape as that registry otherwise.
    //
    // Owns registered Class instances via a raw, registry-owned Class* -
    // Class::build() (via ClassBuilder<T>) always hands back a
    // heap-allocated TypedClass<T> sliced to its Class* base, and Class
    // being polymorphic (virtual ~Class()) is what makes deleting through
    // that base pointer safe. Raw pointer + manual delete on replace/
    // teardown, not std::unique_ptr, to match PropertyManager's existing
    // ownership convention for a polymorphic registry in property.h.
    // registerEnum()/Enum stay by-value (no TypedEnum, no polymorphism
    // needed there) - only Class needed this.
    class ReflectionRegistry {
    public:
        ReflectionRegistry(const ReflectionRegistry&) = delete;
        ReflectionRegistry& operator=(const ReflectionRegistry&) = delete;

        // Takes ownership - classInfo must be heap-allocated (ClassBuilder<T>::
        // build()'s result, almost always passed straight through). Replacing
        // an already-registered type deletes the old entry first, same as
        // PropertyManager::registerProperty()'s replace behavior.
        static void registerClass(Class* classInfo);
        static void registerEnum(Enum enumInfo);

        static const Class* getClass(std::type_index type);
        static const Class* getClass(const std::string& name);

        static const Enum* getEnum(std::type_index type);
        static const Enum* getEnum(const std::string& name);

    private:
        ReflectionRegistry() = default;
        ~ReflectionRegistry();

        static ReflectionRegistry& instance();

        std::unordered_map<std::type_index, Class*> classesByType_;
        std::unordered_map<std::string, std::type_index> classNameToType_;

        std::unordered_map<std::type_index, Enum> enumsByType_;
        std::unordered_map<std::string, std::type_index> enumNameToType_;
    };

    inline const Class* classinfo(const std::string& name) {
        return ReflectionRegistry::getClass(name);
    }

    // The T-aware counterpart to classinfo(name) above - returns the
    // TypedClass<T> registered for T (nullptr if none was ever registered,
    // or - in principle - if T was registered as a plain Class rather than
    // through ClassBuilder<T>, though that shouldn't happen in practice
    // since ClassBuilder<T> is the only thing that ever builds a Class).
    template<typename T>
    const TypedClass<T>* classinfo() {
        std::string classname = demangleTypeName(typeid(T));
        const Class* found = ReflectionRegistry::getClass(classname);
        return dynamic_cast<const TypedClass<T>*>(found);
    }

    
    inline const Class* classinfo(std::type_index type) {        
        const Class* found = ReflectionRegistry::getClass(type);
        return found;
    }

}
