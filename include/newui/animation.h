#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <newui/animationframe.h>
#include <newui/property.h>
#include <newui/runloop.h>

namespace newui {

    // One (Property, value) pair inside a Key - the concrete value that
    // property should hold once that Key becomes active. Non-template base
    // so Key can hold entries for Property<int, Foo>, Property<float, Bar>,
    // ... in a single list; TypedKeyValue<SourceT, ValueT> below is what
    // actually gets constructed (see Key::setValue()).
    class KeyValue {
    public:
        virtual ~KeyValue() = default;

        virtual PropertyBase* property() const = 0;

        // Which curve interpolateFrom() blends into this value with - see
        // Property<SourceT, ValueT>::setupInterpolation()'s kind parameter,
        // which this is passed straight through to. Only affects
        // arithmetic property types (see interpolateFrom()); ignored for
        // struct types, which always step regardless. Defaults to Linear -
        // set via the kind parameter on Key::setValue(), or here directly
        // to change it after the fact.
        InterpolationKind interpolationKind() const {
            return interpolationKind_;
        }

        void setInterpolationKind(InterpolationKind kind) {
            interpolationKind_ = kind;
        }

        // Writes this KeyValue's value straight into its property. This is
        // the only thing possible for a property type that can't be
        // smoothly interpolated (see interpolateFrom()), and also what
        // interpolateFrom() itself does once t reaches 1 or there's no
        // predecessor to blend from.
        virtual void apply() const = 0;

        // Interpolates from `from` (the same property's KeyValue at the
        // previous Key that set it, or nullptr if no earlier Key does) to
        // this value, at t in [0,1], writing the result into the bound
        // field. If a custom interpolation function was set (see
        // TypedKeyValue<SourceT, ValueT>::setInterpolationFunction()),
        // that's used - this is the only way a struct property type
        // (Point, Color, ...) interpolates smoothly, since it puts the
        // actual blending in the caller's hands instead of needing this
        // class to know how. Otherwise, scalar (arithmetic) property types
        // fall back to interpolationKind()'s built-in curve - see
        // Property<SourceT, ValueT>::interpolate(t) - and anything else (a
        // struct with no custom function) has no generic way to blend at
        // all, so it just steps straight to its value once t reaches 1
        // and otherwise holds whatever the previous Key left it at.
        //
        // from, if not null, must wrap the same property (and therefore
        // the same ValueT/SourceT) as this KeyValue - Animation::
        // processFrame() only ever pairs up KeyValues it found via the
        // same PropertyBase*, so this holds by construction.
        virtual void interpolateFrom(const KeyValue* from, float t) const = 0;

        // Type-erased read access to this KeyValue's stored value - ValueT
        // is always IsPodLike (property.h), so copying it into a std::any
        // is always valid. Used by Bundle::writeFrame() to write a saved
        // Animation's key values without needing SourceT/ValueT itself
        // (see AnimationTargetRegistry, below, for the read-side mirror).
        virtual std::any boxedValue() const = 0;

    protected:
        InterpolationKind interpolationKind_ = InterpolationKind::Linear;
    };

    template<typename SourceT, typename ValueT>
    class TypedKeyValue : public KeyValue {
    public:
        // Given the start/end this KeyValue is interpolating between and
        // t, returns the value to write - see setInterpolationFunction().
        using InterpolationFunction = typename ObservableProperty<SourceT, ValueT>::InterpolationFunction;

        TypedKeyValue(ObservableProperty<SourceT, ValueT>* property, ValueT value,
                InterpolationKind kind = InterpolationKind::Linear)
            : property_(property), value_(value) {
            interpolationKind_ = kind;
        }

        TypedKeyValue(ObservableProperty<SourceT, ValueT>* property, ValueT value, InterpolationFunction fn)
            : property_(property), value_(value), interpolationFunction_(std::move(fn)) {}

        ObservableProperty<SourceT, ValueT>* typedProperty() const {
            return property_;
        }

        const ValueT& value() const {
            return value_;
        }

        // A custom curve to interpolate into this value with, overriding
        // interpolationKind() - see interpolateFrom(). Unlike Delegate
        // (limited to plain function pointers so it can publish lock-free
        // snapshots), this accepts any callable, including a capturing
        // lambda, since it's a single direct call rather than a
        // multicast list - same as Property<SourceT, ValueT>::
        // interpolate(t, fn), which this is passed straight through to.
        // Pass an empty std::function to fall back to interpolationKind()
        // again.
        void setInterpolationFunction(InterpolationFunction fn) {
            interpolationFunction_ = std::move(fn);
        }

