#pragma once

#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <newui/delegate.h>

namespace newui {

    // What Property<SourceT, ValueT> (and PropertyManager::registerProperty())
    // accept for ValueT: a scalar (int, float, ...) or a POD struct/class
    // built out of them (e.g. newui::Point) - trivially copyable (safe to
    // copy/compare/destroy byte-wise; no user-defined copy/move/destructor)
    // and standard-layout (no virtual functions/bases, uniform member
    // access). Deliberately std::is_trivially_copyable rather than the
    // stricter std::is_trivial: newui's own value types (Point, Size, Rect,
    // ...) give their members default member initializers
    // ("float x = 0.0f;"), which makes their default constructor non-
    // trivial without affecting copy/comparison safety - is_trivial would
    // wrongly reject them here. Named separately from is_arithmetic since
    // only the latter is accepted by Property<SourceT, ValueT>::interpolate(t)
    // and interpolate(t, keyframes) - see their comments below.
    template<typename ValueT>
    struct IsPodLike : std::integral_constant<bool,
        std::is_arithmetic<ValueT>::value
            || (std::is_trivially_copyable<ValueT>::value && std::is_standard_layout<ValueT>::value)> {};

    // Selects how Property<SourceT, ValueT>::interpolate() maps its [0,1] t
    // parameter onto the [start,end] range set by setupInterpolation().
    enum class InterpolationKind {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
    };

    // Non-template base so PropertyManager can hold Property<Foo, int>,
    // Property<Bar, float>, etc. in a single collection. Property<SourceT,
    // ValueT> below is the type client code actually constructs and uses;
    // this only has what's left once ValueT/SourceT are erased - name(),
    // the untyped source() (see Property::typedSource() for the typed
    // version), and interpolate(t) (the one curve shape that doesn't need
    // ValueT/SourceT known at the call site - see Property::interpolate()).
    class PropertyBase {
    public:
        PropertyBase(const std::string& name, void* source) : name_(name), source_(source) {}
        virtual ~PropertyBase() = default;

        const std::string& name() const {
            return name_;
        }

        void* source() const {
            return source_;
        }

        // Writes the value at t (see setupInterpolation()) into the bound
        // field. t is expected to be in [0,1]; values outside that range
        // extrapolate rather than clamp.
        virtual void interpolate(float t) = 0;

    protected:
        std::string name_;
        void* source_ = nullptr;
    };

    // Binds a single POD scalar or POD struct field (int, float,
    // newui::Point, ...) of type ValueT on *source (of type SourceT), read
    // and written through get()/set(). Normally created and owned via
    // PropertyManager::registerProperty() rather than directly.
    //
    // Templated on both ValueT and SourceT (rather than just ValueT) so
    // onValueChanged fires with a fully-typed Property<SourceT, ValueT>&
    // as its Sender - unlike a Delegate<PropertyBase> callback, one here
    // never needs to static_cast either the property (to call get()) or
    // its source (via typedSource(), the strongly-typed counterpart to
    // PropertyBase::source()'s void*).
    //
    // The built-in curve overloads of interpolate() - the
    // InterpolationKind-based one and the keyframe-vector one - only
    // support scalar ValueT, since they interpolate by doing arithmetic
    // directly on it; interpolate(t, fn) works for struct ValueT too,
    // since the caller-supplied fn does the actual blending (e.g.
    // componentwise).
    template<typename SourceT, typename ValueT>
    class ObservableProperty : public PropertyBase {
        static_assert(IsPodLike<ValueT>::value,
            "Property<SourceT, ValueT> only supports POD scalar types (int, float, ...) or "
            "POD structs made up of them (e.g. newui::Point) for ValueT.");

    public:
        // Given the start/end set by setupInterpolation() and t, returns
        // the value to write - see the interpolate(t, fn) overload below.
        using InterpolationFunction = std::function<ValueT(ValueT start, ValueT end, float t)>;

        // Convenience aliases for this Property's own type and the
        // pointer/reference shapes its API traffics in - used below and
        // in ValueChangedDelegate's callback signature.
        typedef ObservableProperty<SourceT, ValueT> PropertyT;
        typedef SourceT* SourcePtr;
        typedef ValueT* ValuePtr;
        typedef ValueT& ValueRef;
        typedef const ValueT& ConstValueRef;

