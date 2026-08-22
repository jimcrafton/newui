#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <newui/delegate.h>
#include <newui/utils.h>

namespace newui::reflection {


    class Class;
    class Property;



    class ClassWriter {
    public:
        virtual ~ClassWriter() {}

        virtual void beginObject(const std::string& name, const Class* clazz) {};
        virtual void endObject(const std::string& name, const Class* clazz) {};

        virtual void writeInt8(const std::string& propertyName, std::uint8_t value, bool signedVal) {};
        virtual void writeInt16(const std::string& propertyName, std::uint16_t value, bool signedVal) {};
        virtual void writeInt32(const std::string& propertyName, std::uint32_t value, bool signedVal) {};
        virtual void writeInt64(const std::string& propertyName, std::uint64_t value, bool signedVal) {};
        virtual void writeString(const std::string& propertyName, const std::string& value) {};
        virtual void writeFloat(const std::string& propertyName, float value) {};
        virtual void writeDouble(const std::string& propertyName, double value) {};
        virtual void writeBool(const std::string& propertyName, bool value) {};

        virtual void beginCollection(const std::string& propertyName) {};
        virtual void beginElement(const std::any& index, const std::any& key) {};
        virtual void endElement(const std::any& index, const std::any& key) {};
        virtual void endCollection(const std::string& propertyName) {};
    };


    class ClassReader {
    public:
        virtual ~ClassReader() {}

        virtual const Class* beginObject(const std::string& name)  = 0;
        virtual void endObject(const std::string& name, const Class* clazz) = 0;


        virtual void readInt(const std::string& propertyName, std::int32_t& val) = 0;
        virtual void readString(const std::string& propertyName, std::string& val) = 0;
        virtual void readFloat(const std::string& propertyName, float& val) = 0;
        virtual void readDouble(const std::string& propertyName, double& val) = 0;
        virtual void readBool(const std::string& propertyName, bool& val) = 0;

        // Returns how many elements the *source* data actually has under
        // propertyName - unlike ClassWriter::beginCollection() (void; the
        // writer already knows the live count from the object it's
        // serializing), a reader has nothing to iterate by default. See
        // PropertyCollection::read()'s own comment for why looping by this
        // count (not by whatever the target container's current size
        // happens to be) is what makes growing the container on read
        // actually work.
        virtual std::size_t beginCollection(const std::string& propertyName) = 0;
        virtual void beginElement(const std::any& index, const std::any& key) = 0;
        virtual void endElement(const std::any& index, const std::any& key) = 0;
        virtual void endCollection(const std::string& propertyName) = 0;
    };
    // Forward-declared, never given a generic body - referencing
    // ClassAccess<T> for a T nobody ever explicitly specialized is a hard
    // "incomplete type" compile error, not silent UB. NEWUI_REFLECT_PRIVATE()
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

    // container_traits<ContainerT> - one specialization per STL container
    // type reflectable as a Property "collection" (see PropertyCollection/
    // TypedPropertyCollection below). The primary template is deliberately
    // empty (no ElementT member) rather than declared-only: looking up
    // container_traits<T>::ElementT for an unsupported T then fails inside
    // is_reflectable_collection's own immediate context (SFINAE-friendly),
    // instead of erroring while implicitly instantiating an incomplete
    // primary template body.
    namespace detail {
        template<typename ContainerT>
        struct container_traits {};

        template<typename T, typename Alloc>
        struct container_traits<std::vector<T, Alloc>> {
            using ElementT = T;
            using KeyT = std::size_t;
            static constexpr bool associative = false;
            static std::size_t count(const std::vector<T, Alloc>& c) { return c.size(); }
            static T getByIndex(const std::vector<T, Alloc>& c, std::size_t index) { return c.at(index); }
            static void setByIndex(std::vector<T, Alloc>& c, std::size_t index, const T& value) { c.at(index) = value; }
            static T getByKey(const std::vector<T, Alloc>& c, std::size_t key) { return getByIndex(c, key); }
            static void setByKey(std::vector<T, Alloc>& c, std::size_t key, const T& value) { setByIndex(c, key, value); }
            // Growth, for TypedPropertyCollection::readItem() (reflection.h)
            // to append a freshly-read element past the container's current
            // size - see container_can_add_v below for why this is optional
            // (std::array can't grow; std::map would need a key, not just a
            // value, to "add" anything - only std::vector gets this today).
            static void add(std::vector<T, Alloc>& c, const T& value) { c.push_back(value); }
        };

        template<typename T, std::size_t N>
        struct container_traits<std::array<T, N>> {
            using ElementT = T;
            using KeyT = std::size_t;
            static constexpr bool associative = false;
            static std::size_t count(const std::array<T, N>&) { return N; }
            static T getByIndex(const std::array<T, N>& c, std::size_t index) { return c.at(index); }
            static void setByIndex(std::array<T, N>& c, std::size_t index, const T& value) { c.at(index) = value; }
            static T getByKey(const std::array<T, N>& c, std::size_t key) { return getByIndex(c, key); }
            static void setByKey(std::array<T, N>& c, std::size_t key, const T& value) { setByIndex(c, key, value); }
        };

        // Positional (index-based) access enumerates in key order via
        // std::advance() on an iterator - only stable as long as the map
        // isn't mutated between calls, same caveat as any iterator-derived
        // index would have.
        template<typename K, typename V, typename Compare, typename Alloc>
        struct container_traits<std::map<K, V, Compare, Alloc>> {
            using ElementT = V;
            using KeyT = K;
            static constexpr bool associative = true;
            static std::size_t count(const std::map<K, V, Compare, Alloc>& c) { return c.size(); }
            static V getByIndex(const std::map<K, V, Compare, Alloc>& c, std::size_t index) {
                auto it = c.begin();
                std::advance(it, index);
                return it->second;
            }
            static void setByIndex(std::map<K, V, Compare, Alloc>& c, std::size_t index, const V& value) {
                auto it = c.begin();
                std::advance(it, index);
                it->second = value;
            }
            static V getByKey(const std::map<K, V, Compare, Alloc>& c, const K& key) { return c.at(key); }
            static void setByKey(std::map<K, V, Compare, Alloc>& c, const K& key, const V& value) { c[key] = value; }
        };

        template<typename T, typename = void>
        struct is_reflectable_collection : std::false_type {};
        template<typename T>
        struct is_reflectable_collection<T, std::void_t<typename container_traits<T>::ElementT>> : std::true_type {};
        template<typename T>
        inline constexpr bool is_reflectable_collection_v = is_reflectable_collection<T>::value;

        // Detects whether container_traits<ContainerT>::add(ContainerT&,
        // const ElementT&) exists (see std::vector's specialization above) -
        // SFINAE, not a flag on container_traits itself, so std::array/
        // std::map (no add() defined) keep compiling fine even though
        // TypedPropertyCollection::readItem() unconditionally *mentions*
        // Traits::add in its body; the `if constexpr` there only actually
        // instantiates that call for a ContainerT this resolves true for.
        template<typename ContainerT, typename ElementT, typename = void>
        struct container_can_add : std::false_type {};
        template<typename ContainerT, typename ElementT>
        struct container_can_add<ContainerT, ElementT,
            std::void_t<decltype(container_traits<ContainerT>::add(std::declval<ContainerT&>(), std::declval<const ElementT&>()))>>
            : std::true_type {};
        template<typename ContainerT, typename ElementT>
        inline constexpr bool container_can_add_v = container_can_add<ContainerT, ElementT>::value;
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
#define NEWUI_REFLECT_PRIVATE() \
    template<typename NewuiReflectT_> friend struct newui::reflection::detail::ClassAccess

    enum class Scope {
        Public,
        Protected,
        Private,
    };

    // Property::flags() - Collection marks a Property that also implements
    // PropertyCollection (see below); Associative only means anything
    // combined with Collection (a std::map, keyed by something other than a
    // plain index, vs. a std::vector/std::array keyed by index). Static
    // marks a Field backed by a fixed ValueT* (TypedField) rather than a
    // pointer-to-data-member needing a real instance (TypedMemberField/
    // TypedFieldCollection) - see Field::isStatic(). Meaningless on a
    // Property (every TypedProperty backing mode needs an instance one way
    // or another - a getter/setter pair included, even a pair that happens
    // to wrap a static accessor is still called *through* an instance
    // pointer here) - never set there.
    enum class PropertyFlags : std::uint32_t {
        None        = 0,
        Collection  = 1u << 0,
        Associative = 1u << 1,
        CreatedOnHeap = 1u << 2,
        Static      = 1u << 3,
    };

    inline PropertyFlags operator|(PropertyFlags lhs, PropertyFlags rhs) {
        return static_cast<PropertyFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }
    inline PropertyFlags operator&(PropertyFlags lhs, PropertyFlags rhs) {
        return static_cast<PropertyFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
    }
    inline PropertyFlags& operator|=(PropertyFlags& lhs, PropertyFlags rhs) { return lhs = lhs | rhs; }

    // Disambiguates a bare &Class::method when method is overloaded (most
    // commonly a const/non-const pair, e.g. `ViewStyle& View::style()` vs.
    // `const ViewStyle& View::style() const`) so it can still be passed
    // directly to ClassBuilder<T>::property()'s getter/setter overload,
    // instead of needing a wrapping lambda. `&Class::method` on its own is
    // ambiguous there - overload resolution for taking a member function's
    // address needs a target type to pick a candidate against, and
    // property()'s GetterT/SetterT are deduced template parameters, not a
    // fixed type, so there's nothing for the compiler to resolve against
    // at the point of '&'. Explicitly naming Signature here supplies
    // exactly that missing target type - same idiom as Qt's qOverload<>()
    // or RTTR's select_overload<>() (rttr.org), just scoped to this
    // header. Usage: selectOverload<ViewStyle&(View::*)()>(&View::style).
    template<typename Signature>
    constexpr Signature selectOverload(Signature method) {
        return method;
    }

    // One parameter of a Method/Delegate/Constructor. name is best-effort
    // (may be empty - not every call site has one available) and never
    // affects invoke(); only position within the argument list and type do.
    //@reflect ignore=true
    struct Argument {
        std::string name;
        std::type_index type;
    };

    // A member variable reached via direct storage access - a real
    // pointer-to-data-member for an ordinary (non-static) member
    // (TypedMemberField/TypedFieldCollection, below) or a fixed ValueT* for
    // a static class variable (TypedField, below) - never through a
    // getter/setter method. That's the whole distinction from Property: a
    // member with real accessor methods is a Property (TypedProperty's
    // RefGetter/PtrGetter/Getter+Setter modes); a plain member variable
    // with no accessors at all - static or not - is a Field. scope()
    // records the real C++ access level as metadata (see
    // NEWUI_REFLECT_PRIVATE()'s friend declaration for how a private one
    // gets reached at all) - it doesn't gate address()/get()/set().
    //
    // instance is meaningless for a static field (TypedField's own
    // overrides ignore it - the address is already fixed at registration
    // time) but required (a real, non-null SourceT*) for an instance field
    // (TypedMemberField/TypedFieldCollection) - one instance parameter
    // threaded through every backing mode uniformly, same shape Property's
    // own API already has, even where a particular mode doesn't need it.
    //
    // flags()/isCollection()/isAssociative() reuse PropertyFlags (a
    // generic "collection/associative/heap-owned" vocabulary, not actually
    // Property-specific despite the name) - FieldCollection/
    // TypedFieldCollection (below) are the Field-side counterpart to
    // PropertyCollection/TypedPropertyCollection, same reasoning either way.
    //@reflect ignore=true
    class Field {
    public:
        Field(std::string name, std::type_index type, Scope scope, void* address,
               std::any (*get)(), void (*set)(const std::any&),
               PropertyFlags flags = PropertyFlags::None)
            : name_(std::move(name)), type_(type), scope_(scope), flags_(flags),
              address_(address), get_(get), set_(set) {}

        virtual ~Field() = default;

        const std::string& name() const { return name_; }
        std::type_index type() const { return type_; }
        Scope scope() const { return scope_; }
        PropertyFlags flags() const { return flags_; }
        bool isCollection() const { return (flags_ & PropertyFlags::Collection) != PropertyFlags::None; }
        bool isAssociative() const { return (flags_ & PropertyFlags::Associative) != PropertyFlags::None; }
        // true for TypedField (a fixed ValueT*, no instance needed - see
        // PropertyFlags::Static's own comment), false for TypedMemberField/
        // TypedFieldCollection (a real pointer-to-data-member, needs one).
        bool isStatic() const { return (flags_ & PropertyFlags::Static) != PropertyFlags::None; }

        virtual void* address(void* instance) const { return address_; }
        virtual std::any get(void* instance) const { return get_ ? get_() : std::any(); }
        virtual void set(void* instance, const std::any& value) const { if (set_) set_(value); }

        // Convenience for a caller that already knows T at compile time -
        // just std::any_cast<T> on top of get(), still throws
        // std::bad_any_cast on a mismatch rather than silently misreading.
        template<typename T>
        T getAs(void* instance) const {
            return std::any_cast<T>(get(instance));
        }

        // Base defaults are no-ops (same "every method has a do-nothing
        // default" idiom Property/PropertyCollection already use) -
        // TypedMemberField below is the one that actually does something;
        // TypedField (a static class variable) and FieldCollection stay
        // no-op for now, out of scope for this pass.
        virtual void write(void* instancePtr, ClassWriter* writer) const {}
        virtual void read(void* instancePtr, ClassReader* reader) const {}
    private:
        template<typename T> friend class ClassBuilder;

        using GetFn = std::any (*)();
        using SetFn = void (*)(const std::any&);

        std::string name_;
        std::type_index type_;
        Scope scope_;
        PropertyFlags flags_;
        void* address_;
        GetFn get_;
        SetFn set_;
    };

