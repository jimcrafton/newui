#pragma once

// Only for the POINT/RECT conversions below - safe to pull in here (this
// whole toolkit is Win32-only throughout, see newui.h) via newui.h itself
// rather than a raw <windows.h>, so NOMINMAX/UNICODE are already defined
// before windows.h is reached even if this header happens to be the first
// one included in some translation unit - see the std::min/max hazard
// documented against utils.h (feedback_no_std_minmax memory/HANDOFF.md
// Part 6) for exactly what skipping that sequencing once already broke.
#include "newui/newui.h"
#include <blend2d/blend2d.h>


namespace newui {

class Point {
public:
    Point() = default;
    Point(float x, float y) : x(x), y(y) {}

    // Implicit both ways - POINT/Point round-trip freely at any Win32 API
    // boundary (::ClientToScreen(hwnd, &point) etc.) without an explicit
    // cast at every call site.
    Point(const POINT& pt) : x(float(pt.x)), y(float(pt.y)) {}

    operator POINT() const {
        return POINT{ LONG(x), LONG(y) };
    }

    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
    }

    // Vector-space arithmetic (Point doubles as a 2D vector) - lets
    // generic code that only needs +, -, and scalar * (e.g.
    // CurveInterpolation<T>'s De Casteljau evaluation) work with Point
    // the same way it does with a plain arithmetic T.
    Point operator+(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    Point operator-(const Point& other) const {
        return Point(x - other.x, y - other.y);
    }

    Point operator*(float scalar) const {
        return Point(x * scalar, y * scalar);
    }

    Point& operator+=(const Point& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Point& operator-=(const Point& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Point& operator+=(float scalar) {
        x += scalar;
        y += scalar;
        return *this;
    }

    Point& operator-=(float scalar) {
        x -= scalar;
        y -= scalar;
        return *this;
    }

    Point& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Point operator/(float scalar) const {
        return Point(x / scalar, y / scalar);
    }

    Point& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }
};

class Size {
public:
    Size() = default;
    Size(float width, float height) : width(width), height(height) {}

    float width = 0.0f;
    float height = 0.0f;

    bool operator==(const Size& other) const {
        return width == other.width && height == other.height;
    }

    bool operator!=(const Size& other) const {
        return !(*this == other);
    }

    Size& operator+=(const Size& other) {
        width += other.width;
        height += other.height;
        return *this;
    }

    Size& operator-=(const Size& other) {
        width -= other.width;
        height -= other.height;
        return *this;
    }

    Size& operator+=(float scalar) {
        width += scalar;
        height += scalar;
        return *this;
    }

    Size& operator-=(float scalar) {
        width -= scalar;
        height -= scalar;
        return *this;
    }

    Size& operator*=(float scalar) {
        width *= scalar;
        height *= scalar;
        return *this;
    }

    Size& operator/=(float scalar) {
        width /= scalar;
        height /= scalar;
        return *this;
    }
};

class Rect {
public:
    Rect() = default;
    Rect(float x, float y, float width, float height)
        : size_(width, height), pos_(x, y) {}
    Rect(const Point& pos, const Size& size)
        : size_(size), pos_(pos) {}

    // Implicit both ways, same reasoning as Point's POINT conversions
    // above - RECT::right/bottom are already exclusive edges, exactly
    // matching how right()/bottom() are already defined here.
    Rect(const RECT& r)
        : size_(float(r.right - r.left), float(r.bottom - r.top))
        , pos_(float(r.left), float(r.top)) {}

    operator RECT() const {
        return RECT{ LONG(left()), LONG(top()), LONG(right()), LONG(bottom()) };
    }

    operator BLRect() const {
        return BLRect{ left(), top(), width(), height() };
    }



    Point pos() const {
        return pos_;
    }

    void setPos(const Point& pos) {
        pos_ = pos;
    }

    Size size() const {
        return size_;
    }

    void setSize(const Size& size) {
        size_ = size;
    }
    
    float left() const {
        return pos_.x;
    }

    // Moves this edge only - width()/height() (and the opposite edge's
    // position) stay fixed, same "one component of pos_/size_ at a time"
    // contract setPos()/setSize() already have for the whole Point/Size.
    //
    // left/top/width/height are read-only from reflectgen's perspective
    // (setLeft/setTop/setWidth/setHeight below are all @reflect
    // ignore=true) - pos()/size() are the two real, independently
    // reflected properties backing the same pos_/size_ storage; before
    // this, both the structured (pos/size) and flat (left/top/width/
    // height) views of the identical data were separately registered as
    // full read/write properties, so ObjectReader::read()'s per-property
    // walk applied pos_/size_ correctly via "pos"/"size" and then
    // immediately overwrote them again via "left"/"top"/"width"/"height" -
    // harmless when a document happens to carry both (redundant but
    // consistent, as Bundle::writeFrame()'s own output always does - it
    // writes both), but a real, reproduced data-loss bug for any
    // document (e.g. one written by hand, or by anything else that only
    // emits "pos"/"size") that has the structured pair but not the flat
    // one: every such bounds/clientBounds/etc. silently came back at
    // Rect{}'s default (0,0 0x0) - see Bundle test
    // LoadFrameAppliesBoundsAndVisibleOntoRebuiltChildren.
    //@reflect ignore=true
    void setLeft(float value) {
        pos_.x = value;
    }

    float top() const {
        return pos_.y;
    }

    //@reflect ignore=true
    void setTop(float value) {
        pos_.y = value;
    }

    float right() const {
        return pos_.x + size_.width;
    }

    float bottom() const {
        return pos_.y + size_.height;
    }


    float width() const {
        return size_.width;
    }

    //@reflect ignore=true
    void setWidth(float value) {
        size_.width = value;
    }

    float height() const {
        return size_.height;
    }

    //@reflect ignore=true
    void setHeight(float value) {
        size_.height = value;
    }


    bool contains(const Point& point) const {
        return point.x >= left() && point.x < right()
            && point.y >= top() && point.y < bottom();
    }

    // Returns a copy inset by the given amount on each side (a negative
    // amount inflates that side instead). Width/height are clamped to 0
    // rather than going negative if the insets overlap.
    Rect deflate(float left, float top, float right, float bottom) const {
        float w = size_.width - left - right;
        float h = size_.height - top - bottom;
        return Rect(pos_.x + left, pos_.y + top, w < 0.0f ? 0.0f : w, h < 0.0f ? 0.0f : h);
    }

    // Uniform inset on all four sides.
    Rect deflate(float amount) const {
        return deflate(amount, amount, amount, amount);
    }

    bool operator==(const Rect& other) const {
        return pos_ == other.pos_ && size_ == other.size_;
    }

    bool operator!=(const Rect& other) const {
        return !(*this == other);
    }

    void clear() {
        pos_.x = pos_.y = size_.height = size_.width = 0;
    }

    bool empty() const {
        return pos_.x == 0.0 && pos_.y == 0.0 && size_.width == 0.0 && size_.height == 0.0;
    }

private:
    Point pos_;
    Size size_;
};

}