        // Fired by set() (and so by interpolate(), which writes through
        // set()) once the bound field's value has actually changed.
        // Sender is this concrete Property<SourceT, ValueT>, not
        // PropertyBase, and the callback also receives the source
        // instance and a pointer to the (already updated) field directly,
        // so it never needs to call typedSource()/get() itself, let alone
        // static_cast anything.
        typedef Delegate<PropertyT, SourcePtr, ValuePtr> ValueChangedDelegate;
        ValueChangedDelegate onValueChanged;

        // The source instance this property's field belongs to, strongly
        // typed - a cast-free alternative to PropertyBase::source()'s
        // void*.
        SourcePtr typedSource() const {
            return static_cast<SourcePtr>(source_);
        }

        // Returns a reference, not a copy, so reading a struct ValueT
        // (Point, Color, ...) doesn't pay for copying every field just to
        // look at it.
        ConstValueRef get() const {
            return *field_;
        }

        // Writes value into the bound field and fires onValueChanged - but
        // only if value actually differs from the field's current value
        // (matching e.g. SubView::setBounds()'s onSizeChanged).
        void set(ConstValueRef value) {
            if (*field_ == value) {
                return;
            }

            *field_ = value;
            onValueChanged(*this, typedSource(), field_);
        }

        // Establishes the range interpolate() maps its t input onto. Does
        // not itself touch the field - call interpolate() to do that.
        void setupInterpolation(ValueT start, ValueT end, InterpolationKind kind = InterpolationKind::Linear) {
            interpStart_ = start;
            interpEnd_ = end;
            interpKind_ = kind;
        }

        // Maps t through the InterpolationKind set by setupInterpolation()
        // and writes the resulting value via set() - so onValueChanged
        // still only fires when that write actually changes the field.
        // Only supported for scalar ValueT (this does the blend by direct
        // arithmetic on it); throws for struct ValueT - use
        // interpolate(t, fn) instead, which leaves the actual blending up
        // to the caller.
        void interpolate(float t) override {
            if constexpr (std::is_arithmetic<ValueT>::value) {
                float eased = ease(t, interpKind_);
                double value = static_cast<double>(interpStart_)
                    + (static_cast<double>(interpEnd_) - static_cast<double>(interpStart_)) * eased;
                set(static_cast<ValueT>(value));
            } else {
                throw std::logic_error(
                    "Property<SourceT, ValueT>::interpolate(t): built-in InterpolationKind curves "
                    "require a scalar ValueT; use interpolate(t, fn) for struct field types.");
            }
        }

        // Interpolates using fn instead of the InterpolationKind set by
        // setupInterpolation() - start/end still come from whatever
        // setupInterpolation() call set them, only the shaping curve is
        // replaced. Unlike Delegate (which only takes plain function
        // pointers so it can publish lock-free snapshots), this accepts any
        // callable - including a capturing lambda - since it's a single
        // direct call rather than a multicast list.
        void interpolate(float t, const InterpolationFunction& fn) {
            set(fn(interpStart_, interpEnd_, t));
        }

        // Interpolates using keyframes directly, ignoring whatever start/end
        // setupInterpolation() established: keyframes[0] is the value at
        // t=0, keyframes.back() is the value at t=1, and entries in between
        // are spaced evenly across [0,1]. t selects the two keyframes it
        // falls between and linearly interpolates across them (t outside
        // [0,1] extrapolates along whichever end segment it falls beyond).
        // keyframes must have at least one entry. Only available for scalar
        // ValueT (each keyframe is a single float); there's no generic way
        // to build a struct ValueT out of one, so this overload doesn't
        // exist for struct ValueT rather than failing at runtime - use
        // interpolate(t, fn) instead.
        template<typename U = ValueT, typename = typename std::enable_if<std::is_arithmetic<U>::value>::type>
        void interpolate(float t, const std::vector<float>& keyframes) {
            if (keyframes.empty()) {
                return;
            }

            if (keyframes.size() == 1) {
                set(static_cast<ValueT>(keyframes[0]));
                return;
            }

            int segmentCount = static_cast<int>(keyframes.size()) - 1;
            float scaled = t * static_cast<float>(segmentCount);

            int index = static_cast<int>(std::floor(scaled));
            if (index < 0) {
                index = 0;
            } else if (index > segmentCount - 1) {
                index = segmentCount - 1;
            }

            float frac = scaled - static_cast<float>(index);
            float value = keyframes[index] + (keyframes[index + 1] - keyframes[index]) * frac;
            set(static_cast<ValueT>(value));
        }

    private:
        // Only PropertyManager::registerProperty() may construct a
        // Property - it's the sole owner of every Property's lifetime
        // (see PropertyManager's class comment), so there's no such thing
        // as a Property that isn't tracked by one.
        friend class PropertyManager;