    // T-aware TypedField<ValueT> - a static class variable. The address is
    // known and fixed at registration time (no instance involved), so
    // there's no accessor-function indirection at all: just a real
    // ValueT* stored directly. Adds no std::any-thunk state of its own;
    // address()/get()/set() are overridden outright, and their instance
    // parameter (kept only for uniformity with Field's other backing
    // modes) is ignored.
    //@reflect ignore=true
    template<typename ValueT>
    class TypedField : public Field {
    public:
        TypedField(std::string name, Scope scope, ValueT* address)
            : Field(std::move(name), typeid(ValueT), scope, nullptr, nullptr, nullptr, PropertyFlags::Static),
              address_(address) {}

        void* address(void* /*instance*/) const override { return address_; }
        std::any get(void* /*instance*/) const override { return std::any(*address_); }
        void set(void* /*instance*/, const std::any& value) const override { *address_ = std::any_cast<ValueT>(value); }

    private:
        ValueT* address_;
    };

    // T-aware TypedMemberField<SourceT,ValueT> - an ordinary (non-static)
    // member variable, reached via a real pointer-to-data-member
    // (ValueT SourceT::*) - the direct-storage counterpart to
    // TypedProperty's MemberPtr mode, minus the RefGetter/PtrGetter/
    // Getter+Setter accessor-method modes Property has beyond that (see
    // Field's own "raw access, never through a method" comment above).
    // instance must be a real, non-null SourceT*.
    //@reflect ignore=true
    template<typename SourceT, typename ValueT>
    class TypedMemberField : public Field {
    public:
        using MemberPtr = ValueT SourceT::*;

        TypedMemberField(std::string name, Scope scope, MemberPtr member)
            : Field(std::move(name), typeid(ValueT), scope, nullptr, nullptr, nullptr), member_(member) {}

        void* address(void* instance) const override {
            return &(static_cast<SourceT*>(instance)->*member_);
        }
        std::any get(void* instance) const override {
            return std::any(static_cast<SourceT*>(instance)->*member_);
        }
        void set(void* instance, const std::any& value) const override {
            static_cast<SourceT*>(instance)->*member_ = std::any_cast<ValueT>(value);
        }

        // Real write()/read() (unlike Field's own no-op defaults) - a
        // TypedMemberField only ever has the one backing mode (a real
        // pointer-to-data-member, always addressable), so this is simpler
        // than TypedProperty's equivalent (no RefGetter/PtrGetter/Getter+
        // Setter cases to also handle): nested-Class recursion when
        // ValueT is itself reflected (e.g. a plain-public-field Point
        // nested inside a reflected class), Property::writeValue()/
        // readValue()'s scalar path otherwise - same two-way split
        // TypedProperty::write()/read() already use for the same reason.
        void write(void* instancePtr, ClassWriter* writer) const override {
            if (const Class* nestedClazz = classinfo(typeid(ValueT)); nestedClazz != nullptr) {
                nestedClazz->write(address(instancePtr), writer, name());
                return;
            }
            Property::writeValue(name(), typeid(ValueT), get(instancePtr), instancePtr, writer);
        }

        void read(void* instancePtr, ClassReader* reader) const override {
            if (const Class* nestedClazz = classinfo(typeid(ValueT)); nestedClazz != nullptr) {
                std::any existing(static_cast<ValueT*>(address(instancePtr)));
                bool onHeap = false;
                nestedClazz->read(reader, name(), existing, onHeap);
                return;
            }
            std::any val;
            Property::readValue(name(), typeid(ValueT), val, instancePtr, reader);
            set(instancePtr, val);
        }

    private:
        MemberPtr member_;
    };

    // A Field whose value is itself a collection (std::vector/std::array/
    // std::map - see container_traits above) - the direct-member-access
    // counterpart to PropertyCollection (see its own comment for why
    // element-level access matters beyond the whole-member address()/
    // get()/set()). Concrete, same "every method has a do-nothing
    // default, TypedFieldCollection overrides all of it" idiom
    // PropertyCollection/TypedPropertyCollection already use.
    //@reflect ignore=true
    class FieldCollection : public Field {
    public:
        using Field::get;
        using Field::set;

        FieldCollection(std::string name, std::type_index type, Scope scope,
                          PropertyFlags flags = PropertyFlags::Collection)
            : Field(std::move(name), type, scope, nullptr, nullptr, nullptr, flags) {}

        virtual std::type_index elementType() const { return typeid(void); }
        virtual std::type_index keyType() const { return typeid(void); }

        virtual std::size_t count(std::any& instance) const { return 0; }
        virtual std::any get(std::any& instance, std::size_t index) const { return std::any(); }
        virtual void set(std::any& instance, std::size_t index, const std::any& value) const {}
        virtual std::any get(std::any& instance, const std::any& key) const { return std::any(); }
        virtual void set(std::any& instance, const std::any& key, const std::any& value) const {}
    };

    // T-aware TypedFieldCollection<SourceT,ContainerT> - always a real
    // pointer-to-data-member (ContainerT SourceT::*); unlike
    // TypedPropertyCollection there's only ever this one backing mode
    // (see Field's own "raw access, never through a method" distinction
    // from Property), so it's always addressable and get()/set() always
    // reach the real, live member - never a disconnected copy the way a
    // getter-backed TypedPropertyCollection's get() is.
    //@reflect ignore=true
    template<typename SourceT, typename ContainerT>
    class TypedFieldCollection : public FieldCollection {
    public:
        using Traits = detail::container_traits<ContainerT>;
        using ElementT = typename Traits::ElementT;
        using KeyT = typename Traits::KeyT;
        using MemberPtr = ContainerT SourceT::*;

        TypedFieldCollection(std::string name, Scope scope, MemberPtr member)
            : FieldCollection(std::move(name), typeid(ContainerT), scope, flagsFor()), member_(member) {}

        void* address(void* instance) const override {
            return &(static_cast<SourceT*>(instance)->*member_);
        }
        std::any get(void* instance) const override {
            return std::any(static_cast<SourceT*>(instance)->*member_);
        }
        void set(void* instance, const std::any& value) const override {
            static_cast<SourceT*>(instance)->*member_ = std::any_cast<ContainerT>(value);
        }

        std::type_index elementType() const override { return typeid(ElementT); }
        std::type_index keyType() const override { return typeid(KeyT); }

        std::size_t count(std::any& instance) const override { return Traits::count(unbox(instance)); }
        std::any get(std::any& instance, std::size_t index) const override {
            return std::any(Traits::getByIndex(unbox(instance), index));
        }
        void set(std::any& instance, std::size_t index, const std::any& value) const override {
            Traits::setByIndex(unbox(instance), index, std::any_cast<ElementT>(value));
        }
        std::any get(std::any& instance, const std::any& key) const override {
            return std::any(Traits::getByKey(unbox(instance), std::any_cast<KeyT>(key)));
        }
        void set(std::any& instance, const std::any& key, const std::any& value) const override {
            Traits::setByKey(unbox(instance), std::any_cast<KeyT>(key), std::any_cast<ElementT>(value));
        }

    private:
        static PropertyFlags flagsFor() {
            return Traits::associative ? (PropertyFlags::Collection | PropertyFlags::Associative) : PropertyFlags::Collection;
        }
        static ContainerT& unbox(std::any& instance) { return std::any_cast<ContainerT&>(instance); }

        MemberPtr member_;
    };

    // A member variable, reflected regardless of its real C++ access level
    // (see NEWUI_REFLECT_PRIVATE()'s friend declaration) - scope() records the
    // real access level as metadata, it doesn't gate whether address()/get()/
    // set() work. address() is the zero-copy live pointer into instance;
    // get()/set() are a boxed convenience layer on top for callers that only
    // have a name string and no compile-time type to cast address() with.
    //
    // Abstract - there is no way to build a bare Property standing on its
    // own. Every real property crossing the reflection boundary is a
    // TypedProperty<SourceT,ValueT>/TypedPropertyCollection<SourceT,
    // ContainerT> (below): address()/get()/set() used to be backed here by
    // raw void*(*)(void*)/std::any(*)(void*)/void(*)(void*,const std::any&)
    // function pointers, which meant a private/protected member's untyped
    // ClassBuilder::property() thunk had to hand-cast void* to SourceT*
    // itself with nothing checking that cast was even to the right type.
    // That storage/thunk machinery has moved down into TypedProperty as a
    // real std::function<ValueT(SourceT&)>-shaped getter/setter (see its
    // own comment) - the SourceT cast happens exactly once, inside
    // TypedProperty's own address()/get()/set(), never anywhere else.
    //@reflect ignore=true
    class Property {
    public:
        Property(std::string name, std::type_index type, Scope scope, PropertyFlags flags = PropertyFlags::None)
            : name_(std::move(name)), type_(type), scope_(scope), flags_(flags) {}

        virtual ~Property() = default;

        const std::string& name() const { return name_; }
        std::type_index type() const { return type_; }
        Scope scope() const { return scope_; }
        PropertyFlags flags() const { return flags_; }
        bool isCollection() const { return (flags_ & PropertyFlags::Collection) != PropertyFlags::None; }
        bool isAssociative() const { return (flags_ & PropertyFlags::Associative) != PropertyFlags::None; }
        bool shouldCreateOnHeap() const { return (flags_ & PropertyFlags::CreatedOnHeap) != PropertyFlags::None; }
        

        virtual void* address(void* instance) const = 0;
        virtual std::any get(void* instance) const = 0;
        virtual void set(void* instance, const std::any& value) const = 0;

        // false for a by-value getter/setter property - the only case
        // where address() throws rather than returning something real
        // (see TypedProperty/TypedPropertyCollection's own address()
        // comments). write() (below) checks this before ever calling
        // address(), rather than assuming any property whose type()
        // happens to resolve to a registered Class must be addressable -
        // a get-only Rect-typed property (e.g. View::bounds(), returning
        // const Rect&) is exactly the case where that assumption breaks:
        // Rect can still be a registered Class (for Writer-based nested
        // serialization) without this particular property ever having a
        // live Rect& to hand out.
        virtual bool isAddressable() const { return true; }

        // Convenience for a caller that already knows T at compile time -
        // just std::any_cast<T> on top of get(), still throws
        // std::bad_any_cast on a mismatch rather than silently misreading.
        template<typename T>
        T getAs(void* instance) const {
            return std::any_cast<T>(get(instance));
        }

        virtual void write(void* instancePtr, ClassWriter* writer) const = 0;

        virtual void read(void* instancePtr, ClassReader* reader) const = 0;

        static void writeValue(const Property* property, const std::any& val, void* instancePtr, ClassWriter* writer);

        static void writeValue(const std::string& valName, const std::type_index& valType, const std::any& val, void* instancePtr, ClassWriter* writer);
        static void readValue(const std::string& valName, const std::type_index& valType, std::any& val, void* instancePtr, ClassReader* reader);

    protected:
        std::string name_;
        std::type_index type_;
        Scope scope_;
        PropertyFlags flags_;
    };

    // T-aware TypedProperty<SourceT,ValueT> - the one place a void* instance
    // ever gets cast to SourceT*, for any of its four backing modes:
    //
    //   - MemberPtr (ValueT SourceT::*): a real pointer-to-data-member.
    //     instance->*member_ is valid C++ for any access level as long as
    //     *evaluating* &SourceT::field_ was legal at the point this
    //     pointer-to-member value was obtained - for a private field that
    //     means going through a detail::ClassAccess<T> specialization (see
    //     reflection.h's top comment), just handing back a pointer-to-
    //     member instead of a thunk function. Addressable.
    //   - RefGetter (std::function<ValueT&(SourceT&)>): a getter that hands
    //     back a *live* ValueT&, e.g. `ViewStyle& View::style()` - for a
    //     nested sub-object reachable only through an accessor method, not
    //     a raw pointer-to-member (a protected/private member with no
    //     NEWUI_REFLECT_PRIVATE() on that class, or one that's genuinely
    //     computed-but-stable). Still addressable, same as MemberPtr -
    //     address() just hands back &refGetter_(*self) instead of
    //     &(self->*member_).
    //   - PtrGetter (std::function<ValueT*(SourceT&)>): a getter that hands
    //     back a possibly-null ValueT*, e.g. `Frame* Application::getFrame()`
    //     - for an *optional* nested sub-object (unlike RefGetter, which
    //     assumes the sub-object always exists). Addressable (the pointer
    //     itself, which may be nullptr - callers already have to check
    //     address()'s result for null the same way they'd check the real
    //     pointer); get() is an empty std::any when null rather than
    //     dereferencing it.
    //   - Getter/Setter (std::function<ValueT(SourceT&)>/std::function<void
    //     (SourceT&,const ValueT&)>): a plain by-value accessor pair, e.g.
    //     `std::string View::name()`/`void View::setName(const string&)`,
    //     or a getter with no matching setter at all (read-only). Not
    //     addressable - there's no live ValueT to take the address of, only
    //     ever a fresh copy - address() throws rather than returning a
    //     dangling pointer to a temporary.
    //
    // Exactly one of these four is ever populated per instance - see
    // ClassBuilder<T>::property()'s getter/setter overload, which picks
    // the right one from the getter's own return type and is the only
    // thing that ever constructs one of these (besides the plain
    // pointer-to-data-member overload, for MemberPtr).
    //@reflect ignore=true
    template<typename SourceT, typename ValueT>
    class TypedProperty : public Property {
    public:
        using MemberPtr = ValueT SourceT::*;
        using RefGetter = std::function<ValueT&(SourceT&)>;
        using PtrGetter = std::function<ValueT*(SourceT&)>;
        // Distinct from Setter (below) on purpose: a real setter method
        // for a PtrGetter property - e.g. `void Application::setFrame
        // (Frame*)` - reassigns *which* object is pointed at (ownership-
        // transfer style, takes a ValueT*), not "copy this ValueT into
        // the object currently pointed at" the way Setter's `const
        // ValueT&` does. set() picks whichever of the two this property
        // actually has - see its own comment.
        using PtrSetter = std::function<void(SourceT&, ValueT*)>;
        using Getter = std::function<ValueT(SourceT&)>;
        using Setter = std::function<void(SourceT&, const ValueT&)>;