        bool hasInterpolationFunction() const {
            return static_cast<bool>(interpolationFunction_);
        }

        PropertyBase* property() const override {
            return property_;
        }

        std::any boxedValue() const override {
            return std::any(value_);
        }

        void apply() const override {
            property_->set(value_);
        }

        void interpolateFrom(const KeyValue* from, float t) const override {
            if (from == nullptr || t >= 1.0f) {
                apply();
                return;
            }

            const ValueT& startValue = static_cast<const TypedKeyValue<SourceT, ValueT>*>(from)->value_;
            property_->setupInterpolation(startValue, value_, interpolationKind_);

            if (interpolationFunction_) {
                // A custom curve was supplied - this works for any
                // ValueT, struct types included, since the caller does
                // the actual blending (see properties1.cpp's Demo 5/6
                // for the pattern - componentwise lerp for Point/Color).
                property_->interpolate(t, interpolationFunction_);
            } else if constexpr (std::is_arithmetic<ValueT>::value) {
                property_->interpolate(t);
            } else {
                // Non-arithmetic ValueT with no custom function: no
                // generic way to blend an arbitrary struct, so step
                // instead - hold at `from`'s value until t reaches 1.
                // Re-applying `from` on every mid-segment call (rather
                // than leaving the field alone) is what makes
                // processFrame() correct for a direct/out-of-order call -
                // e.g. seeking straight to a frame mid-animation - not
                // just for sequential playback that happened to already
                // apply `from` on an earlier call.
                from->apply();
            }
        }

    private:
        ObservableProperty<SourceT, ValueT>* property_;
        ValueT value_;
        InterpolationFunction interpolationFunction_;
    };

    // One keyframe in an Animation: a point in time (keyFrame, in whole
    // frames, relative to the owning Animation's startTime() - see
    // Animation) at which a named set of Properties should hold specific
    // values. Properties this Key doesn't mention are left alone by it -
    // see Animation::processFrame().
    class Key {
    public:
        Key() = default;
        Key(std::string name, std::uint64_t keyFrame) : name_(std::move(name)), keyFrame_(keyFrame) {}

        const std::string& name() const {
            return name_;
        }

        void setName(const std::string& name) {
            name_ = name;
        }

        std::uint64_t keyFrame() const {
            return keyFrame_;
        }

        void setKeyFrame(std::uint64_t keyFrame) {
            keyFrame_ = keyFrame;
        }

        // Sets the value property should hold once this Key becomes
        // active, replacing whatever value this Key already had for it
        // (if any). kind selects the curve used to blend into value from
        // the previous Key that sets property (see
        // KeyValue::interpolateFrom()) - defaults to Linear, and only
        // matters for arithmetic property types; struct-typed properties
        // always step regardless of kind unless a custom interpolation
        // function is set too (via the overload below, or
        // TypedKeyValue<SourceT, ValueT>::setInterpolationFunction() on
        // the returned KeyValue - see findValue()). ValueT and SourceT
        // are both deduced from property.
        template<typename SourceT, typename ValueT>
        void setValue(ObservableProperty<SourceT, ValueT>* property, ValueT value,
                InterpolationKind kind = InterpolationKind::Linear) {
            for (auto& entry : values_) {
                if (entry->property() == property) {
                    entry = std::make_unique<TypedKeyValue<SourceT, ValueT>>(property, value, kind);
                    return;
                }
            }
            values_.push_back(std::make_unique<TypedKeyValue<SourceT, ValueT>>(property, std::move(value), kind));
        }

        // Same as above, but blends into value using a custom curve (fn)
        // instead of one of the built-in InterpolationKind shapes -
        // see TypedKeyValue<SourceT, ValueT>::setInterpolationFunction().
        // This is the way to smoothly interpolate a struct-typed property
        // (Point, Color, ...) between Keys, since fn - not this class -
        // does the actual componentwise blending.
        template<typename SourceT, typename ValueT>
        void setValue(ObservableProperty<SourceT, ValueT>* property, ValueT value,
                typename TypedKeyValue<SourceT, ValueT>::InterpolationFunction fn) {
            for (auto& entry : values_) {
                if (entry->property() == property) {
                    entry = std::make_unique<TypedKeyValue<SourceT, ValueT>>(property, value, std::move(fn));
                    return;
                }
            }
            values_.push_back(
                std::make_unique<TypedKeyValue<SourceT, ValueT>>(property, std::move(value), std::move(fn)));
        }

