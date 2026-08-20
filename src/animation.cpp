#include "newui/animation.h"

#include <algorithm>

namespace newui {

Key* Animation::addKey(const std::string& name, std::uint64_t keyFrame) {
    auto key = std::make_unique<Key>(name, keyFrame);
    Key* result = key.get();
    keys_.push_back(std::move(key));

    std::sort(keys_.begin(), keys_.end(), [](const std::unique_ptr<Key>& a, const std::unique_ptr<Key>& b) {
        return a->keyFrame() < b->keyFrame();
    });

    return result;
}

void Animation::processFrame(std::uint64_t frame) {
    if (keys_.empty()) {
        return;
    }

    std::uint64_t localFrame = (frame > startTime_) ? (frame - startTime_) : 0;

    // Each property is bracketed and interpolated independently, walking
    // only the Keys that actually set *that* property - not just the two
    // Keys immediately adjacent to localFrame in this Animation's overall
    // keys_ list. A Key doesn't have to set every property this Animation
    // ever touches (see Key's own class comment: "Properties this Key
    // doesn't mention are left alone by it"), so two different properties
    // can have two entirely different bracketing Key pairs for the same
    // localFrame - e.g. a property set only on Keys at frame 0 and frame
    // 90 needs to keep interpolating smoothly across that whole span even
    // though some other property sets a Key at frame 45 in between that
    // says nothing about the first property at all. Treating keys_[i]/
    // keys_[i+1] as a shared bracket for every property (the previous
    // implementation) breaks exactly that case: a property missing from
    // the adjacent Key has nothing for interpolateFrom() to blend from,
    // so it steps straight to its next real value instead of continuing
    // its interpolation.
    std::vector<PropertyBase*> processed;
    for (const auto& key : keys_) {
        for (const auto& value : key->values()) {
            PropertyBase* property = value->property();
            if (std::find(processed.begin(), processed.end(), property) != processed.end()) {
                continue;
            }
            processed.push_back(property);

            Key* fromKey = nullptr;
            Key* toKey = nullptr;
            for (const auto& candidate : keys_) {
                if (candidate->findValue(property) == nullptr) {
                    continue;
                }
                if (candidate->keyFrame() <= localFrame) {
                    fromKey = candidate.get();
                } else if (toKey == nullptr) {
                    toKey = candidate.get();
                }
            }

            if (toKey == nullptr) {
                // localFrame is at or past the last Key that sets this
                // property specifically - hold its value.
                fromKey->findValue(property)->apply();
            } else if (fromKey == nullptr) {
                // localFrame is at or before the first Key that sets this
                // property specifically - nothing earlier to interpolate
                // from, so hold its value instead (same as
                // KeyValue::interpolateFrom(nullptr, t)'s own fallback).
                toKey->findValue(property)->apply();
            } else {
                std::uint64_t span = toKey->keyFrame() - fromKey->keyFrame();
                float t = (span > 0)
                    ? static_cast<float>(localFrame - fromKey->keyFrame()) / static_cast<float>(span)
                    : 1.0f;
                toKey->findValue(property)->interpolateFrom(fromKey->findValue(property), t);
            }
        }
    }
}

Animation* AnimationManager::addAnimation(const std::string& name, std::uint64_t startTime, std::uint64_t duration) {
    auto& inst = AnimationManager::instance();

    auto animation = std::make_unique<Animation>(name, startTime, duration);
    Animation* result = animation.get();
    inst.animations_.push_back(std::move(animation));
    return result;
}

void AnimationManager::removeAnimation(Animation* animation) {
    auto& inst = AnimationManager::instance();
    inst.animations_.erase(
        std::remove_if(inst.animations_.begin(), inst.animations_.end(),
            [animation](const std::unique_ptr<Animation>& entry) { return entry.get() == animation; }),
        inst.animations_.end());
}

bool AnimationManager::processIdle() {
    auto now = std::chrono::steady_clock::now();
    auto& inst = AnimationManager::instance();
    if (!inst.started_) {
        inst.clockStart_ = now;
        inst.started_ = true;
        return false;
    }

    AnimationFrame next(inst.currentFrame_.framerate());
    next.setFromElapsed(inst.clockStart_, now);

    if (next.value() <= inst.currentFrame_.value()) {
        return false;
    }

    std::uint64_t previousFrame = inst.currentFrame_.value();
    inst.currentFrame_ = next;

    for (const auto& animation : inst.animations_) {
        // A looping Animation (see Animation::looping()) never goes
        // through the isActiveAt()/justFinished machinery below at all -
        // once currentFrame_ reaches startTime(), it's fed
        // startTime() + (elapsed % duration()) forever, wrapping playback
        // back to the beginning every time it would otherwise have ended,
        // instead of holding endTime()'s Key values.
        if (animation->looping()) {
            if (inst.currentFrame_.value() < animation->startTime()) {
                continue;
            }
            std::uint64_t duration = animation->duration();
            std::uint64_t elapsed = inst.currentFrame_.value() - animation->startTime();
            std::uint64_t wrapped = animation->startTime() + (duration > 0 ? elapsed % duration : 0);
            animation->processFrame(wrapped);
            continue;
        }

        // Normally isActiveAt() alone is enough - currentFrame_ advances
        // one (or a few) whole frames per call, so a still-running
        // animation is simply active on the next call, same as always.
        // But if the gap between idle passes is large enough (e.g.
        // RunLoop is correctly *not* spinning as fast as possible between
        // calls - see RunLoop::run()'s idle-loop comment) relative to a
        // short animation's whole duration, this step's elapsed time can
        // jump clean over the animation's entire [startTime, endTime]
        // window in one call, and isActiveAt(currentFrame_.value()) would
        // then be false *every* time this animation is ever checked again
        // - it would simply never get another processFrame() call, and
        // silently stay stuck wherever it was (its initial state, if this
        // was its first-ever check) forever. justFinished catches exactly
        // that transition (was not yet past endTime() as of the previous
        // step, is now) and gives it one settling call, clamped to its
        // own endTime() - Animation::processFrame() already holds/clamps
        // correctly for a frame past its last Key, so this reaches the
        // same real end value a normal frame-by-frame advance would have.
        bool active = animation->isActiveAt(inst.currentFrame_.value());
        bool justFinished = previousFrame < animation->endTime() && inst.currentFrame_.value() > animation->endTime();
        if (active || justFinished) {
            std::uint64_t frameToApply = active ? inst.currentFrame_.value() : animation->endTime();
            animation->processFrame(frameToApply);
        }
    }

    // Fired once every Animation active at the new frame has already been
    // given its processFrame() call above, so a subscriber reading
    // Property values back out (see onFrameChanged's own comment) sees
    // this frame's fully-updated state, not a partial one.
    inst.onFrameChanged(inst, inst.currentFrame_.value());

    return false;
}

void AnimationManager::clear() {
    auto& inst = AnimationManager::instance();
    inst.animations_.clear();
    inst.currentFrame_ = AnimationFrame();
    inst.clockStart_ = std::chrono::steady_clock::time_point();
    inst.started_ = false;
}

}
