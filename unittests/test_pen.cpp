#include <gtest/gtest.h>

#include <newui/pen.h>

#include <windows.h>

using namespace newui;

TEST(Pen, ReadsPressureTiltAndButtons) {
    PenInfo info;
    info.pointerFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
    info.penFlags = PEN_FLAG_BARREL;
    info.penMask = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
    info.pressure = 512;
    info.tiltX = -30;
    info.tiltY = 45;

    const PenState state = penStateFrom(info, Point(10.0f, 20.0f));
    EXPECT_TRUE(state.inRange);
    EXPECT_TRUE(state.inContact);
    EXPECT_TRUE(state.barrel);
    EXPECT_FALSE(state.eraser);
    EXPECT_TRUE(state.hasPressure);
    EXPECT_FLOAT_EQ(state.pressure, 0.5f);
    EXPECT_EQ(state.tiltX, -30);
    EXPECT_EQ(state.tiltY, 45);
    EXPECT_FLOAT_EQ(state.position.x, 10.0f);
}

TEST(Pen, HoveringHasNoContactAndNoPressure) {
    PenInfo info;
    info.pointerFlags = POINTER_FLAG_INRANGE;
    info.penMask = PEN_MASK_PRESSURE;
    const PenState state = penStateFrom(info, Point());
    EXPECT_TRUE(state.inRange);
    EXPECT_FALSE(state.inContact);
    EXPECT_FLOAT_EQ(state.pressure, 0.0f);
}

TEST(Pen, MissingPressureReadsAsFullWhileTouching) {
    PenInfo info;
    info.pointerFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
    const PenState state = penStateFrom(info, Point());
    EXPECT_FALSE(state.hasPressure);
    EXPECT_FLOAT_EQ(state.pressure, 1.0f);
}

TEST(Pen, InvertedOrEraserFlagMeansEraser) {
    PenInfo info;
    info.penFlags = PEN_FLAG_INVERTED;
    EXPECT_TRUE(penStateFrom(info, Point()).eraser);
    info.penFlags = PEN_FLAG_ERASER;
    EXPECT_TRUE(penStateFrom(info, Point()).eraser);
}