        // Not reflectgen-registered as a collection - unlike Animation::
        // keys() just below (a real, single, reflectable Key class),
        // every element here is a TypedKeyValue<SourceT,ValueT> - one
        // concrete C++ type per (source, value) type pair actually used,
        // never itself a registered reflection::Class (there's no way to
        // enumerate every SourceT/ValueT combo an app might ever use).
        // TypedPropertyCollection::writeItem()'s runtime-type lookup
        // (classinfo(typeid(*elementPtr))) would find nothing for any real
        // element here - Bundle::writeFrame() reads this vector directly
        // in C++ instead, writing each KeyValue's target descriptor +
        // boxedValue() by hand (see AnimationTargetRegistry, below, for
        // the read-side mirror).
        // Inserts an already-built KeyValue directly, replacing any
        // existing entry for the same property() - the non-templated
        // counterpart to setValue<SourceT,ValueT>() above, for a caller
        // that built a KeyValue without SourceT/ValueT of its own to name
        // (AnimationTargetRegistry::buildKeyValue(), property.h/
        // animation.h - see Bundle::loadFrame()'s own "animations" block,
        // bundle.cpp, reconstructing a saved Animation).
        void addRawValue(std::unique_ptr<KeyValue> value) {
            PropertyBase* property = value->property();
            for (auto& entry : values_) {
                if (entry->property() == property) {
                    entry = std::move(value);
                    return;
                }
            }
            values_.push_back(std::move(value));
        }

        const std::vector<std::unique_ptr<KeyValue>>& values() const {
            return values_;
        }

        // Returns this Key's KeyValue for property, or nullptr if this Key
        // doesn't set it.
        KeyValue* findValue(PropertyBase* property) const {
            for (auto& entry : values_) {
                if (entry->property() == property) {
                    return entry.get();
                }
            }
            return nullptr;
        }

    private:
        std::string name_;
        std::uint64_t keyFrame_ = 0;
        std::vector<std::unique_ptr<KeyValue>> values_;
    };

    // A sequence of Keys played back over the frame range
    // [startTime, startTime + duration) - both in whole frames, at
    // whatever rate the owning AnimationManager::frameRate() is driving
    // playback at. processFrame(frame) finds the two Keys bracketing
    // frame - startTime() (by Key::keyFrame(), kept sorted ascending by
    // addKey()) and, for every property either one of them sets,
    // interpolates between the earlier Key's value and the later Key's
    // value - see KeyValue::interpolateFrom().
    class Animation {
    public:
        Animation() = default;
        Animation(std::string name, std::uint64_t startTime, std::uint64_t duration)
            : name_(std::move(name)), startTime_(startTime), duration_(duration) {}

        const std::string& name() const {
            return name_;
        }

        void setName(const std::string& name) {
            name_ = name;
        }

        std::uint64_t startTime() const {
            return startTime_;
        }

        void setStartTime(std::uint64_t startTime) {
            startTime_ = startTime;
        }

        std::uint64_t duration() const {
            return duration_;
        }

        void setDuration(std::uint64_t duration) {
            duration_ = duration;
        }

        std::uint64_t endTime() const {
            return startTime_ + duration_;
        }

        // Whether frame (an absolute frame number, same units as
        // startTime()) falls within [startTime(), endTime()], inclusive of
        // both ends - endTime() (= startTime() + duration()) is where the
        // last Key at local frame duration() lives, so excluding it would
        // mean that Key's value never actually gets applied.
        bool isActiveAt(std::uint64_t frame) const {
            return frame >= startTime_ && frame <= endTime();
        }

        // Whether AnimationManager::processIdle() should wrap this
        // Animation's playback back to startTime() once it passes
        // endTime(), indefinitely, instead of holding endTime()'s Key
        // values forever (the non-looping default - see processIdle()'s
        // own comment on how the two modes differ). processFrame() itself
        // knows nothing about looping - it always interpolates whatever
        // absolute frame it's given, clamped to [startTime(), endTime()]
        // the same way regardless of this flag; looping is entirely
        // processIdle() feeding it a wrapped frame number instead of the
        // raw elapsed one.
        bool looping() const {
            return looping_;
        }

        void setLooping(bool looping) {
            looping_ = looping;
        }

        // Creates a Key, owned by this Animation, and returns a pointer to
        // it - stable for the Animation's lifetime regardless of further
        // addKey() calls (unlike a plain vector<Key> element, which
        // reallocation could invalidate). Keeps keys() sorted by
        // Key::keyFrame() ascending, so processFrame() can assume that
        // ordering.
        Key* addKey(const std::string& name, std::uint64_t keyFrame);

        const std::vector<std::unique_ptr<Key>>& keys() const {
            return keys_;
        }