        TypedProperty(std::string name, Scope scope, MemberPtr member)
            : Property(std::move(name), typeid(ValueT), scope), member_(member) {
            if constexpr (std::is_pointer_v<ValueT>) {
                this->flags_ |= PropertyFlags::CreatedOnHeap;
            }
        }

        TypedProperty(std::string name, Scope scope, RefGetter refGetter)
            : Property(std::move(name), typeid(ValueT), scope), refGetter_(std::move(refGetter)) {}

        // A PtrGetter property is exactly as pointer/ownership-shaped as
        // the plain-MemberPtr constructor's `is_pointer_v<ValueT>` case
        // above (e.g. `Frame* Application::getFrame()`) - CreatedOnHeap
        // needs to be set here too, not just there, for read() (below) to
        // know this always needs a fresh heap instance rather than an
        // in-place edit. See isAddressable()'s own comment for why this is
        // still "addressable" even when the flag says heap - the two
        // answer different questions (is there live storage to inspect
        // right now vs. how should a *fresh* one be built on read).
        TypedProperty(std::string name, Scope scope, PtrGetter ptrGetter, PtrSetter ptrSetter = nullptr)
            : Property(std::move(name), typeid(ValueT), scope), ptrGetter_(std::move(ptrGetter)), ptrSetter_(std::move(ptrSetter)) {
            this->flags_ |= PropertyFlags::CreatedOnHeap;
        }

        TypedProperty(std::string name, Scope scope, Getter getter, Setter setter = nullptr)
            : Property(std::move(name), typeid(ValueT), scope), getter_(std::move(getter)), setter_(std::move(setter)) {}

        virtual ~TypedProperty() = default;

        bool isAddressable() const override {
            return member_ != nullptr || bool(refGetter_) || bool(ptrGetter_);
        }

        void* address(void* instance) const override {
            SourceT* self = static_cast<SourceT*>(instance);
            if (member_ != nullptr) {
                return &(self->*member_);
            }
            if (refGetter_) {
                return &refGetter_(*self);
            }
            if (ptrGetter_) {
                return static_cast<void*>(ptrGetter_(*self));
            }
            throw std::logic_error("TypedProperty::address(): '" + name() +
                "' is a by-value getter/setter property, not addressable");
        }
        // get()/set() box/unbox a *copy* of ValueT in every mode except
        // PtrSetter (see set()'s own comment) - so both need
        // std::is_copy_constructible_v<ValueT> (get())/is_copy_assignable_v
        // (set()) compile-time guards around the actual std::any(...)/
        // std::any_cast<ValueT>(...) calls. This matters for exactly the
        // properties RefGetter/PtrGetter exist for in the first place - a
        // nested sub-object like RootView or View, which (thanks to its
        // own unique_ptr<ViewStyle> member) has no copy constructor/
        // assignment at all. Without the guard, a *runtime* `if` still
        // requires *every* branch to type-check for whatever ValueT this
        // TypedProperty was instantiated with, even the branches that
        // could never execute for a given instance's chosen mode -
        // address() has no such problem (no copying involved), which is
        // exactly why RefGetter/PtrGetter properties stay fully usable
        // through address() even when get()/set() have nothing to offer.
        std::any get(void* instance) const override {
            SourceT* self = static_cast<SourceT*>(instance);
            if constexpr (std::is_copy_constructible_v<ValueT>) {
                if (member_ != nullptr) {
                    return std::any(self->*member_);
                }
                if (refGetter_) {
                    return std::any(refGetter_(*self));
                }
                if (ptrGetter_) {
                    ValueT* p = ptrGetter_(*self);
                    return p != nullptr ? std::any(*p) : std::any();
                }
                return getter_ ? std::any(getter_(*self)) : std::any();
            } else {
                return std::any();
            }
        }
        // Only ever calls an explicitly-registered setter - member_ (a raw
        // pointer-to-data-member, the whole point of registering one that
        // way in the first place - see reflection.h's top comment) or a
        // real setter_/ptrSetter_ callable someone actually passed to
        // ClassBuilder::property(). Deliberately does NOT improvise a
        // "set" out of a bare getter by assigning through whatever
        // reference/pointer it returns (refGetter_(*self) = value, or
        // *ptrGetter_(*self) = value) - a getter-only accessor method (no
        // setter given) might exist specifically *because* the real class
        // has invariants a raw assignment would skip (e.g. View::setStyle()
        // also fixes up style_->setView() linkage - a plain
        // `view.style() = newStyle` would silently skip that). Getter-only
        // is read-only, full stop; nothing here ever reaches into an
        // object's internals beyond what an explicit setter call does.
        void set(void* instance, const std::any& value) const override {
            SourceT* self = static_cast<SourceT*>(instance);
            if (member_ != nullptr) {
                if constexpr (std::is_copy_constructible_v<ValueT> && std::is_copy_assignable_v<ValueT>) {
                    self->*member_ = std::any_cast<ValueT>(value);
                }
                return;
            }
            if (ptrSetter_) {
                // The one mode that never copies a ValueT at all - value
                // holds a ValueT* (the new pointer target, e.g. a just-
                // createInstance()'d object), not a ValueT to copy into
                // whatever's already pointed at. That's exactly what
                // makes this work with zero extra unboxing on a future
                // Reader's part: Class::createInstance() already returns
                // a std::any holding precisely ValueT* (whatever
                // TypedConstructor<ValueT,...>::invoke()'s own
                // `new ValueT(...)` produced) - passed straight through.
                ptrSetter_(*self, std::any_cast<ValueT*>(value));
                return;
            }
            if (setter_) {
                if constexpr (std::is_copy_constructible_v<ValueT>) {
                    setter_(*self, std::any_cast<ValueT>(value));
                }
                return;
            }
            // refGetter_/ptrGetter_ with no matching setter, or a plain
            // getter_ with none: read-only, silently a no-op - same
            // contract Property::set() has always had for a property with
            // no setter.
        }

        // Same idea as Property::getAs<T>(), but no std::any round-trip at
        // all - a live reference straight through whichever addressable
        // backing this property has. Throws the same as address() does for
        // a by-value getter/setter property (and, for PtrGetter, if the
        // pointer is currently null).
        ValueT& getTyped(SourceT& instance) const {
            if (member_ != nullptr) {
                return instance.*member_;
            }
            if (refGetter_) {
                return refGetter_(instance);
            }
            if (ptrGetter_) {
                if (ValueT* p = ptrGetter_(instance); p != nullptr) {
                    return *p;
                }
                throw std::logic_error("TypedProperty::getTyped(): '" + name() + "' is currently null");
            }
            throw std::logic_error("TypedProperty::getTyped(): '" + name() +
                "' is a by-value getter/setter property, not addressable");
        }
        void setTyped(SourceT& instance, const ValueT& value) const {
            set(&instance, std::any(value));
        }

        // When type() resolves to another registered Class, this recurses
        // directly (bypassing writeValue()'s own nested-Class branch,
        // which assumes whatever instancePtr it's handed is already live
        // and non-null - collection elements satisfy that trivially, but
        // a plain property doesn't always) rather than assuming
        // isAddressable() must be true just because the type happens to
        // also be reflected (e.g. Rect, registered so a Writer can
        // recurse into "bounds", even though View::bounds() itself is a
        // get-only, non-addressable property - see isAddressable()'s own
        // comment). Not addressable but still copy-constructible: writes
        // a throwaway local copy's own fields instead - there's a real
        // value, just no live storage location backing it to take the
        // address of.
        void write(void* instancePtr, ClassWriter* writer) const override {
            if (const Class* nestedClazz = classinfo(type()); nestedClazz != nullptr) {
                if (isAddressable()) {
                    if (void* nestedPtr = address(instancePtr); nestedPtr != nullptr) {
                        nestedClazz->write(nestedPtr, writer, this->name());
                    }
                } else if constexpr (std::is_copy_constructible_v<ValueT>) {
                    if (std::any boxed = get(instancePtr); boxed.has_value()) {
                        ValueT temp = std::any_cast<ValueT>(boxed);
                        nestedClazz->write(&temp, writer, this->name());
                    }
                }
                return;
            }
            Property::writeValue( this, get(instancePtr), nullptr, writer);
        }

        // Mirrors write()'s own three-way split above, plus the
        // heap-vs-stack question write() never had to answer (a live
        // ValueT can always just be read - see Property::write()'s own
        // comment - but a *new* one has to be built somewhere first):
        //   - shouldCreateOnHeap(): always a fresh heap instance via the
        //     nested Class's own registered constructor, handed over
        //     through whichever real setter this property has (member_/
        //     ptrSetter_/setter_ - see set()'s own "never improvise a set"
        //     comment) - never written into whatever's currently there,
        //     even if isAddressable() would also be true (a PtrGetter
        //     property reports addressable purely because ptrGetter_
        //     exists, regardless of whether the pointer it currently
        //     returns is null - not a reliable "already-live storage"
        //     signal the way MemberPtr/RefGetter's are).
        //   - isAddressable() (and not heap): already-live storage -
        //     address()'s target, whether that sits behind a unique_ptr
        //     (View::style()) or inline (a data member) - read straight
        //     into it, no allocation anywhere.
        //   - neither: a plain by-value getter/setter pair (e.g. Rect) -
        //     build a throwaway stack local, read fields into it (still no
        //     heap involved), then hand the finished value to the real
        //     setter by copy.
        void read(void* instancePtr, ClassReader* reader) const override {
            if (const Class* nestedClazz = classinfo(type()); nestedClazz != nullptr) {
                if constexpr (std::is_copy_constructible_v<ValueT>) {
                    if (shouldCreateOnHeap()) {
                        // raw (a type-erased void*), not
                        // std::any_cast<ValueT*>(fresh) - fresh may hold a
                        // more-derived pointer than ValueT (nestedClazz->
                        // read() can resolve a "type" tag naming a real
                        // subclass, see TypedClass<T>::read()'s own
                        // comment), and any_cast needs an exact type
                        // match. static_cast<ValueT*>(raw) is well-defined
                        // here for the same single-non-virtual-inheritance
                        // reason TypedPropertyCollection::readFreshElement()'s
                        // own pointer branch already relies on.
                        std::any fresh;
                        bool onHeap = false;
                        void* raw = nullptr;
                        nestedClazz->read(reader, name(), fresh, onHeap, &raw);
                        if (raw != nullptr) {
                            set(instancePtr, std::any(static_cast<ValueT*>(raw)));
                        }
                    } else if (isAddressable()) {
                        if (void* nestedPtr = address(instancePtr); nestedPtr != nullptr) {
                            std::any existing(static_cast<ValueT*>(nestedPtr));
                            bool onHeap = false;
                            nestedClazz->read(reader, name(), existing, onHeap);
                        }
                    } else if constexpr (std::is_default_constructible_v<ValueT> && std::is_copy_assignable_v<ValueT>) {
                        ValueT temp{};
                        std::any boxedTemp(&temp);
                        bool onHeap = false;
                        nestedClazz->read(reader, name(), boxedTemp, onHeap);
                        set(instancePtr, std::any(temp));
                    }
                }
                return;
            }
            std::any val;
            Property::readValue(name(), type(), val, instancePtr, reader);
            set(instancePtr, val);
        }
    private:
        MemberPtr member_ = nullptr;
        RefGetter refGetter_;
        PtrGetter ptrGetter_;
        PtrSetter ptrSetter_;
        Getter getter_;
        Setter setter_;
    };

    // A Property whose value is itself a collection (std::vector/std::array/
    // std::map - see container_traits above) - element-level access beyond
    // Property's own whole-container address()/get()/set(). Concrete (not
    // abstract), same idiom as Property/Field: every method here has a
    // do-nothing default (element type/key type of void, count of 0, a
    // no-op get/set), and TypedPropertyCollection<SourceT,ContainerT>
    // (below) overrides all of it with a real container_traits-backed
    // implementation - a bare PropertyCollection is never useful on its
    // own, just like a bare Property/Field never carries real thunks
    // outside ClassBuilder's untyped path.
    //
    // count()/get()/set() below act on a std::any the caller already holds
    // (typically Property::get(instance)'s result) rather than on a live
    // SourceT instance directly - this is what lets a caller enumerate/
    // mutate collection elements from nothing but a std::any value, with no
    // compile-time ValueT and no void* instance pointer to a still-live
    // owning object. get(std::any&, size_t)/get(std::any&, const std::any&)
    // both exist so a caller can index sequentially (vector/array) or by a
    // real key (map) through the same interface - for a sequential
    // collection the key overload just treats the key as an index.
    //
    // Property::get(void*)/set(void*, const std::any&) declared here would
    // otherwise be hidden by this class's own get()/set() overloads (C++
    // name hiding is per-name, not per-signature) - the using-declarations
    // below keep the whole-container path reachable through a
    // PropertyCollection*/TypedPropertyCollection*, not just via an upcast
    // to Property*.
    //@reflect ignore=true
    class PropertyCollection : public Property {
    public:
        using Property::get;
        using Property::set;

        PropertyCollection(std::string name, std::type_index type, Scope scope,
                             PropertyFlags flags = PropertyFlags::Collection)
            : Property(std::move(name), type, scope, flags) {}

        virtual std::type_index elementType() const { return typeid(void); }
        virtual std::type_index keyType() const { return typeid(void); }

        virtual std::size_t count(std::any& instance) const { return 0; }
        virtual std::any get(std::any& instance, std::size_t index) const { return std::any(); }
        virtual void set(std::any& instance, std::size_t index, const std::any& value) const {}
        virtual std::any get(std::any& instance, const std::any& key) const { return std::any(); }
        virtual void set(std::any& instance, const std::any& key, const std::any& value) const {}

