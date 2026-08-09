#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace newui {

    // A parametric Bezier curve of arbitrary order, evaluated via De
    // Casteljau's algorithm: repeatedly lerping between each consecutive
    // pair of points until only one remains. T must support T+T, T-T, and
    // T*float (vector-space arithmetic) - plain arithmetic types (float,
    // double, ...) provide these natively; newui::Point provides them
    // explicitly (see geometry.h) for exactly this use.
    //
    // operator() matches Property<T>::InterpolationFunction's
    // T(T start, T end, float t) signature, so a CurveInterpolation<T> can
    // be passed directly to Key::setValue() wherever a custom
    // interpolation function is accepted - no adapter needed. Used that
    // way, addPoint()'d control points sit *between* the Key transition's
    // start and end values (see evaluateSegment()), shaping the path the
    // way a vector-graphics Bezier handle shapes a curve - not just
    // retiming it, but bending it. With no control points added,
    // evaluateSegment() degenerates to a plain two-point (linear) Bezier,
    // i.e. a straight lerp.
    template<typename T>
    class CurveInterpolation {
    public:
        // Appends a control point to the curve, after any already added.
        void addPoint(T point) {
            points_.push_back(std::move(point));
        }

        void clearPoints() {
            points_.clear();
        }

        const std::vector<T>& points() const {
            return points_;
        }

        // Evaluates this curve's own points() (not a segment's start/end -
        // see evaluateSegment()) via De Casteljau's algorithm at t.
        // points() must have at least one entry.
        T evaluate(float t) const {
            return deCasteljau(points_, t);
        }

        // Evaluates [start, points()..., end] at t - the shape used when
        // this curve controls a Key transition (see the class comment):
        // start/end come from the segment being interpolated, and this
        // curve's own points() are the intermediate control points that
        // bend the path between them.
        T evaluateSegment(T start, T end, float t) const {
            std::vector<T> controlPoints;
            controlPoints.reserve(points_.size() + 2);
            controlPoints.push_back(std::move(start));
            controlPoints.insert(controlPoints.end(), points_.begin(), points_.end());
            controlPoints.push_back(std::move(end));

            return deCasteljau(std::move(controlPoints), t);
        }

        T operator()(T start, T end, float t) const {
            return evaluateSegment(std::move(start), std::move(end), t);
        }

    private:
        static T deCasteljau(std::vector<T> workingPoints, float t) {
            while (workingPoints.size() > 1) {
                for (std::size_t i = 0; i + 1 < workingPoints.size(); ++i) {
                    workingPoints[i] = workingPoints[i] + (workingPoints[i + 1] - workingPoints[i]) * t;
                }
                workingPoints.pop_back();
            }
            return workingPoints.front();
        }

        std::vector<T> points_;
    };

}
