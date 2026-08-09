#pragma once

namespace newui {

class Point {
public:
    Point() = default;
    Point(float x, float y) : x(x), y(y) {}

    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
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
};

class Rect {
public:
    Rect() = default;
    Rect(float x, float y, float width, float height)
        : size_(width, height), pos_(x, y) {}
    Rect(const Point& pos, const Size& size)
        : size_(size), pos_(pos) {}

    

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

    float top() const {
        return pos_.y;
    }

    float right() const {
        return pos_.x + size_.width;
    }

    float bottom() const {
        return pos_.y + size_.height;
    }

    bool contains(const Point& point) const {
        return point.x >= left() && point.x < right()
            && point.y >= top() && point.y < bottom();
    }

    // Returns a copy inset by the given amount on each side (a negative
    // amount inflates that side instead). Width/height are clamped to 0
    // rather than going negative if the insets overlap.
    Rect deflated(float left, float top, float right, float bottom) const {
        float w = size_.width - left - right;
        float h = size_.height - top - bottom;
        return Rect(pos_.x + left, pos_.y + top, w < 0.0f ? 0.0f : w, h < 0.0f ? 0.0f : h);
    }

    // Uniform inset on all four sides.
    Rect deflated(float amount) const {
        return deflated(amount, amount, amount, amount);
    }

    bool operator==(const Rect& other) const {
        return pos_ == other.pos_ && size_ == other.size_;
    }

    bool operator!=(const Rect& other) const {
        return !(*this == other);
    }

private:
    Point pos_;
    Size size_;
};

}