        // Interpolates every property this Animation's Keys set to its
        // value at frame (an absolute frame number, same units as
        // startTime()). Frames at or before the first Key, or at or after
        // the last, hold that Key's values rather than extrapolating. A
        // no-op if this Animation has no Keys at all.
        void processFrame(std::uint64_t frame);

    private:
        std::string name_;
        std::uint64_t startTime_ = 0;
        std::uint64_t duration_ = 0;
        bool looping_ = false;
        std::vector<std::unique_ptr<Key>> keys_;
    };

    // Owns a set of Animations and drives them forward using RunLoop idle
    // time: run(runLoop) registers processIdle() with runLoop.postIdle()
    // so it's called every idle pass for the lifetime of runLoop. Each
    // call checks how much wall-clock time has elapsed since playback
    // started and, once that advances currentFrame() (via
    // AnimationFrame::setFromElapsed() - see processIdle()), calls
    // Animation::processFrame() on every registered Animation that's
    // active at the new frame.
    //
    // Singleton (see instance()) rather than freely constructible - a
    // process only has one playback clock/idle-driven frame counter to
    // speak of, and two independent AnimationManagers would each try to
    // drive their own currentFrame() from the same wall clock
    // independently, which isn't a meaningful thing to do.
    class AnimationManager {
    public:
        AnimationManager(const AnimationManager&) = delete;
        AnimationManager& operator=(const AnimationManager&) = delete;
        AnimationManager(AnimationManager&&) = delete;
        AnimationManager& operator=(AnimationManager&&) = delete;

        static AnimationManager& instance() {
            static AnimationManager manager;
            return manager;
        }

        // Fired by processIdle() with the new currentFrame() value every
        // time playback actually advances by at least one whole frame -
        // exactly the condition every registered Animation gets
        // (re-)checked against (see processIdle()'s own comment). A UI
        // component hosting animated state (e.g. a View painting a
        // ShapeLayer whose shapes get synced from Property values each
        // frame - see examples/shapes2.cpp) can subscribe here to know
        // when to resync and repaint itself, instead of separately
        // polling currentFrame() on every RunLoop idle pass to notice
        // when it's changed. Delegate only accepts plain function
        // pointers, not a capturing lambda (see Delegate's own class
        // comment, delegate.h) - a subscriber that needs to reach a
        // specific instance (a specific View, a specific set of Shapes)
        // needs that reachable some other way (a singleton, a static/
        // file-local pointer, ...) rather than captured directly here.
        Delegate<AnimationManager, std::uint64_t> onFrameChanged;

        static FrameRate frameRate()  {
            return AnimationManager::instance().currentFrame_.framerate();
        }

        static void setFrameRate(FrameRate frameRate) {
            AnimationManager::instance().currentFrame_.setFramerate(frameRate);
        }

        // Current frame number, advanced by processIdle() at frameRate()
        // frames per second based on elapsed wall-clock time since
        // playback started (the first processIdle() call).
        static std::uint64_t currentFrame() {
            return AnimationManager::instance().currentFrame_.value();
        }

        // Creates an Animation, owned by this AnimationManager, and
        // returns a pointer to it - stable for the manager's lifetime
        // regardless of further addAnimation() calls.
        static Animation* addAnimation(const std::string& name, std::uint64_t startTime, std::uint64_t duration);

        // Every currently-registered Animation - added purely for
        // Bundle::writeFrame() (bundle.cpp) to filter over when deciding
        // which animations to persist alongside a given Frame; nothing
        // else needs to enumerate them (contrast addAnimation()/
        // removeAnimation(), the only ways animations_ is normally
        // touched).
        static const std::vector<std::unique_ptr<Animation>>& animations() {
            return AnimationManager::instance().animations_;
        }

        // Removes and destroys animation, if it's registered with this
        // manager.
        static void removeAnimation(Animation* animation);

        // Registers this manager's idle processing with runLoop - see
        // processIdle(). The returned idle task never reports itself
        // done, so it keeps running for as long as runLoop does.
        static void addToRunLoop(RunLoop& runLoop) {
            
            runLoop.postIdle([]() {                 
                return AnimationManager::processIdle();
               });
        }

        // Advances currentFrame() by however many whole frames have
        // elapsed (at frameRate() fps) since the last call that actually
        // advanced it, then calls Animation::processFrame(currentFrame())
        // on every registered Animation active at that frame. A no-op
        // call (not enough wall-clock time has passed yet for the next
        // frame) does neither. Always returns false - see run() - so a
        // manager driven via RunLoop::postIdle() keeps being called for
        // as long as the loop runs, not just until the first frame.
        static bool processIdle();

