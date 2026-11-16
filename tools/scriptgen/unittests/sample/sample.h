#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demo {

enum class Mode { Fast, Slow };

enum Legacy { LegacyA, LegacyB = 5 };

// Not marked: it is bound as an empty class because Circle names it.
class Hidden {
public:
    int secret() const { return 7; }
};

// Never bound, and the members that name it are left out without a warning.
// @scriptable ignore=true
class Internal {};

// @scriptable
class Shape {
public:
    Shape() = default;
    explicit Shape(int id) : id_(id) {}
    virtual ~Shape() = default;
    virtual std::string name() const = 0;
    int id() const { return id_; }

protected:
    int id_ = 3;
};

// @scriptable holder=shared
class Circle : public Shape {
public:
    enum class Kind { Round, Flat };

    Circle() = default;
    explicit Circle(double radius, const std::string& label = "circle") : radius_(radius), label_(label) {}

    std::string name() const override { return label_; }
    double area() const { return 3.0 * radius_ * radius_; }

    // @scriptable property=radius setter=setRadius
    double radius() const { return radius_; }
    void setRadius(double radius) { radius_ = radius; }

    int scale(int v) { return v * 2; }
    float scale(float v) { return v * 2.5f; }

    int add(int a, int b = 10) const { return a + b; }
    static Circle unit() { return Circle(1.0, "unit"); }
    std::shared_ptr<Circle> clone() const { return std::make_shared<Circle>(*this); }
    Hidden& hidden() { return hidden_; }
    const Hidden& hidden() const { return hidden_; }
    double stretch(double widthScale, int stepCount = 2) const { return widthScale * stepCount; }
    Mode mode() const { return Mode::Fast; }
    Legacy legacy() const { return LegacyB; }
    Kind kind() const { return Kind::Flat; }
    std::vector<int> numbers() const { return {1, 2, 3}; }
    std::optional<std::string> maybe(bool present) const {
        if (present) {
            return label_;
        }
        return std::nullopt;
    }
    void each(const std::function<void(int)>& fn) const {
        fn(1);
        fn(2);
    }

    void outParameter(int& result) const { result = 1; }
    void touch(Internal& internal) const { (void)internal; }
    Internal* internals() const { return nullptr; }

    // @scriptable ignore=true
    void notExposed() {}

    // @scriptable name=renamed_value
    int counter = 0;
    static inline int instances = 0;
    const int fixed = 4;

private:
    double radius_ = 2.0;
    std::string label_ = "circle";
    Hidden hidden_;
};

}  // namespace demo