        // Calls the *owning* object's real add/remove method directly
        // (e.g. View::addChild()/removeChild()) - never a copy of the
        // collection's current contents the way count()/get(std::any&,...)
        // above operate on. Mutating a disconnected snapshot wouldn't
        // actually add/remove anything from the real object, and (per the
        // same reasoning TypedProperty::set()'s own comment gives for
        // never improvising a "set" out of a bare getter) the real method
        // may maintain invariants - parent/rootView linkage, layout
        // invalidation - a raw container mutation would silently skip.
        // Only true for a collection built via TypedPropertyCollection's
        // AddFn/RemoveFn/CountFn/GetAtFn accessor-function constructor
        // (see its own comment) - false/no-op for every other mode, same
        // as isAddressable() is false there.
        virtual bool supportsAddRemove() const { return false; }
        virtual void add(void* instance, const std::any& element) const {}
        virtual void remove(void* instance, const std::any& element) const {}

        // Boxes the whole container exactly once (via Property::get(void*),
        // the same whole-container path address()/get(instancePtr) already
        // use) and reuses that one boxed std::any for every element -
        // count()/get(std::any&,index) below all read through it, never
        // re-deriving it from instancePtr per item.
        void write(void* instancePtr, ClassWriter* writer) const override {
            std::any boxedInstance = get(instancePtr);
            auto itemCount = count(boxedInstance);

            writer->beginCollection(name());

            for (std::size_t index = 0; index < itemCount; ++index) {
                writeItem(boxedInstance, index, writer);
            }

            writer->endCollection(name());
        }

        // Unlike write() above, this never goes through get()/set()'s
        // std::any-boxed-copy round trip - get(instancePtr) always hands
        // back a *copy* (see TypedPropertyCollection::get()'s own comment),
        // and TypedPropertyCollection has no Setter-pair concept at all
        // (only a plain data member ever gets written back through set()) -
        // so a collection reachable only through a getter (e.g.
        // "childViews", see this class's own header comment) has no way to
        // hand a freshly-read container back to its owner at all. Rather
        // than build (and leak, since nothing would ever take ownership)
        // elements with nowhere real to go, this skips entirely unless
        // isAddressable() - i.e. there's a real ContainerT& to mutate in
        // place (a plain data member, or a reference-returning getter like
        // View::style() has for a single nested property) - matching
        // TypedProperty::read()'s own isAddressable() branch, which also
        // always writes directly through address() rather than round-
        // tripping through get()/set().
        //
        // Loops by reader->beginCollection()'s returned count (the
        // *source* data's element count), not by this container's current
        // size the way write() loops by the live count - a fresh/shorter
        // container needs to grow (see TypedPropertyCollection::readItem()
        // and container_can_add_v), and a longer one is simply left with
        // its own trailing elements untouched past the source's count.
        //
        // Two reconstruction paths, mutually exclusive: isAddressable()
        // (a real live ContainerT& to place elements into, growing it via
        // Traits::add() past its current size - readItem()'s job) takes
        // priority; supportsAddRemove() (no live container at all, only
        // the owning object's real add() method - readAndAddItem()'s job)
        // is the fallback for exactly the case isAddressable() can't cover
        // (see this class's own header comment on "childViews"). Neither
        // being true means this generic interface has nothing it can do -
        // same read-only-through-here contract a getter-only Property has.
        void read(void* instancePtr, ClassReader* reader) const override {
            if (isAddressable()) {
                void* containerPtr = address(instancePtr);
                if (containerPtr == nullptr) {
                    return;
                }

                std::size_t itemCount = reader->beginCollection(name());
                for (std::size_t index = 0; index < itemCount; ++index) {
                    readItem(containerPtr, index, reader);
                }
                reader->endCollection(name());
                return;
            }

            if (supportsAddRemove()) {
                std::size_t itemCount = reader->beginCollection(name());
                for (std::size_t index = 0; index < itemCount; ++index) {
                    readAndAddItem(instancePtr, index, reader);
                }
                reader->endCollection(name());
            }
        }

        virtual void writeItem(std::any& boxedInstance, std::size_t index, ClassWriter* writer) const = 0;

        // containerPtr is the real, live ContainerT* (address()'s result -
        // see read()'s own comment for why this never goes through the
        // std::any-boxed-copy path writeItem() above uses), not a boxed
        // snapshot - a TypedPropertyCollection<SourceT,ContainerT> override
        // casts it back to ContainerT& itself, the one place that cast is
        // legal (same "SourceT/ContainerT cast happens exactly once, inside
        // the Typed* class's own methods" discipline TypedProperty/
        // TypedPropertyCollection already follow everywhere else).
        virtual void readItem(void* containerPtr, std::size_t index, ClassReader* reader) const = 0;

        // read()'s supportsAddRemove() path - instance is the *owning*
        // object (not a container), passed straight to add() once a fresh
        // element has been read. Default no-op, matching every other
        // "not this mode" default in this class.
        virtual void readAndAddItem(void* instance, std::size_t index, ClassReader* reader) const {}
    };

    // T-aware TypedPropertyCollection<SourceT,ContainerT> - same three
    // backing modes as TypedProperty (see its own comment): a real
    // pointer-to-data-member (addressable), a reference-returning getter
    // method (addressable, e.g. a collection reachable only through an
    // accessor that hands back ContainerT&), or a plain by-value getter
    // (not addressable - the only option when the real container is only
    // reachable through a *const*-returning accessor, e.g.
    // `const std::vector<SubView*>& View::childViews()`, which can't
    // itself be exposed as a live mutable ContainerT& without reaching
    // past real C++ access control the way NEWUI_REFLECT_PRIVATE() does).
    // container_traits<ContainerT> (above) supplies every element-level
    // operation regardless of which mode built this instance.
    //@reflect ignore=true
    template<typename SourceT, typename ContainerT>
    class TypedPropertyCollection : public PropertyCollection {
    public:
        using Traits = detail::container_traits<ContainerT>;
        using ElementT = typename Traits::ElementT;
        using KeyT = typename Traits::KeyT;
        using MemberPtr = ContainerT SourceT::*;
        using RefGetter = std::function<ContainerT&(SourceT&)>;
        using Getter = std::function<ContainerT(SourceT&)>;
        // Fourth backing mode, distinct from the three above: no real
        // ContainerT ever exists as a gettable/addressable object at all -
        // count/getAt drive read-only enumeration (get(void*) below
        // synthesizes a snapshot from them), and add/remove call the
        // *owning* object's real methods directly (e.g. View::addChild()/
        // removeChild()) rather than mutating a container that was never
        // really there - see PropertyCollection::add()/remove()'s own
        // comment for why that distinction is the whole point of this
        // mode, not an implementation detail. add/remove are independently
        // optional (either or both nullptr for a read-only collection);
        // ClassBuilder::propertyCollection() is the only thing that ever
        // builds one of these.
        using CountFn = std::function<std::size_t(SourceT&)>;
        using GetAtFn = std::function<ElementT(SourceT&, std::size_t)>;
        using AddFn = std::function<void(SourceT&, const ElementT&)>;
        using RemoveFn = std::function<void(SourceT&, const ElementT&)>;

        TypedPropertyCollection(std::string name, Scope scope, MemberPtr member)
            : PropertyCollection(std::move(name), typeid(ContainerT), scope, flagsFor()), member_(member) {}

        TypedPropertyCollection(std::string name, Scope scope, RefGetter refGetter)
            : PropertyCollection(std::move(name), typeid(ContainerT), scope, flagsFor()), refGetter_(std::move(refGetter)) {}

        TypedPropertyCollection(std::string name, Scope scope, Getter getter)
            : PropertyCollection(std::move(name), typeid(ContainerT), scope, flagsFor()), getter_(std::move(getter)) {}

        TypedPropertyCollection(std::string name, Scope scope, CountFn count, GetAtFn getAt,
                                 AddFn add = nullptr, RemoveFn remove = nullptr)
            : PropertyCollection(std::move(name), typeid(ContainerT), scope, flagsFor()),
              countFn_(std::move(count)), getAtFn_(std::move(getAt)), addFn_(std::move(add)), removeFn_(std::move(remove)) {}

        bool isAddressable() const override {
            return member_ != nullptr || bool(refGetter_);
        }

        void* address(void* instance) const override {
            SourceT* self = static_cast<SourceT*>(instance);
            if (member_ != nullptr) {
                return &(self->*member_);
            }
            if (refGetter_) {
                return &refGetter_(*self);
            }
            throw std::logic_error("TypedPropertyCollection::address(): '" + name() +
                "' is a by-value getter collection, not addressable");
        }
        // The accessor-fn branch synthesizes a fresh ContainerT snapshot by
        // calling countFn_/getAtFn_ (self's real accessor methods) once
        // each per element and Traits::add()-ing the results in - the same
        // "read-only enumeration off a plain copy" shape the Getter branch
        // above already has, just built one element at a time instead of
        // handed back whole. Fine for write()'s purposes (the only
        // consumer of the whole-container get() path - see its own
        // comment); reconstruction never goes through this snapshot at
        // all, only through add()/readAndAddItem() below.
        std::any get(void* instance) const override {
            SourceT* self = static_cast<SourceT*>(instance);
            if (member_ != nullptr) {
                return std::any(self->*member_);
            }
            if (refGetter_) {
                return std::any(refGetter_(*self));
            }
            if (getter_) {
                return std::any(getter_(*self));
            }
            if (countFn_ && getAtFn_) {
                ContainerT snapshot{};
                std::size_t n = countFn_(*self);
                for (std::size_t i = 0; i < n; ++i) {
                    if constexpr (detail::container_can_add_v<ContainerT, ElementT>) {
                        Traits::add(snapshot, getAtFn_(*self, i));
                    }
                }
                return std::any(std::move(snapshot));
            }
            return std::any();
        }
        // Only member_ (a real pointer-to-data-member) ever writes - same
        // "never improvise a set out of a bare getter" rule TypedProperty::
        // set() documents; a RefGetter/Getter/accessor-fn collection with
        // no setter is read-only through this generic *whole-container*
        // interface. Also matches how reconstruction actually happens for
        // every collection this codebase reflects today - one element at a
        // time through the owning object's real addChild()-shaped API
        // (add(), below, for the accessor-fn mode), never a whole-
        // container replace.
        void set(void* instance, const std::any& value) const override {
            if (member_ != nullptr) {
                SourceT* self = static_cast<SourceT*>(instance);
                self->*member_ = std::any_cast<ContainerT>(value);
            }
        }

        bool supportsAddRemove() const override { return bool(addFn_); }
        void add(void* instance, const std::any& element) const override {
            if (addFn_) {
                addFn_(*static_cast<SourceT*>(instance), std::any_cast<ElementT>(element));
            }
        }
        void remove(void* instance, const std::any& element) const override {
            if (removeFn_) {
                removeFn_(*static_cast<SourceT*>(instance), std::any_cast<ElementT>(element));
            }
        }

        std::type_index elementType() const override { return typeid(ElementT); }
        std::type_index keyType() const override { return typeid(KeyT); }

        std::size_t count(std::any& instance) const override { return Traits::count(unbox(instance)); }
        std::any get(std::any& instance, std::size_t index) const override {
            return std::any(Traits::getByIndex(unbox(instance), index));
        }
        void set(std::any& instance, std::size_t index, const std::any& value) const override {
            Traits::setByIndex(unbox(instance), index, std::any_cast<ElementT>(value));
        }
        std::any get(std::any& instance, const std::any& key) const override {
            return std::any(Traits::getByKey(unbox(instance), std::any_cast<KeyT>(key)));
        }
        void set(std::any& instance, const std::any& key, const std::any& value) const override {
            Traits::setByKey(unbox(instance), std::any_cast<KeyT>(key), std::any_cast<ElementT>(value));
        }

        // boxedInstance is the whole container (built once by
        // PropertyCollection::write()), never re-derived here. For a
        // pointer-typed element (e.g. SubView* in a childViews-shaped
        // collection) this resolves the nested Class by the *pointee's*
        // actual runtime type (typeid(*elementPtr), via ElementT's own
        // vtable) rather than elementType()'s static declared type - the
        // same "single, non-virtual inheritance means a pointer erased to
        // void* and cast back through the identical static type is always
        // well-defined" reasoning any polymorphic-through-void* code in
        // this codebase relies on (see examples/reflection2.cpp's own
        // resolveNestedClass()). A non-pointer element has no such
        // question - elementType() is already its real (and only) type.
        void writeItem(std::any& boxedInstance, std::size_t index, ClassWriter* writer) const override {
            std::any elementVal = get(boxedInstance, index);
            std::any idx(index);

            writer->beginElement(idx, std::any());

            if constexpr (std::is_pointer_v<ElementT>) {
                ElementT elementPtr = std::any_cast<ElementT>(elementVal);
                std::type_index runtimeType = elementPtr != nullptr ? typeid(*elementPtr) : elementType();
                Property::writeValue("", runtimeType, elementVal, static_cast<void*>(elementPtr), writer);
            } else {
                // instancePtr can't be nullptr here the way the pointer
                // branch's null-elementPtr case legitimately can - a
                // non-pointer element always has a real value to write,
                // and Property::writeValue()'s nested-Class branch
                // (reflection.cpp) only recurses into valClazz->write()
                // when instancePtr is non-null; passing nullptr for a
                // registered-class ElementT (e.g. GradientStop, Point)
                // silently wrote nothing at all for the whole element -
                // no error, just an empty array-slot skipped, which is
                // why a Gradient's own "stops"/Path's own "points" wrote
                // as "[]" even with real elements. any_cast<ElementT>'s
                // pointer overload reaches into elementVal's own storage
                // (elementVal was just constructed to hold exactly
                // ElementT, so this always succeeds) rather than
                // requiring some other already-live address.
                ElementT* elementPtr = std::any_cast<ElementT>(&elementVal);
                Property::writeValue("", elementType(), elementVal, elementPtr, writer);
            }

            writer->endElement(idx, std::any());
        }

