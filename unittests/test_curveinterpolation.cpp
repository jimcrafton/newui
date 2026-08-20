#include "newui/newui.h"
#include "newui/curveinterpolation.h"

#include "newui/animation.h"
#include "newui/geometry.h"
#include "newui/property.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------
// CurveInterpolation<float> - the scalar case, checked against the
// closed-form Bezier formulas directly.
// ---------------------------------------------------------------------

TEST(CurveInterpolation, PointsStartsEmpty) {
    newui::CurveInterpolation<float> curve;
    EXPECT_TRUE(curve.points().empty());
}

TEST(CurveInterpolation, AddPointAppendsInOrder) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(1.0f);
    curve.addPoint(2.0f);
    curve.addPoint(3.0f);

    ASSERT_EQ(curve.points().size(), 3u);
    EXPECT_FLOAT_EQ(curve.points()[0], 1.0f);
    EXPECT_FLOAT_EQ(curve.points()[1], 2.0f);
    EXPECT_FLOAT_EQ(curve.points()[2], 3.0f);
}

TEST(CurveInterpolation, ClearPointsEmptiesIt) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(1.0f);
    curve.clearPoints();
    EXPECT_TRUE(curve.points().empty());
}

TEST(CurveInterpolation, EvaluateWithOnePointIsConstantRegardlessOfT) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(42.0f);

    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 42.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 42.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 42.0f);
}

TEST(CurveInterpolation, EvaluateWithTwoPointsIsALinearLerp) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(0.0f);
    curve.addPoint(100.0f);

    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.25f), 25.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 100.0f);
}

TEST(CurveInterpolation, EvaluateWithThreePointsMatchesQuadraticBezierFormula) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(0.0f);    // P0
    curve.addPoint(100.0f);  // P1 (control)
    curve.addPoint(0.0f);    // P2

    // B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2
    float t = 0.25f;
    float expected = (1 - t) * (1 - t) * 0.0f + 2 * (1 - t) * t * 100.0f + t * t * 0.0f;
    EXPECT_NEAR(curve.evaluate(t), expected, 1e-4f);

    // Symmetric curve peaks at t=0.5.
    EXPECT_NEAR(curve.evaluate(0.5f), 50.0f, 1e-4f);
}

// ---------------------------------------------------------------------
// evaluateSegment() - the shape used to control a Key transition: start/
// end come in as arguments, points() are the intermediate controls.
// ---------------------------------------------------------------------

TEST(CurveInterpolation, EvaluateSegmentWithNoPointsIsAPlainLerp) {
    newui::CurveInterpolation<float> curve;  // no addPoint() calls

    EXPECT_FLOAT_EQ(curve.evaluateSegment(0.0f, 100.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.evaluateSegment(0.0f, 100.0f, 0.5f), 50.0f);
    EXPECT_FLOAT_EQ(curve.evaluateSegment(0.0f, 100.0f, 1.0f), 100.0f);
}

TEST(CurveInterpolation, EvaluateSegmentWithOneControlPointMatchesQuadraticBezier) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(200.0f);  // pulls the midpoint up

    float t = 0.5f;
    float expected = (1 - t) * (1 - t) * 0.0f + 2 * (1 - t) * t * 200.0f + t * t * 100.0f;
    EXPECT_NEAR(curve.evaluateSegment(0.0f, 100.0f, t), expected, 1e-4f);
}

TEST(CurveInterpolation, EvaluateSegmentWithTwoControlPointsMatchesCubicBezier) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(50.0f);
    curve.addPoint(150.0f);

    float t = 0.5f;
    // Cubic Bezier: B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3
    float p0 = 0.0f, p1 = 50.0f, p2 = 150.0f, p3 = 100.0f;
    float expected = (1 - t) * (1 - t) * (1 - t) * p0
        + 3 * (1 - t) * (1 - t) * t * p1
        + 3 * (1 - t) * t * t * p2
        + t * t * t * p3;
    EXPECT_NEAR(curve.evaluateSegment(p0, p3, t), expected, 1e-4f);
}

TEST(CurveInterpolation, OperatorCallMatchesEvaluateSegment) {
    newui::CurveInterpolation<float> curve;
    curve.addPoint(200.0f);

    EXPECT_FLOAT_EQ(curve(0.0f, 100.0f, 0.5f), curve.evaluateSegment(0.0f, 100.0f, 0.5f));
}

// ---------------------------------------------------------------------
// CurveInterpolation<Point> - the vector-space case (relies on the +, -,
// * operators added to Point in geometry.h for exactly this).
// ---------------------------------------------------------------------

TEST(CurveInterpolation, EvaluateSegmentWithPointTracesAQuadraticArc) {
    newui::CurveInterpolation<newui::Point> curve;
    curve.addPoint(newui::Point(100.0f, 0.0f));

    newui::Point start(0.0f, 100.0f);
    newui::Point end(100.0f, 100.0f);

    newui::Point atStart = curve.evaluateSegment(start, end, 0.0f);
    newui::Point atEnd = curve.evaluateSegment(start, end, 1.0f);
    newui::Point atMid = curve.evaluateSegment(start, end, 0.5f);

    EXPECT_EQ(atStart, start);
    EXPECT_EQ(atEnd, end);
    // Midpoint of a quadratic Bezier is the average of the three control
    // points: ((0+100+100)/4, (100+0+100)/4) using B(0.5) directly:
    // 0.25*start + 0.5*control + 0.25*end.
    EXPECT_FLOAT_EQ(atMid.x, 0.25f * 0.0f + 0.5f * 100.0f + 0.25f * 100.0f);
    EXPECT_FLOAT_EQ(atMid.y, 0.25f * 100.0f + 0.5f * 0.0f + 0.25f * 100.0f);
}

// ---------------------------------------------------------------------
// Integration - CurveInterpolation<T> used directly as a Key's
// interpolation function, the same slot a raw lambda fills (see
// animation1.cpp's Demo 4 for the hand-written equivalent).
// ---------------------------------------------------------------------

TEST(CurveInterpolation, UsableDirectlyAsAKeyInterpolationFunction) {
    newui::PropertyManager::instance().clear();
    newui::Point positionField(-1.0f, -1.0f);
    auto* position = newui::PropertyManager::instance().registerProperty(nullptr, &positionField, "position");

    newui::CurveInterpolation<newui::Point> curve;
    curve.addPoint(newui::Point(100.0f, 0.0f));

    newui::Animation animation("arc", 0, 10);
    animation.addKey("start", 0)->setValue(position, newui::Point(0.0f, 100.0f));
    animation.addKey("end", 10)->setValue(position, newui::Point(100.0f, 100.0f), curve);

    animation.processFrame(5);  // t = 0.5

    EXPECT_FLOAT_EQ(positionField.x, 0.25f * 0.0f + 0.5f * 100.0f + 0.25f * 100.0f);
    EXPECT_FLOAT_EQ(positionField.y, 0.25f * 100.0f + 0.5f * 0.0f + 0.25f * 100.0f);
}