        // Removes every registered Animation and resets playback (frame
        // rate, current frame, and the started-clock state) back to a
        // freshly-constructed AnimationManager's. Not part of normal
        // application use - this exists for test isolation, so each test
        // can start from a clean AnimationManager despite it being a
        // process-wide singleton (see the class comment) rather than a
        // fresh instance per test.
        static void clear();

    private:
        AnimationManager() = default;

        std::vector<std::unique_ptr<Animation>> animations_;
        AnimationFrame currentFrame_;
        std::chrono::steady_clock::time_point clockStart_;
        bool started_ = false;
    };

    // Bridges a saved (sourceType, valueType, propertyName, resolved live
    // source, boxed value) tuple - everything Bundle::loadFrame() has
    // after resolving a saved target's view-path (viewpath.h) - back into
    // a real, working ObservableProperty<SourceT,ValueT>/
    // TypedKeyValue<SourceT,ValueT> pair, the same shape hand-written code
    // already builds via PropertyManager::registerProperty<ValueT>(source,
    // propertyName) + Key::setValue(property, value).
    //
    // SourceT/ValueT can't be recovered from a std::type_index alone -
    // there's no runtime "construct me a T" for an arbitrary C++ type -
    // so registerTarget<SourceT,ValueT>() closes over both once, at
    // registration time, into a small type-erased factory keyed by the
    // (sourceType, valueType) pair; buildKeyValue() below just looks that
    // factory up and calls it. Same explicit, hand-maintained opt-in idiom
    // @reflect/NEWUI_REFLECT_PRIVATE already use elsewhere in this
    // codebase - an app registers every (SourceT, ValueT) combination it
    // wants a persisted Animation able to target, once (e.g. alongside its
    // own registerReflectionData() call), rather than this trying to
    // auto-discover them.
    class AnimationTargetRegistry {
    public:
        template<typename SourceT, typename ValueT>
        static void registerTarget() {
            AnimationTargetRegistry& reg = instance();
            TypeKey key{ std::type_index(typeid(SourceT)), std::type_index(typeid(ValueT)) };
            reg.factories_[key] = [](void* source, const std::string& propertyName,
                                       const std::any& boxedValue, InterpolationKind kind) -> std::unique_ptr<KeyValue> {
                // get-or-register, never a fresh registerProperty() every
                // call - PropertyManager::registerProperty() *replaces*
                // (deletes) whatever was already registered for the same
                // (name, source) pair (its own documented contract), and a
                // multi-Key Animation calls this once per Key that sets
                // the same property - a real, reproduced crash the first
                // way (each call deleting the previous Key's still-in-use
                // ObservableProperty out from under it). Hand-written code
                // avoids this by registering once and reusing the same
                // pointer across every Key::setValue() call (see
                // examples/shapes2.cpp) - this mirrors that.
                PropertyBase* existing = PropertyManager::getProperty(source, propertyName);
                ObservableProperty<SourceT, ValueT>* property = existing != nullptr
                    ? static_cast<ObservableProperty<SourceT, ValueT>*>(existing)
                    : PropertyManager::registerProperty<ValueT>(static_cast<SourceT*>(source), propertyName);
                return std::make_unique<TypedKeyValue<SourceT, ValueT>>(
                    property, std::any_cast<ValueT>(boxedValue), kind);
            };
        }

        // nullptr (with a stderr note) if no registerTarget<SourceT,
        // ValueT>() call matches this exact (sourceType, valueType) pair -
        // Bundle::loadFrame() skips just that one key value rather than
        // failing the whole load, same "unrecognized data is silently
        // skipped" contract the rest of this codebase's own reflection
        // read paths already have.
        static std::unique_ptr<KeyValue> buildKeyValue(std::type_index sourceType, std::type_index valueType,
                void* source, const std::string& propertyName, const std::any& boxedValue, InterpolationKind kind) {
            AnimationTargetRegistry& reg = instance();
            auto it = reg.factories_.find(TypeKey{ sourceType, valueType });
            if (it == reg.factories_.end()) {
                return nullptr;
            }
            return it->second(source, propertyName, boxedValue, kind);
        }

    private:
        AnimationTargetRegistry() = default;

        static AnimationTargetRegistry& instance() {
            static AnimationTargetRegistry reg;
            return reg;
        }

        using TypeKey = std::pair<std::type_index, std::type_index>;
        using Factory = std::function<std::unique_ptr<KeyValue>(void*, const std::string&, const std::any&, InterpolationKind)>;
        std::map<TypeKey, Factory> factories_;
    };

}