        // containerPtr is the real, live ContainerT* - see PropertyCollection::
        // read()'s own comment for why this never goes through get()/set()'s
        // boxed-copy path the way writeItem() does. place() below decides
        // overwrite-in-place (index already exists) vs. grow
        // (container_can_add_v - see std::vector's container_traits) for
        // both the pointer and non-pointer element shapes, so the two
        // branches below only differ in *how a fresh ElementT gets built*,
        // not in how it lands in the container.
        void readItem(void* containerPtr, std::size_t index, ClassReader* reader) const override {
            ContainerT& container = *static_cast<ContainerT*>(containerPtr);
            std::any idx(index);

            auto place = [&](const ElementT& value) {
                if (index < Traits::count(container)) {
                    Traits::setByIndex(container, index, value);
                } else if constexpr (detail::container_can_add_v<ContainerT, ElementT>) {
                    Traits::add(container, value);
                } else {
                    throw std::logic_error("TypedPropertyCollection::readItem(): '" + name() +
                        "' container type has no way to grow past its current size");
                }
            };

            reader->beginElement(idx, std::any());
            if (std::any fresh = readFreshElement(reader); fresh.has_value()) {
                place(std::any_cast<ElementT>(fresh));
            }
            reader->endElement(idx, std::any());
        }

        // PropertyCollection::read()'s supportsAddRemove() path - instance
        // is the *owning* object (View, not a std::vector<SubView*>), so
        // the freshly-read element goes straight to add() (which calls the
        // real addChild()-shaped method) rather than anywhere a container
        // would be - there is no container here at all, see this class's
        // own AddFn/RemoveFn/CountFn/GetAtFn comment.
        void readAndAddItem(void* instance, std::size_t index, ClassReader* reader) const override {
            std::any idx(index);
            reader->beginElement(idx, std::any());
            if (std::any fresh = readFreshElement(reader); fresh.has_value()) {
                add(instance, fresh);
            }
            reader->endElement(idx, std::any());
        }
    private:
        // Reads one fresh element (an ElementT, or an ElementT* for a
        // pointer-typed element - see the pointer branch's own comment)
        // off reader, boxed as std::any - the part readItem() (places it
        // into an existing/growing ContainerT) and readAndAddItem() (hands
        // it straight to add()) share; they only differ in *where* the
        // finished element ends up, never in how it gets built. Caller is
        // responsible for its own beginElement()/endElement() bracketing -
        // this only reads what's between them.
        std::any readFreshElement(ClassReader* reader) const {
            if constexpr (std::is_pointer_v<ElementT>) {
                // Always a fresh heap instance - same ownership-transfer
                // shape TypedProperty::read()'s own shouldCreateOnHeap()
                // branch uses for a single pointer property (see
                // flagsFor()'s comment below) - a collection element has no
                // "already there, edit in place" case the way an addressable
                // *property* does, it's either a brand new slot/add() call
                // or an existing one being fully replaced, never partially
                // updated. Resolves the nested Class by the element's
                // *static* declared pointee type (e.g. Shape) - that's
                // only a starting point, though: nestedClazz->read() below
                // (TypedClass<T>::read(), reflection.h) itself consults
                // the source data's own "type" tag (via beginObject()'s
                // return) and, when it names a genuine subclass, hands
                // back an instance of *that* concrete type instead - see
                // its own comment for the full mechanism. raw is a type-
                // erased void*, not std::any_cast<PointeeT*>(fresh) - the
                // any_cast would throw whenever polymorphic dispatch
                // actually fired (fresh then holds e.g. Circle*, not
                // Shape*, and any_cast needs an exact type match) - same
                // "capture the pointer as void* at the point it's known,
                // don't try to any_cast it back out later" reasoning
                // Constructor::invoke(args, outRaw)'s own comment gives.
                // static_cast<PointeeT*>(raw) is well-defined here only
                // because every registered element type in practice
                // reaches PointeeT via single, non-virtual inheritance
                // (same assumption writeItem()'s own runtime-type lookup
                // above already relies on for the write side).
                using PointeeT = std::remove_pointer_t<ElementT>;
                if (const Class* nestedClazz = classinfo(typeid(PointeeT)); nestedClazz != nullptr) {
                    std::any fresh;
                    bool onHeap = false;
                    void* raw = nullptr;
                    nestedClazz->read(reader, "", fresh, onHeap, &raw);
                    if (raw != nullptr) {
                        return std::any(static_cast<PointeeT*>(raw));
                    }
                }
                return std::any();
            } else if (const Class* nestedClazz = classinfo(elementType()); nestedClazz != nullptr) {
                if constexpr (std::is_default_constructible_v<ElementT> && std::is_copy_constructible_v<ElementT>) {
                    ElementT value{};
                    std::any boxedTemp(&value);
                    bool onHeap = false;
                    nestedClazz->read(reader, "", boxedTemp, onHeap);
                    return std::any(value);
                }
                return std::any();
            } else {
                std::any val;
                Property::readValue("", elementType(), val, nullptr, reader);
                return val;
            }
        }

        // A pointer ElementT (e.g. "childViews"'s SubView*) is exactly the
        // same ownership shape TypedProperty's own CreatedOnHeap detection
        // covers for a single nested property - each element read() gets
        // is a fresh heap instance handed over one at a time (addChild()-
        // style), never an in-place edit of something already there.
        static PropertyFlags flagsFor() {
            PropertyFlags f = Traits::associative ? (PropertyFlags::Collection | PropertyFlags::Associative) : PropertyFlags::Collection;
            if constexpr (std::is_pointer_v<ElementT>) {
                f |= PropertyFlags::CreatedOnHeap;
            }
            return f;
        }

        static ContainerT& unbox(std::any& instance) { return std::any_cast<ContainerT&>(instance); }

        MemberPtr member_ = nullptr;
        RefGetter refGetter_;
        Getter getter_;
        CountFn countFn_;
        GetAtFn getAtFn_;
        AddFn addFn_;
        RemoveFn removeFn_;
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
    //@reflect ignore=true
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
    //@reflect ignore=true
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
    //@reflect ignore=true
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

        // Every currently-connected, describable listener's own descriptor
        // (see newui::Delegate<SenderT,Args...>::describedListeners(),
        // delegate.h) - what TypedClass<T>::write() (below) uses to decide
        // both whether this delegate gets a "delegates" entry at all (an
        // empty result - the common case for a delegate nobody's connected
        // yet, or one only ever wired up through undescribed add() calls -
        // means "don't write me") and, when it does, exactly which
        // descriptor strings go in its array. Base Delegate has no real
        // newui::Delegate<> to reach into (it only knows a type-erased
        // instance pointer, not SourceT/Args), so this is virtual, real
        // implementation in TypedDelegate below.
        virtual std::vector<std::string> describedListeners(void* instance) const {
            return {};
        }

        // Reconnects a listener read back from a "delegates" block
        // (ObjectReader::readObjects(), reflectionio.h) - `targetMethod`
        // must be a genuinely public Method on the target's own Class
        // (walked up its base chain by the caller, since Class::method()
        // itself only checks direct members) whose signature is
        // `SyncReturn(SourceT*, Args...)` - sender by *pointer*, not
        // reference, is what makes this representable through Method::
        // invoke()'s std::any-based argument boxing at all (a non-const
        // reference argument can't safely round-trip through std::any -
        // see invoke_arg_type_unsupported()'s comment in reflectgen.py for
        // the same constraint on the codegen side); newui::Delegate<>'s
        // own Callback signature (SourceT&, Args...) is untouched, this
        // only concerns what a *reflectable* listener method needs to look
        // like. Returns false (caller logs and skips) on any signature
        // mismatch, or if this base Delegate has no real SourceT/Args to
        // check against - real implementation in TypedDelegate below.
        virtual bool connectListener(void* senderInstance, const std::string& descriptor,
                                      void* targetInstance, const Method* targetMethod) const {
            return false;
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
    //@reflect ignore=true
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

        std::vector<std::string> describedListeners(void* instance) const override {
            SourceT* self = static_cast<SourceT*>(instance);
            return (self->*member_).describedListeners();
        }

        // See Delegate::connectListener()'s own comment for the signature
        // contract targetMethod must satisfy - checked here (arguments()[0]
        // must be SourceT*, the rest must match Args... exactly, and the
        // return must be a real SyncReturn) since a wrong signature would
        // otherwise surface as a std::bad_any_cast several frames down
        // inside targetMethod->invoke() instead of a clean "false" the
        // caller can log and skip.
        bool connectListener(void* senderInstance, const std::string& descriptor,
                              void* targetInstance, const Method* targetMethod) const override {
            if (!targetMethod->hasReturnValue() || targetMethod->returnType() != typeid(SyncReturn)) {
                return false;
            }
            const std::vector<Argument>& margs = targetMethod->arguments();
            if (margs.size() != 1 + sizeof...(Args) || margs[0].type != typeid(SourceT*)) {
                return false;
            }
            if (!argsMatch(margs, std::index_sequence_for<Args...>{})) {
                return false;
            }

            SourceT* self = static_cast<SourceT*>(senderInstance);
            Callback fn = [targetInstance, targetMethod](SourceT& sender, Args... args) -> SyncReturn {
                std::vector<std::any> boxed{ std::any(&sender), std::any(args)... };
                std::any result = targetMethod->invoke(targetInstance, boxed);
                return result.has_value() ? std::any_cast<SyncReturn>(result) : SyncReturn(SyncReturn::Ignored);
            };
            (self->*member_).add(descriptor, std::move(fn));
            return true;
        }

    private:
        using Callback = typename newui::Delegate<SourceT, Args...>::Callback;

        template<std::size_t... I>
        std::any invokeImpl(void* instance, const std::vector<std::any>& args, std::index_sequence<I...>) const {
            SourceT* self = static_cast<SourceT*>(instance);
            (self->*member_).syncCall(*self, std::any_cast<Args>(args.at(I))...);
            return std::any();
        }

        // Args...'s own types against margs[1..] (margs[0] - the sender
        // slot - already checked separately in connectListener(), always
        // SourceT* there, never one of Args...) - same index_sequence
        // shape invokeImpl() already uses, just comparing type_index
        // instead of unpacking a std::any.
        template<std::size_t... I>
        bool argsMatch(const std::vector<Argument>& margs, std::index_sequence<I...>) const {
            return ((margs[1 + I].type == typeid(Args)) && ...);
        }

        MemberPtr member_;
    };

    // One name/value pair of a reflected enum. value is always normalized to
    // uint64_t regardless of the enum's real underlying type, so Enum
    // doesn't need to be templated on it - unsigned specifically (not
    // int64_t) so the bitwise AND/OR/NOT decompose()/EnumBuilder<T>'s own
    // conversion functions never have to worry about sign-extension: a
    // signed type's ~value or a right-shift of a negative value are exactly
    // the kind of surprise a mask/flags enum's bit manipulation shouldn't
    // have to reason about. Any real (possibly signed) underlying_type_t<T>
    // value still round-trips correctly through this - EnumBuilder<T>'s own
    // toUInt64_/fromUInt64_ do the (defined, bit-preserving) signed<->
    // unsigned conversion at the one place that knows T.
    //@reflect ignore=true
    struct EnumValue {
        std::string name;
        std::uint64_t value;
    };

    //@reflect ignore=true
    class Enum {
    public:
        Enum(std::type_index type, std::string name, std::string namespaceName = "")
            : type_(type), name_(std::move(name)), namespaceName_(std::move(namespaceName)) {}

        const std::string& name() const { return name_; }
        std::type_index type() const { return type_; }
        const std::vector<EnumValue>& values() const { return values_; }

        // Same shape as Class::namespaceName()/qualifiedName() (reflection.h) -
        // namespaceName() already carries its own trailing "::" (see
        // extractNamespace()), empty for a global-scope enum, so
        // qualifiedName() is a plain concatenation. Set by EnumBuilder<T>'s
        // constructor from typeid(T) directly, independent of whatever
        // bare `name` the caller passed in - see its own comment.
        const std::string& namespaceName() const { return namespaceName_; }
        std::string qualifiedName() const { return namespaceName_ + name_; }

        // Set only via EnumBuilder<T>::flags() - an explicit, per-enum
        // opt-in (see its own comment) rather than guessed from the
        // enum's values or name: this codebase's own real enums proved
        // every shape-based heuristic considered (power-of-two/combinable
        // values, an "operator|" overload, a "Mask"/"Flags" name suffix)
        // either misses a real flags enum or - worse - misclassifies an
        // ordinary sequential enum as flags-shaped (DialogResult's
        // Ok=1/Cancel=2/No=4 are individually powers of two purely by
        // coincidence of small sequential values, which would make
        // Abort=5 decompose into the nonsensical "Ok"|"No").
        bool isFlags() const { return isFlags_; }

        // Boxes/unboxes an arbitrary std::any holding this Enum's own
        // real C++ type - only EnumBuilder<T> (below) can install these,
        // since only it knows T at the point it's constructed; Enum
        // itself stays a concrete, non-polymorphic value type (consistent
        // with Delegate/Method/Constructor's own raw-function-pointer
        // type erasure elsewhere in this file, not virtual dispatch -
        // Enum is stored by value in ReflectionRegistry's own
        // std::unordered_map<type_index, Enum>, not through a pointer, so
        // there's nothing to make polymorphic in the first place).
        std::uint64_t toUInt64(const std::any& val) const {
            return toUInt64_ ? toUInt64_(val) : 0;
        }
        std::any fromUInt64(std::uint64_t val) const {
            return fromUInt64_ ? fromUInt64_(val) : std::any();
        }

        bool tryParse(const std::string& valueName, std::uint64_t& outValue) const {
            for (const auto& v : values_) {
                if (v.name == valueName) {
                    outValue = v.value;
                    return true;
                }
            }
            return false;
        }

