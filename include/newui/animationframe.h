#pragma once

#include <chrono>
#include <cstdint>

namespace newui {

    // A frame rate expressed as a rational numerator/denominator pair of
    // integers (frames per second = numerator / denominator), rather than
    // a single float or whole-number FPS value. This is what lets it
    // represent broadcast rates like NTSC's ~29.97 fps exactly - NTSC is
    // literally 30000/1001, not a value that any integer or (exactly)
    // float FPS field could hold - while still only ever storing integer
    // types. See the named constants below (NTSC(), PAL(), Film(), ...)
    // for the common ones.
    class FrameRate {
    public:
        constexpr FrameRate() = default;

        constexpr explicit FrameRate(std::uint32_t numerator, std::uint32_t denominator = 1)
            : numerator_(numerator), denominator_(denominator == 0 ? 1 : denominator) {}

        constexpr std::uint32_t numerator() const {
            return numerator_;
        }

        constexpr std::uint32_t denominator() const {
            return denominator_;
        }

        // Frames per second as a double - e.g. ~29.97 for NTSC() - for use
        // in time/frame-count arithmetic (see AnimationFrame). The
        // numerator/denominator pair, not this, is what's actually stored.
        double fps() const {
            return static_cast<double>(numerator_) / static_cast<double>(denominator_);
        }

        // Cross-multiplies rather than comparing fps() directly, so this
        // is an exact comparison, not a floating-point one.
        bool operator==(const FrameRate& other) const {
            return static_cast<std::uint64_t>(numerator_) * other.denominator_
                == static_cast<std::uint64_t>(other.numerator_) * denominator_;
        }

        bool operator!=(const FrameRate& other) const {
            return !(*this == other);
        }

        // Common broadcast/film/digital frame rates. NTSC's family (and
        // its film-transfer cousin) run at exactly 1000/1001 of their
        // "nominal" whole-number rate for historical color-signal
        // reasons; PAL and most digital/web rates don't have that
        // wrinkle, hence denominator 1.
        static constexpr FrameRate NTSC() {
            return FrameRate(30000, 1001);  // ~29.97 fps
        }

        static constexpr FrameRate NTSCFilm() {
            return FrameRate(24000, 1001);  // ~23.976 fps
        }

        static constexpr FrameRate NTSC60() {
            return FrameRate(60000, 1001);  // ~59.94 fps
        }

        static constexpr FrameRate PAL() {
            return FrameRate(25, 1);  // 25 fps
        }

        static constexpr FrameRate PAL50() {
            return FrameRate(50, 1);  // 50 fps
        }

        static constexpr FrameRate Film() {
            return FrameRate(24, 1);  // 24 fps
        }

        static constexpr FrameRate FPS30() {
            return FrameRate(30, 1);
        }

        static constexpr FrameRate FPS60() {
            return FrameRate(60, 1);
        }

        static constexpr FrameRate FPS120() {
            return FrameRate(120, 1);
        }

    private:
        std::uint32_t numerator_ = 30;
        std::uint32_t denominator_ = 1;
    };

    // A frame count (value) at a given FrameRate (framerate) - e.g. "frame
    // 150 at NTSC" (= 5.005s in). Used by AnimationManager to track
    // playback position instead of separately tracking a raw frame number
    // and a frame rate by hand.
    class AnimationFrame {
    public:
        AnimationFrame() = default;

        explicit AnimationFrame(FrameRate framerate) : framerate_(framerate) {}

        AnimationFrame(std::uint64_t value, FrameRate framerate) : value_(value), framerate_(framerate) {}

        std::uint64_t value() const {
            return value_;
        }

        void setValue(std::uint64_t value) {
            value_ = value;
        }

        FrameRate framerate() const {
            return framerate_;
        }

        void setFramerate(FrameRate framerate) {
            framerate_ = framerate;
        }

        // Advances value() by frames (one, by default) - e.g. for
        // stepping playback forward one frame at a time without going
        // through setFromElapsed()'s wall-clock math.
        void increment(std::uint64_t frames = 1) {
            value_ += frames;
        }

        AnimationFrame& operator++() {
            increment();
            return *this;
        }

        // Sets value() to how many whole frames, at framerate(), have
        // elapsed between startTime and currentTime. currentTime at or
        // before startTime sets value() to 0 rather than underflowing.
        void setFromElapsed(std::chrono::steady_clock::time_point startTime,
                std::chrono::steady_clock::time_point currentTime) {
            if (currentTime <= startTime) {
                value_ = 0;
                return;
            }

            std::chrono::duration<double> elapsed = currentTime - startTime;
            value_ = static_cast<std::uint64_t>(elapsed.count() * framerate_.fps());
        }

        // Returns the frame count at newRate representing (as closely as
        // whole frame counts allow) the same point in wall-clock time as
        // this AnimationFrame - e.g. converting frame 30 at Film() (24fps,
        // so 1.25s in) to NTSC() (~29.97fps) yields frame ~37, since
        // that's how far into an NTSC-timed playback 1.25s reaches.
        AnimationFrame convertTo(FrameRate newRate) const {
            double seconds = static_cast<double>(value_) / framerate_.fps();
            std::uint64_t newValue = static_cast<std::uint64_t>(seconds * newRate.fps());
            return AnimationFrame(newValue, newRate);
        }

    private:
        std::uint64_t value_ = 0;
        FrameRate framerate_ = FrameRate::FPS30();
    };

}
