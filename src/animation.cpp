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

    if (localFrame <= keys_.front()->keyFrame()) {
        for (const auto& value : keys_.front()->values()) {
            value->apply();
        }
        return;
    }

    if (localFrame >= keys_.back()->keyFrame()) {
        for (const auto& value : keys_.back()->values()) {
            value->apply();
        }
        return;
    }

    Key* fromKey = keys_.front().get();
    Key* toKey = keys_.back().get();
    for (std::size_t i = 0; i + 1 < keys_.size(); ++i) {
        if (localFrame >= keys_[i]->keyFrame() && localFrame <= keys_[i + 1]->keyFrame()) {
            fromKey = keys_[i].get();
            toKey = keys_[i + 1].get();
            break;
        }
    }

    std::uint64_t span = toKey->keyFrame() - fromKey->keyFrame();
    float t = (span > 0)
        ? static_cast<float>(localFrame - fromKey->keyFrame()) / static_cast<float>(span)
        : 1.0f;

    for (const auto& toValue : toKey->values()) {
        KeyValue* fromValue = fromKey->findValue(toValue->property());
        toValue->interpolateFrom(fromValue, t);
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