        bool tryToString(std::uint64_t value, std::string& outName) const {
            for (const auto& v : values_) {
                if (v.value == value) {
                    outName = v.name;
                    return true;
                }
            }
            return false;
        }

        // Decomposes `value` into the fewest declared flag names whose
        // bitwise OR reconstructs it - only meaningful when isFlags() is
        // true, but callable regardless (an empty/all-numeric-remainder
        // result for a non-flags Enum just means "nothing declared
        // matched", same as tryToString() failing). Tries every declared
        // nonzero value as a candidate component, largest bit-count
        // (popcount) first so an explicitly-declared combo name (e.g.
        // "CtrlShift" = Ctrl|Shift) is preferred over separately naming
        // its individual bits, ties broken by declaration order. Any bits
        // left over once no further candidate matches (an undeclared bit
        // combination, or the enum wasn't really flags-shaped at all) are
        // appended as a single "0x..." hex token instead of silently
        // dropped - see ObjectReader's own read side for how that token
        // is recognized and OR'd back in on read, keeping this lossless
        // even for a value nothing here can fully name.
        std::vector<std::string> decompose(std::uint64_t value) const {
            std::vector<std::string> candidates;
            for (const auto& v : values_) {
                if (v.value != 0) {
                    candidates.push_back(v.name);
                }
            }
            std::stable_sort(candidates.begin(), candidates.end(),
                [this](const std::string& a, const std::string& b) {
                    return popcount(valueOf(a)) > popcount(valueOf(b));
                });

            std::vector<std::string> result;
            std::uint64_t remaining = value;
            for (const std::string& name : candidates) {
                std::uint64_t v = valueOf(name);
                if (v != 0 && (remaining & v) == v) {
                    result.push_back(name);
                    remaining &= ~v;
                    if (remaining == 0) {
                        break;
                    }
                }
            }
            if (remaining != 0) {
                std::ostringstream hex;
                hex << "0x" << std::hex << remaining;
                result.push_back(hex.str());
            }
            return result;
        }

    private:
        template<typename T> friend class EnumBuilder;

        std::uint64_t valueOf(const std::string& name) const {
            std::uint64_t v = 0;
            tryParse(name, v);
            return v;
        }

        static int popcount(std::uint64_t v) {
            int count = 0;
            while (v != 0) {
                count += static_cast<int>(v & 1u);
                v >>= 1;
            }
            return count;
        }

        using ToUInt64Fn = std::uint64_t (*)(const std::any&);
        using FromUInt64Fn = std::any (*)(std::uint64_t);

        std::type_index type_;
        std::string name_;
        std::string namespaceName_;
        std::vector<EnumValue> values_;
        bool isFlags_ = false;
        ToUInt64Fn toUInt64_ = nullptr;
        FromUInt64Fn fromUInt64_ = nullptr;
    };

    // One constructor overload. Unlike Property/Field/Delegate, and same as
    // Method, only ever built from a genuinely public constructor - see
    // Class::createInstance()'s comment.
    //@reflect ignore=true
    class Constructor {
    public:
        Constructor(std::vector<Argument> arguments, std::any (*invoke)(const std::vector<std::any>&))
            : arguments_(std::move(arguments)), invoke_(invoke) {}

        virtual ~Constructor() = default;

        const std::vector<Argument>& arguments() const { return arguments_; }

        virtual std::any invoke(const std::vector<std::any>& args) const {
            return invoke_ ? invoke_(args) : std::any();
        }