        ObservableProperty(const std::string& name, SourceT* source, ValueT* field)
            : PropertyBase(name, source), field_(field) {}

        static float ease(float t, InterpolationKind kind) {
            switch (kind) {
                case InterpolationKind::EaseIn:
                    return t * t;
                case InterpolationKind::EaseOut:
                    return t * (2.0f - t);
                case InterpolationKind::EaseInOut:
                    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
                case InterpolationKind::Linear:
                default:
                    return t;
            }
        }

        ValueT* field_;
        ValueT interpStart_{};
        ValueT interpEnd_{};
        InterpolationKind interpKind_ = InterpolationKind::Linear;
    };

    // Owns every Property in the process, keyed by (name, source pointer)
    // so the same field name can be reused across different source
    // instances - see registerProperty(). A Property is only ever reached
    // through here: its constructor is private, friended only to this
    // class, so there's no way to create one without it being tracked.
    // Singleton (see instance()) rather than freely constructible, since
    // that tracking needs to be process-wide - two independent
    // PropertyManagers could each think they exclusively own the (name,
    // source) keyspace and silently clash. removeProperty() (or clear())
    // deletes the Property(s) it removes.
    class PropertyManager {
    public:
        PropertyManager(const PropertyManager&) = delete;
        PropertyManager& operator=(const PropertyManager&) = delete;
        PropertyManager(PropertyManager&&) = delete;
        PropertyManager& operator=(PropertyManager&&) = delete;

        static PropertyManager& instance() {
            static PropertyManager manager;
            return manager;
        }

        // Creates a Property<SourceT, ValueT> binding name on *source to
        // *field, stores it (owned by this PropertyManager) keyed by
        // (name, source), and returns a pointer to it. Registering the
        // same (name, source) pair again replaces (and deletes) whatever
        // was registered before. ValueT and SourceT are both deduced from
        // source/field - see the std::nullptr_t overload below for
        // registerProperty(nullptr, field, name), where there's no
        // meaningful source type to deduce.
        template<typename SourceT, typename ValueT>
        static ObservableProperty<SourceT, ValueT>* registerProperty(SourceT* source, ValueT* field, const std::string& name) {
            static_assert(IsPodLike<ValueT>::value,
                "PropertyManager::registerProperty only supports POD scalar types (int, float, ...) "
                "or POD structs made up of them (e.g. newui::Point) for the field type.");
            auto& pm = PropertyManager::instance();

            ObservableProperty<SourceT, ValueT>* property = new ObservableProperty<SourceT, ValueT>(name, source, field);
            Key key{ name, static_cast<void*>(source) };

            auto it = pm.properties_.find(key);
            if (it != pm.properties_.end()) {
                delete it->second;
                it->second = property;
            }
            else {
                pm.properties_.emplace(std::move(key), property);
            }

            return property;
        }

        // registerProperty(nullptr, field, name) - std::nullptr_t doesn't
        // match the SourceT* deduction above (nullptr isn't "a pointer to
        // SourceT" for any SourceT), so a source-less property (no
        // meaningful owning instance) needs this explicit overload;
        // SourceT is void, and typedSource() degenerates to the same thing
        // as PropertyBase::source().
        template<typename ValueT>
        static ObservableProperty<void, ValueT>* registerProperty(std::nullptr_t, ValueT* field, const std::string& name) {
            return registerProperty<void, ValueT>(static_cast<void*>(nullptr), field, name);
        }

        // Returns the Property registered for (source, name), or nullptr
        // if none has been registered (or it was already removed).
        static PropertyBase* getProperty(void* source, const std::string& name);

        // Deletes the Property registered for (source, name), if any.
        static void removeProperty(void* source, const std::string& name);

        // Deletes every registered Property and forgets about all of
        // them. Not part of normal application use - this exists for
        // test isolation, so each test can start from a clean
        // PropertyManager despite it being a process-wide singleton (see
        // the class comment) rather than a fresh instance per test.
        static void clear();

    private:
        PropertyManager() = default;
        ~PropertyManager();

        struct Key {
            std::string name;
            void* source;

            bool operator==(const Key& other) const {
                return source == other.source && name == other.name;
            }
        };

        struct KeyHash {
            std::size_t operator()(const Key& key) const {
                return std::hash<std::string>()(key.name) ^ (std::hash<void*>()(key.source) << 1);
            }
        };

        std::unordered_map<Key, PropertyBase*, KeyHash> properties_;
    };

}