        // Same as invoke(args) above, but also hands back (when outRaw is
        // non-null) the freshly-constructed object as a type-erased
        // void* - for a caller that doesn't know the concrete SourceT at
        // the call site (TypedClass<T>::read()'s polymorphic-dispatch
        // branch: it only knows the statically-declared base T, e.g.
        // Shape, so it can't std::any_cast<ConcreteT*> the std::any this
        // returns - any_cast needs the exact type, and the whole point
        // here is that the concrete type is more-derived than what the
        // caller has). Default implementation just clears outRaw and
        // delegates to invoke(args) - only TypedConstructor's override
        // below (the one place with a real compile-time SourceT to
        // construct and hand a pointer to) can actually fill it in; the
        // untyped invoke_-thunk path (a hand-written registration, e.g.
        // examples/reflection1.cpp) doesn't support this, same as it
        // already doesn't support anything else requiring a real C++
        // type at this layer.
        virtual std::any invoke(const std::vector<std::any>& args, void** outRaw) const {
            if (outRaw != nullptr) {
                *outRaw = nullptr;
            }
            return invoke(args);
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
    //@reflect ignore=true
    template<typename SourceT, typename... Args>
    class TypedConstructor : public Constructor {
    public:
        TypedConstructor() : Constructor({ Argument{"", typeid(Args)}... }, nullptr) {}

        std::any invoke(const std::vector<std::any>& args) const override {
            return invokeImpl(args, nullptr, std::index_sequence_for<Args...>{});
        }

        std::any invoke(const std::vector<std::any>& args, void** outRaw) const override {
            return invokeImpl(args, outRaw, std::index_sequence_for<Args...>{});
        }

    private:
        template<std::size_t... I>
        std::any invokeImpl(const std::vector<std::any>& args, void** outRaw, std::index_sequence<I...>) const {
            SourceT* obj = new SourceT(std::any_cast<Args>(args.at(I))...);
            if (outRaw != nullptr) {
                *outRaw = obj;
            }
            return std::any(obj);
        }
    };

    enum ClassFlags  {
        None = 0,
        Derived = 1u << 0,
        Abstract = 1u << 1,
        Struct = 1u << 2,
        Singleton = 1u << 3
    };
    /*
    inline ClassFlags operator|(ClassFlags lhs, ClassFlags rhs) {
        return static_cast<ClassFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }
    inline ClassFlags operator&(ClassFlags lhs, ClassFlags rhs) {
        return static_cast<ClassFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
    }
    inline ClassFlags& operator|=(ClassFlags& lhs, ClassFlags rhs) { return lhs = lhs | rhs; }
    */

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
    //@reflect ignore=true
    class Class {
    public:
        Class(std::type_index type, std::string name, std::string namespaceName)
            : type_(type), flags_(0), name_(std::move(name)), namespaceName_(std::move(namespaceName)) {}

        virtual ~Class();
        Class(const Class&) = delete;
        Class& operator=(const Class&) = delete;
        Class(Class&&) = default;
        Class& operator=(Class&&) = default;

        std::uint32_t flags() const { return flags_; }
        bool isDerived() const { return (flags_ & ClassFlags::Derived) != ClassFlags::None; }        
        bool isAbstract() const { return (flags_ & ClassFlags::Abstract) != ClassFlags::None; }
        bool isStruct() const { return (flags_ & ClassFlags::Struct) != ClassFlags::None; }
        bool isSingleton() const { return (flags_ & ClassFlags::Singleton) != ClassFlags::None; }

        // The reflected base class this class was registered against (see
        // ClassBuilder<T>::base<BaseT>()), or nullptr if none was given -
        // either because T has no base at all, or because it does but that
        // base was never itself run through ClassBuilder/registerClass()
        // (isDerived() can still be true in that case; parentClass() just
        // has nothing to point at).
        const Class* parentClass() const { return parentClass_; }

        std::type_index type() const { return type_; }
        const std::string& name() const { return name_; }
        const std::string& namespaceName() const { return namespaceName_; }

        // namespaceName() + name(), e.g. "newui::Rect" - namespaceName()
        // already carries its own trailing "::" (see extractNamespace()),
        // so this is a plain concatenation. classinfo()/getClass() accept
        // this alongside the bare name() - see ReflectionRegistry::
        // registerClass()'s own comment for why both are registered.
        std::string qualifiedName() const { return namespaceName_ + name_; }

        const std::vector<Property*>& properties() const { return properties_; }
        const std::vector<Field*>& fields() const { return fields_; }
        const std::vector<Method*>& methods() const { return methods_; }
        const std::vector<Delegate*>& delegates() const { return delegates_; }

        const Property* property(const std::string& propertyName) const;
        const Field* field(const std::string& fieldName) const;
        const Method* method(const std::string& methodName) const;
        const Delegate* delegate(const std::string& delegateName) const;

        // Most-derived-first, first occurrence of each name wins - same
        // merge rule TypedClass<T>::allProperties() already uses (see its
        // own comment), just for delegates_ instead of properties_. Lives
        // directly on Class (not TypedClass<T>, unlike allProperties())
        // since it only ever touches delegates()/parentClass(), both
        // already public here - needed by ObjectReader::readObjects()
        // (reflectionio.h), which only ever has a type-erased `const
        // Class*` per object (its concrete T isn't known until the
        // "type" tag is resolved at runtime), never a TypedClass<T> to
        // call a templated helper on.
        bool allDelegates(std::vector<const Delegate*>& outDelegates) const {
            for (const Class* c = this; c != nullptr; c = c->parentClass()) {
                for (const Delegate* d : c->delegates()) {
                    bool alreadySeen = false;
                    for (const Delegate* existing : outDelegates) {
                        if (existing->name() == d->name()) {
                            alreadySeen = true;
                            break;
                        }
                    }
                    if (!alreadySeen) {
                        outDelegates.push_back(d);
                    }
                }
            }
            return !outDelegates.empty();
        }

        // Same most-derived-first, first-name-wins merge as allDelegates()
        // above, just for properties() instead. Lives here (not on
        // TypedClass<T>) for the same reason allDelegates() does - a
        // caller resolving a polymorphic collection element or nested
        // property (TypedClass<T>::read()'s own dispatch branch,
        // TypedPropertyCollection::readFreshElement()) only ever has a
        // type-erased `const Class*` for whichever *concrete* class a
        // "type" tag named, not a TypedClass<T> to call a templated
        // helper on.
        bool allProperties(std::vector<const Property*>& outProperties) const {
            for (const Class* c = this; c != nullptr; c = c->parentClass()) {
                for (const Property* p : c->properties()) {
                    bool alreadySeen = false;
                    for (const Property* existing : outProperties) {
                        if (existing->name() == p->name()) {
                            alreadySeen = true;
                            break;
                        }
                    }
                    if (!alreadySeen) {
                        outProperties.push_back(p);
                    }
                }
            }

            return !outProperties.empty();
        }

        // Same merge again, for fields() - a plain public-field value
        // type (Point, Size, Rect, ...) has no properties of its own at
        // all, only fields, so write()/read() need this to actually
        // reach them.
        bool allFields(std::vector<const Field*>& outFields) const {
            for (const Class* c = this; c != nullptr; c = c->parentClass()) {
                for (const Field* f : c->fields()) {
                    bool alreadySeen = false;
                    for (const Field* existing : outFields) {
                        if (existing->name() == f->name()) {
                            alreadySeen = true;
                            break;
                        }
                    }
                    if (!alreadySeen) {
                        outFields.push_back(f);
                    }
                }
            }

            return !outFields.empty();
        }

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

        // Same as createInstance() above, but also hands back (when
        // outRaw is non-null) the freshly-built object as a type-erased
        // void* - see Constructor::invoke(args, outRaw)'s own comment for
        // why a caller ever needs this instead of just any_cast-ing the
        // returned std::any: it's for exactly the case where the caller
        // (this Class's *own* type isn't necessarily what got
        // constructed - e.g. calling createInstance(&raw) on a more-
        // derived Class* resolved from a "type" tag) doesn't know the
        // concrete type at the call site.
        std::any createInstance(void** outRaw) const;

        // name is the key this object should attach itself under in
        // whatever the Writer is building - Writer::beginObject()/
        // endObject()'s own first argument, verbatim. Empty for the two
        // cases that don't have one: the true document root (the caller's
        // very first Class::write() call - ObjectWriter::write<InstanceT>()
        // passes an empty name for exactly this reason), and a collection
        // element (positional, not keyed - see PropertyCollection::write()/
        // TypedPropertyCollection::writeItem(), which always pass "").
        // Property::writeValue() supplies a real one for a nested property
        // (its own name()) when recursing via valClazz->write(); a
        // multi-object write (ObjectWriter::writeObjects(), reflectionio.h)
        // supplies the object's own document-level key ("foo", "bar", ...)
        // directly - a plain string, not a Property*, since a root-level
        // named object in a multi-object document isn't anybody's property
        // at all. Matches read()'s own propertyName parameter, just below -
        // this used to take `const Property* owningProperty` instead and
        // derive the name from it, an asymmetry with read() that also made
        // a multi-object write need to fabricate a throwaway Property just
        // to name itself; taking the string directly removes both.
        virtual void write(void* instancePtr, ClassWriter* writer, const std::string& name) const = 0;

        // outRawInstance, when non-null, receives the same raw pointer
        // outInstance ends up holding (as a type-erased void* rather than
        // the std::any's own concretely-typed T*) - the only way a caller
        // that doesn't know T can get a usable pointer back at all, since
        // std::any_cast requires an exact type match and a generic caller
        // has no T to cast to. Needed by ObjectReader::readObjects()
        // (reflectionio.h): its pass 1 creates/reads every named object
        // through this same Class::read() regardless of its concrete type,
        // then pass 2 needs a real pointer for each to hand to Method::
        // invoke()/Delegate::connectListener() while resolving delegate
        // connections - both of which already take void*, not std::any.
        // Defaulted so every existing call site is unaffected.
        virtual void read(ClassReader* reader, const std::string& propertyName, std::any& outInstance,
                            bool& instanceOnHeap, void** outRawInstance = nullptr) const = 0;
    private:
        template<typename T> friend class ClassBuilder;

        void setIsDerived(bool v) {
            flags_ = (v == true) ? flags_ | ClassFlags::Derived : flags_ & ~ClassFlags::Derived;
        }

        void setIsAbstract(bool v) {
            flags_ = (v == true) ? flags_ | ClassFlags::Abstract : flags_ & ~ClassFlags::Abstract;
        }

        void setIsStruct(bool v) {
            flags_ = (v == true) ? flags_ | ClassFlags::Struct : flags_ & ~ClassFlags::Struct;
        }

        void setIsSingleton(bool v) {
            flags_ = (v == true) ? flags_ | ClassFlags::Singleton : flags_ & ~ClassFlags::Singleton;
        }

        // Raw, non-owning pointer into the registry's own copy of the base
        // class's Class (set by ClassBuilder<T>::base<BaseT>(), never by
        // this class itself) - ~Class() must never delete this, unlike
        // properties_/fields_/methods_/delegates_/constructors_ below.
        // Stays valid as long as the base's Class isn't later replaced in
        // ReflectionRegistry (see ReflectionRegistry::registerClass()'s
        // replace-on-re-register behavior) - fine for the intended "register
        // every base before its derived classes, once, at startup" usage;
        // re-registering an already-linked-to base after the fact would
        // leave this dangling.
        const Class* parentClass_ = nullptr;
        std::type_index type_;
        std::uint32_t flags_;
        std::string name_;
        std::string namespaceName_;        
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
    //@reflect ignore=true
    template<typename T>
    class TypedClass : public Class {
    public:
        TypedClass(std::string name, std::string namespaceName)
            : Class(typeid(T), std::move(name), std::move(namespaceName)) {}

        virtual ~TypedClass() {}

        TypedClass(const TypedClass&) = delete;
        TypedClass& operator=(const TypedClass&) = delete;
        TypedClass(TypedClass&&) = default;
        TypedClass& operator=(TypedClass&&) = default;

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

        void write(void* instancePtr, ClassWriter* writer, const std::string& name) const override {
            // Empty for the true document root - see Class::write()'s own
            // doc comment for why: a Writer's attach()-style logic treats
            // an empty name at depth 0 as "this is the root, already
            // handled", not a keyed property. Writing the class name
            // (e.g. "Application") here instead made every root-level
            // write() call ObjectWriter::attach() with a non-empty name
            // after builder.pop() had already popped the one open scope -
            // the next builder[name]=... then read _counts.back() on an
            // empty vector (MSVC vector debug assert).
            writer->beginObject(name, this);

            std::vector<const Property*> ordered;
            allProperties(ordered);

            for (const Property* property : ordered) {
                property->write(instancePtr, writer);
            }

            // Fields - the .field() (rather than .property()) side of
            // ClassBuilder, e.g. Point/Size/Rect's own plain public x/y/
            // width/height. Public only, deliberately: a field with no
            // accessor methods at all reached private/protected scope
            // only via NEWUI_REFLECT_PRIVATE()'s friend bypass - fine for
            // ad hoc runtime introspection (Class::field(name)'s own
            // purpose), but writing one out here would serialize
            // implementation details a class never chose to expose,
            // unlike a Property (which already reaches private members
            // regardless of scope - see Property's own class comment) -
            // that asymmetry is intentional, not an oversight.
            std::vector<const Field*> orderedFields;
            allFields(orderedFields);

            for (const Field* field : orderedFields) {
                if (field->scope() == Scope::Public) {
                    field->write(instancePtr, writer);
                }
            }

            // "delegates" - see Delegate::describedListeners()'s own
            // comment. Collected up front (rather than writing straight
            // into a beginObject("delegates",...)/endObject() bracket as
            // each Delegate is visited) so the whole block - including
            // the wrapper object itself - is skipped entirely when no
            // Delegate on this class currently has anything describable
            // connected; ObjectWriter::beginObject()'s own null-Class
            // guard is what makes a Class-less "delegates" wrapper object
            // (no "type" tag - it isn't a reflected instance, just a
            // grouping) safe to open at all.
            std::vector<const Delegate*> delegatesOrdered;
            allDelegates(delegatesOrdered);
            std::vector<std::pair<const Delegate*, std::vector<std::string>>> described;
            for (const Delegate* d : delegatesOrdered) {
                std::vector<std::string> names = d->describedListeners(instancePtr);
                if (!names.empty()) {
                    described.emplace_back(d, std::move(names));
                }
            }
            if (!described.empty()) {
                writer->beginObject("delegates", nullptr);
                for (const auto& [d, names] : described) {
                    writer->beginCollection(d->name());
                    for (const std::string& n : names) {
                        writer->writeString("", n);
                    }
                    writer->endCollection(d->name());
                }
                writer->endObject("delegates", nullptr);
            }

            writer->endObject(name, this);
        }

        // outInstance already holding a value means the caller (a
        // TypedProperty<SourceT,T>::read() addressable/stack-local branch,
        // above) has somewhere real to write into already - a live
        // sub-object's address(), or a throwaway stack local's - so this
        // never allocates in that case, and instanceOnHeap is left exactly
        // as the caller set it (this call didn't allocate anything, so it
        // has nothing new to report). An empty outInstance only happens
        // from the shouldCreateOnHeap() branch (or a future generic
        // Reader's true top-level read(), which has no property/address
        // context at all) - always heap, and (see below) not always this
        // class's own registered constructor. createInstance() returning
        // nothing (no matching constructor registered - e.g. Rect, which
        // is never meant to be built this way in the first place, see
        // TypedProperty::read()'s isAddressable()/stack-local branches)
        // leaves instancePtr null rather than crashing on
        // std::any_cast<T*> against an empty std::any.
        //
        // Polymorphic dispatch: beginObject() below already resolves and
        // returns the *concrete* Class the data's own "type" tag names
        // (ObjectReader::beginObject(), reflectionio.h) - only used here
        // when actually constructing fresh (an already-live outInstance is
        // already the correct, specific type its owner allocated it as -
        // nothing to swap) and when that resolved class is a genuine
        // registered subclass of T (walked via parentClass() below - a
        // stray/bogus "type" tag naming some unrelated class is ignored,
        // falling back to this class's own T, same as if beginObject() had
        // resolved nothing at all). Without this, e.g. a Shape-typed
        // collection element whose data says "type": "Circle" would always
        // try to construct exactly Shape (abstract - createInstance()
        // fails outright) and would only ever read Shape's own properties
        // even if it could - Circle's centerX/centerY/radius would never
        // be reached. clazz->createInstance(&raw)/allProperties()/
        // allFields() (not this->...) is what actually reaches the
        // concrete type's own constructor and full property/field set;
        // see Constructor::invoke(args, outRaw)'s own comment for why a
        // raw void* (not std::any_cast<T*>) is what crosses this
        // particular boundary - clazz might not be T here.
        void read(ClassReader* reader, const std::string& propertyName, std::any& outInstance,
                    bool& instanceOnHeap, void** outRawInstance = nullptr) const override {
            // Mirrors write()'s own beginObject()/endObject() bracketing
            // above - was missing entirely before this pass, which meant a
            // reader had no hook to descend into the propertyName-keyed
            // sub-object at all before reading its properties (see
            // ClassReader::beginObject()'s own doc comment for what a real
            // implementation is expected to do with propertyName).
            const Class* clazz = reader->beginObject(propertyName);

            bool useResolvedClazz = false;
            if (clazz != nullptr && clazz != static_cast<const Class*>(this) && !outInstance.has_value()) {
                for (const Class* c = clazz; c != nullptr; c = c->parentClass()) {
                    if (c == static_cast<const Class*>(this)) {
                        useResolvedClazz = true;
                        break;
                    }
                }
            }
            if (!useResolvedClazz) {
                clazz = this;
            }

            void* instancePtr = nullptr;

            if (outInstance.has_value()) {
                instancePtr = std::any_cast<T*>(outInstance);
            } else {
                void* raw = nullptr;
                outInstance = clazz->createInstance(&raw);
                if (raw == nullptr) {
                    if (outRawInstance) {
                        *outRawInstance = nullptr;
                    }
                    reader->endObject(propertyName, this);
                    return;
                }
                instancePtr = raw;
                instanceOnHeap = true;
            }

            std::vector<const Property*> ordered;
            clazz->allProperties(ordered);

            for (const Property* property : ordered) {
                property->read(instancePtr, reader);
            }

            // Fields - the read-side mirror of write()'s own fields loop
            // above. Public-only there because a private field was never
            // written in the first place (matching scope, not required by
            // anything on this side specifically) - there's simply no
            // data under a private field's name to read back regardless,
            // so this doesn't re-check scope() itself.
            std::vector<const Field*> orderedFields;
            clazz->allFields(orderedFields);

            for (const Field* field : orderedFields) {
                field->read(instancePtr, reader);
            }

            if (outRawInstance) {
                *outRawInstance = instancePtr;
            }

            reader->endObject(propertyName, clazz);
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
    //@reflect ignore=true
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

        ClassBuilder& derived(bool value = true) { class_->setIsDerived(value); return *this; }
        ClassBuilder& abstract(bool value = true) { class_->setIsAbstract(value); return *this; }
        ClassBuilder& isStruct(bool value = true) { class_->setIsStruct(value); return *this; }
        ClassBuilder& singleton(bool value = true) { class_->setIsSingleton(value); return *this; }

        // Links this class to its base class BaseT's already-registered
        // Class (Class::parentClass()) and marks this class derived() -
        // BaseT must have gone through its own ClassBuilder<BaseT> +
        // ReflectionRegistry::registerClass() call *before* this one runs,
        // same "register base classes before their derived classes"
        // ordering every hand-written (and, eventually, reflectgen-
        // generated - the intended future shape here is a plain
        // `.base<BaseClass>()` call alongside `.constructor<Args...>()`)
        // registration function is responsible for getting right itself;
        // nothing in ReflectionRegistry enforces or defers this. Throws
        // std::logic_error rather than silently leaving parentClass_ null
        // if BaseT isn't registered yet - same "fail loud, not silently
        // wrong" convention as Method::invoke()/Property::getAs<T>().
        //
        // For a class derived from a base that isn't (and never will be)
        // itself reflected, call derived(true) alone instead - there's
        // nothing for base<BaseT>() to usefully link to in that case.
        template<typename BaseT>
        ClassBuilder& base() {
            static_assert(std::is_base_of_v<BaseT, T>,
                "ClassBuilder<T>::base<BaseT>(): BaseT must actually be a base class of T");

            const Class* baseClass = ReflectionRegistry::getClass(std::type_index(typeid(BaseT)));
            if (!baseClass) {
                throw std::logic_error(
                    "ClassBuilder<" + demangleTypeName(typeid(T)) + ">::base<" + demangleTypeName(typeid(BaseT)) +
                    ">(): base class is not yet registered in ReflectionRegistry - register base classes "
                    "before their derived classes.");
            }

            class_->parentClass_ = baseClass;
            class_->setIsDerived(true);
            return *this;
        }

        // ValueT T::* also happens to unify with a pointer-to-member-
        // FUNCTION's underlying (qualified-function) type - e.g. deducing
        // ValueT = "bool() const" for &View::isVisible - which isn't a
        // real object type and would otherwise silently steal overload
        // resolution away from the getter/setter overload below (a
        // dedicated compile error, not a working property). Excluded
        // here so a member-function pointer only ever resolves there.
        template<typename ValueT, typename = std::enable_if_t<!std::is_function_v<ValueT>>>
        ClassBuilder& property(std::string name, Scope scope, ValueT T::* member) {
            if constexpr (detail::is_reflectable_collection_v<ValueT>) {
                class_->properties_.push_back(new TypedPropertyCollection<T, ValueT>(std::move(name), scope, member));
            } else {
                class_->properties_.push_back(new TypedProperty<T, ValueT>(std::move(name), scope, member));
            }
            return *this;
        }

        // Getter (+ optional setter) property, for a value only reachable
        // through an accessor method rather than a raw pointer-to-data-
        // member - e.g. `.property("name", Scope::Public, &View::name,
        // &View::setName)`, or getter-only for a read-only property
        // (`.property("visible", Scope::Public, &View::isVisible)`).
        // getter/setter are plain callables (an ordinary lambda, or an
        // unambiguous accessor method pointer - std::invoke handles both
        // identically); an *overloaded* accessor (a const/non-const pair,
        // e.g. ViewStyle& View::style() vs. const ViewStyle& View::style()
        // const) needs an explicit lambda here instead of a bare
        // &Class::method, since there's no target type at the point of
        // '&' for overload resolution to pick one.
        //
        // ValueT is deduced from getter's own std::invoke result - never
        // named explicitly by the caller, so a getter/setter ValueT
        // mismatch is a compile error, not a std::any_cast that only fails
        // at runtime. Three distinct shapes come out of that result type:
        //
        //   - getter returns ValueT* (e.g. `Frame* Application::getFrame()`)
        //     - an *optional* nested object. Always addressable (the
        //     pointer itself, possibly null). setter, if given, must take
        //     a ValueT* too (e.g. `void Application::setFrame(Frame*)`) -
        //     a real pointer-reassignment setter (TypedProperty's
        //     PtrSetter), not a copy-into-what's-already-there one; see
        //     TypedProperty::set()'s own comment for why that distinction
        //     matters for a future Reader.
        //   - getter returns a genuine mutable ValueT& (e.g.
        //     `ViewStyle& View::style()`) and no setter was given - a
        //     nested object that's always present. Addressable the same
        //     way a pointer-to-data-member is - see TypedProperty's
        //     RefGetter.
        //   - anything else (a by-value return, a const&-returning
        //     accessor, or any getter+setter pair regardless of what
        //     getter itself returns) - a plain read/write-by-copy
        //     property. Not addressable - see TypedProperty/
        //     TypedPropertyCollection's own address() comment for why
        //     that throws rather than guessing.
        template<typename GetterT, typename SetterT = std::nullptr_t,
                  typename = std::enable_if_t<!std::is_member_object_pointer_v<GetterT>>>
        ClassBuilder& property(std::string name, Scope scope, GetterT getter, SetterT setter = nullptr) {
            using RawResult = std::invoke_result_t<GetterT, T&>;

            if constexpr (std::is_pointer_v<RawResult>) {
                using ValueT = std::remove_pointer_t<RawResult>;
                using Prop = TypedProperty<T, ValueT>;
                typename Prop::PtrGetter ptrGetter =
                    [getter](T& self) -> ValueT* { return std::invoke(getter, self); };
                typename Prop::PtrSetter ptrSetter = nullptr;
                if constexpr (!std::is_same_v<SetterT, std::nullptr_t>) {
                    ptrSetter = [setter](T& self, ValueT* value) { std::invoke(setter, self, value); };
                }
                class_->properties_.push_back(new Prop(std::move(name), scope, std::move(ptrGetter), std::move(ptrSetter)));
            } else {
                using ValueT = std::decay_t<RawResult>;
                constexpr bool addressable =
                    std::is_lvalue_reference_v<RawResult> &&
                    !std::is_const_v<std::remove_reference_t<RawResult>> &&
                    std::is_same_v<SetterT, std::nullptr_t>;

                if constexpr (detail::is_reflectable_collection_v<ValueT>) {
                    using Coll = TypedPropertyCollection<T, ValueT>;
                    if constexpr (addressable) {
                        typename Coll::RefGetter refGetter =
                            [getter](T& self) -> ValueT& { return std::invoke(getter, self); };
                        class_->properties_.push_back(new Coll(std::move(name), scope, std::move(refGetter)));
                    } else {
                        typename Coll::Getter valueGetter =
                            [getter](T& self) -> ValueT { return std::invoke(getter, self); };
                        class_->properties_.push_back(new Coll(std::move(name), scope, std::move(valueGetter)));
                    }
                } else {
                    using Prop = TypedProperty<T, ValueT>;
                    if constexpr (addressable) {
                        typename Prop::RefGetter refGetter =
                            [getter](T& self) -> ValueT& { return std::invoke(getter, self); };
                        class_->properties_.push_back(new Prop(std::move(name), scope, std::move(refGetter)));
                    } else {
                        typename Prop::Getter valueGetter =
                            [getter](T& self) -> ValueT { return std::invoke(getter, self); };
                        typename Prop::Setter valueSetter = nullptr;
                        if constexpr (!std::is_same_v<SetterT, std::nullptr_t>) {
                            valueSetter = [setter](T& self, const ValueT& value) { std::invoke(setter, self, value); };
                        }
                        class_->properties_.push_back(new Prop(std::move(name), scope, std::move(valueGetter), std::move(valueSetter)));
                    }
                }
            }
            return *this;
        }

        // A collection reachable only through the owning object's real
        // accessor/add/remove *methods* - never a real, gettable/
        // addressable ContainerT (property()'s getter/setter overload
        // above can't cover this: its RefGetter/Getter modes both still
        // need SOME live-or-copyable ContainerT value to return, and a
        // View has no such thing for its children - only
        // childViews()/addChild()/removeChild()). getter is a single real
        // accessor method returning a real container by const reference or
        // value (e.g. `const std::vector<SubView*>& View::childViews()
        // const`) - count/index access are derived from it internally
        // (container_traits::count()/getByIndex()), never separate
        // count()/getAt() methods of the caller's own invention, so every
        // argument here is always a genuine, already-existing method on T.
        // ContainerT/ElementT are both deduced from getter's own
        // std::invoke result - never named explicitly. add/remove are
        // independently optional (nullptr for a read-only, enumerate-only
        // collection); when given, they're what actually gets called on
        // read - never a raw container mutation bypassing whatever
        // invariants the real method maintains (parent/rootView linkage,
        // layout invalidation, ...) - see PropertyCollection::add()/
        // remove()'s own comment.
        //
        // e.g. .propertyCollection("childViews", Scope::Public,
        //          &View::childViews, &View::addChild, &View::removeChild);
        template<typename GetterT, typename AddFnT = std::nullptr_t, typename RemoveFnT = std::nullptr_t>
        ClassBuilder& propertyCollection(std::string name, Scope scope, GetterT getter,
                                           AddFnT add = nullptr, RemoveFnT remove = nullptr) {
            using ContainerT = std::decay_t<std::invoke_result_t<GetterT, T&>>;
            using ElementT = typename detail::container_traits<ContainerT>::ElementT;
            using Coll = TypedPropertyCollection<T, ContainerT>;

            typename Coll::CountFn countFn = [getter](T& self) -> std::size_t {
                return detail::container_traits<ContainerT>::count(std::invoke(getter, self));
            };
            typename Coll::GetAtFn getAtFn = [getter](T& self, std::size_t i) -> ElementT {
                return detail::container_traits<ContainerT>::getByIndex(std::invoke(getter, self), i);
            };

            typename Coll::AddFn addFn = nullptr;
            if constexpr (!std::is_same_v<AddFnT, std::nullptr_t>) {
                addFn = [add](T& self, const ElementT& e) { std::invoke(add, self, e); };
            }
            typename Coll::RemoveFn removeFn = nullptr;
            if constexpr (!std::is_same_v<RemoveFnT, std::nullptr_t>) {
                removeFn = [remove](T& self, const ElementT& e) { std::invoke(remove, self, e); };
            }

            class_->properties_.push_back(new Coll(std::move(name), scope,
                std::move(countFn), std::move(getAtFn), std::move(addFn), std::move(removeFn)));
            return *this;
        }

        // The genuinely-no-single-accessor-at-all case propertyCollection()
        // above can't cover - a class whose collection has no method
        // returning the whole container in any form, only independent
        // count()/getAt(index) methods (e.g. a Sprocket with cogCount()/
        // cogAt() but no cogs()-shaped accessor - see test_reflectionio.cpp).
        // Kept as a separate, differently-named overload rather than folded
        // into propertyCollection() itself: both take nothing but generic
        // callables as their trailing arguments, so overload resolution
        // would genuinely be ambiguous for several real argument counts if
        // they shared a name (a 2-callable call could satisfy either
        // template's parameter list, for instance) - explicit beats clever
        // here. count/getAt/add/remove are otherwise identical in spirit to
        // propertyCollection()'s own count/index/add/remove wiring, just
        // sourced from four separate methods instead of one.
        //
        // e.g. .propertyCollectionByCountAndIndex("cogs", Scope::Public,
        //          &Sprocket::cogCount, &Sprocket::cogAt,
        //          &Sprocket::addCog, &Sprocket::removeCog);
        template<typename CountFnT, typename GetAtFnT, typename AddFnT = std::nullptr_t, typename RemoveFnT = std::nullptr_t>
        ClassBuilder& propertyCollectionByCountAndIndex(std::string name, Scope scope, CountFnT count, GetAtFnT getAt,
                                           AddFnT add = nullptr, RemoveFnT remove = nullptr) {
            using ElementT = std::decay_t<std::invoke_result_t<GetAtFnT, T&, std::size_t>>;
            using ContainerT = std::vector<ElementT>;
            using Coll = TypedPropertyCollection<T, ContainerT>;

            typename Coll::CountFn countFn = [count](T& self) -> std::size_t { return std::invoke(count, self); };
            typename Coll::GetAtFn getAtFn =
                [getAt](T& self, std::size_t i) -> ElementT { return std::invoke(getAt, self, i); };

            typename Coll::AddFn addFn = nullptr;
            if constexpr (!std::is_same_v<AddFnT, std::nullptr_t>) {
                addFn = [add](T& self, const ElementT& e) { std::invoke(add, self, e); };
            }
            typename Coll::RemoveFn removeFn = nullptr;
            if constexpr (!std::is_same_v<RemoveFnT, std::nullptr_t>) {
                removeFn = [remove](T& self, const ElementT& e) { std::invoke(remove, self, e); };
            }

            class_->properties_.push_back(new Coll(std::move(name), scope,
                std::move(countFn), std::move(getAtFn), std::move(addFn), std::move(removeFn)));
            return *this;
        }

        template<typename ValueT>
        ClassBuilder& propertyObj(std::string name, Scope scope, ValueT* T::* member) {
            if constexpr (detail::is_reflectable_collection_v<ValueT>) {
                class_->properties_.push_back(new TypedPropertyCollection<T, ValueT>(std::move(name), scope, member));
            }
            else {
                class_->properties_.push_back(new TypedProperty<T, ValueT>(std::move(name), scope, member));
            }
            return *this;
        }

        ClassBuilder& field(std::string name, std::type_index type, Scope scope, void* address,
                                 Field::GetFn get, Field::SetFn set) {
            class_->fields_.push_back(new Field(std::move(name), type, scope, address, get, set, PropertyFlags::Static));
            return *this;
        }

        // A static class variable - see TypedField's own comment.
        template<typename ValueT>
        ClassBuilder& field(std::string name, Scope scope, ValueT* address) {
            class_->fields_.push_back(new TypedField<ValueT>(std::move(name), scope, address));
            return *this;
        }

        // An ordinary (non-static) member variable with no accessor
        // methods at all - a real pointer-to-data-member, same as
        // property()'s own MemberPtr overload just below (the two are
        // mutually exclusive only by which overload the caller's argument
        // shape resolves to; nothing stops a class from reflecting the
        // same member as a Field here rather than a Property there - the
        // *caller* decides which vocabulary fits, see Field's own "raw
        // access vs. accessor method" comment for the intended rule of
        // thumb). Collection-detected exactly like property()'s MemberPtr
        // overload - a reflectable ContainerT (see container_traits above)
        // registers as a TypedFieldCollection instead of a plain
        // TypedMemberField.
        template<typename ValueT, typename = std::enable_if_t<!std::is_function_v<ValueT>>>
        ClassBuilder& field(std::string name, Scope scope, ValueT T::* member) {
            if constexpr (detail::is_reflectable_collection_v<ValueT>) {
                class_->fields_.push_back(new TypedFieldCollection<T, ValueT>(std::move(name), scope, member));
            } else {
                class_->fields_.push_back(new TypedMemberField<T, ValueT>(std::move(name), scope, member));
            }
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

    // T-aware, mirroring ClassBuilder<T>'s own shape (typeid(T) derived
    // internally rather than passed by the caller) - needed here
    // specifically so the constructor can install real, type-safe
    // toUInt64_/fromUInt64_ conversions on the Enum it builds (see their
    // own comments on Enum) - only the point where T is concretely known
    // can ever safely std::any_cast<T> at all. Used in exactly one place
    // outside this declaration (reflectgen.py's own generated
    // registration code), so replacing the previous untemplated,
    // conversion-less EnumBuilder outright (rather than keeping both)
    // carried no other blast radius.
    //@reflect ignore=true
    template<typename T>
    class EnumBuilder {
        static_assert(std::is_enum_v<T>, "EnumBuilder<T>: T must be a real enum type");

    public:
        // name is still the caller-supplied bare name (reflectgen.py's own
        // EnumInfo::bare_name, or a hand-written literal) rather than
        // derived from typeid(T) the way ClassBuilder<T> derives its own
        // name() - a nested enum's fully-qualified spelling isn't always
        // reconstructable from typeid(T).name() alone the way a class's
        // is (see reflectgen.py's own EnumInfo::bare_name comment).
        // namespaceName, though, is exactly what extractNamespace(typeid(T))
        // already gives Class - deriving it here the same way (rather than
        // asking the caller for a second string) is what actually makes
        // classinfo()-style qualified-name lookup possible for enums too.
        explicit EnumBuilder(std::string name) : enum_(typeid(T), std::move(name), extractNamespace(typeid(T))) {
            enum_.toUInt64_ = [](const std::any& val) -> std::uint64_t {
                using UnderlyingT = std::underlying_type_t<T>;
                return static_cast<std::uint64_t>(
                    static_cast<std::make_unsigned_t<UnderlyingT>>(
                        static_cast<UnderlyingT>(std::any_cast<T>(val))));
            };
            enum_.fromUInt64_ = [](std::uint64_t val) -> std::any {
                return std::any(static_cast<T>(static_cast<std::underlying_type_t<T>>(val)));
            };
        }

        EnumBuilder& addValue(std::string name, std::uint64_t value) {
            enum_.values_.push_back(EnumValue{std::move(name), value});
            return *this;
        }

        // Opts this enum into "flags" treatment (array of decomposed flag
        // names, ObjectWriter/ObjectReader - reflectionio.h) - always
        // explicit, never guessed from the enum's own values/name/
        // operators - see Enum::isFlags()'s own comment for the real,
        // concrete counter-examples (this codebase's own DialogResult,
        // Anchor, KeyboardMasks, ...) that ruled out every shape-based
        // heuristic considered.
        EnumBuilder& flags(bool value = true) {
            enum_.isFlags_ = value;
            return *this;
        }

        Enum build() { return std::move(enum_); }

    private:
        Enum enum_;
    };

    // Process-wide registry of every reflected Class/Enum, keyed by
    // std::type_index (and, secondarily, by name for lookups that only have
    // a string - e.g. a save/load format or a script binding). Reflection
    // works on any class, not just the View/ViewStyle/Layout hierarchy -
    // same Meyer's-singleton shape as Bundle/ThemeData otherwise.
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
    //@reflect ignore=true
    class ReflectionRegistry {
    public:
        ReflectionRegistry(const ReflectionRegistry&) = delete;
        ReflectionRegistry& operator=(const ReflectionRegistry&) = delete;

        using InitFunc = std::function<void()>;
        

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

        static void addInitFunction(InitFunc initFunc);

        static void init();
    private:
        ReflectionRegistry() = default;
        ~ReflectionRegistry();

        static ReflectionRegistry& instance();

        std::unordered_map<std::type_index, Class*> classesByType_;
        std::unordered_map<std::string, std::type_index> classNameToType_;

        std::unordered_map<std::type_index, Enum> enumsByType_;
        std::unordered_map<std::string, std::type_index> enumNameToType_;

        std::vector<InitFunc> initList_;
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
