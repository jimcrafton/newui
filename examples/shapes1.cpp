// A first look at newui::shapes (shapes.h) - a declarative graphics layer
// (Shape/ShapeStyle/ShapeLayer) sitting on top of the same BLContext every
// ViewStyle already paints with. ShapeLayerView below is a small custom
// SubView (the intended way to host one: override View::paint(BLContext&)
// and call ShapeLayer::render() from it, on top of whatever the base
// ViewStyle already drew for the background) holding a handful of shapes
// that exercise most of the hierarchy at once:
//   - RoundRect: solid fill + stroke
//   - Circle: a Linear gradient fill
//   - Circle: a solid fill plus an enabled Glow
//   - Arc (pie slice): a solid fill plus an enabled DropShadow
//   - Line: stroke only
//   - Curve: a smooth path through several points, stroke only
//   - A rough glassmorphism panel: a RoundRect backdrop filled with a
//     GradientKind::Point gradient (4 corner anchors), with translucent
//     "glass" RoundRect/Circle UI shapes (a card, a pill button, an
//     iOS-style toggle switch) layered on top - see AddAuroraPanelShape()'s
//     own comment for what this does and doesn't actually emulate.
//   - Three overlapping Circles blended via ShapeStyle::compositingOp()
//     (Multiply, then Screen) instead of the default CompSrcOver.
//   - A Circle filled with a GradientKind::Conic color wheel.
//   - Five RoundRects rotated via Transform2D::setRotationRadians()/
//     setPivot() - every other shape above leaves its own transform() at
//     the identity.
//   - Five RoundRects skewed via Transform2D::setSkewX() (a fixed shear
//     angle per square, increasing left to right) - each square keeps its
//     own local geometry at (0,0) and uses transform().setPosition() to
//     place itself, since blend2d's skew has no pivot correction of its
//     own (skewing about a non-origin position would fling the shape
//     off-screen otherwise).
//   - A thick-stroked wavy Curve with an enabled Glow and no fill, to
//     show that Shape::paintEffect() rasterizes both fill AND stroke
//     geometry into its glow/shadow mask, so a glow on a stroke-only
//     shape still hugs the stroke's own outline rather than needing an
//     invisible fill to "attach" to.
//
// Deliberately not "using namespace newui::shapes" anywhere in this file -
// newui::shapes::Rectangle collides with wingdi.h's own
// ::Rectangle(HDC, ...) function (pulled in via newui.h's <windows.h>), so
// every shapes:: type below is spelled out instead (same reason
// unittests/test_shapes.cpp already does this).

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/reflection.h"
#include "newui/reflectionio.h"
#include "newui/rootview.h"
#include "newui/shapes.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <fstream>
#include <memory>
#include <string>

// Defined in the reflectgen-generated .cpp (cmake/ReflectGen.cmake's
// newui_add_reflectgen_output(newui), compiled into the `newui` target
// this example links against) - same forward declaration src/main.cpp/
// unittests/test_reflection.cpp already use; no header declares it since
// it's generated, not hand-written. Has to run before WriteShapeLayerToDisk()
// below - ObjectWriter can't find Circle/RoundRect/.../ShapeLayer's own
// registered Class/Property data otherwise.
extern void registerReflectionData();

// Heap-only, like every other View/SubView in this codebase - see
// SubView's own class comment (subview.h).
class ShapeLayerView : public newui::SubView {
public:
    newui::shapes::ShapeLayer& shapeLayer() { return shapeLayer_; }

    // Runs after paintStyle() (View::paintChildren(), view.cpp) - already
    // translated/clipped to this view's own (0,0)-(size) local space, on
    // top of whatever the base ViewStyle already painted for the
    // background. visibleRect is this view's own full bounds - a real app
    // hosting a much larger ShapeLayer than fits on screen would pass the
    // actual dirty/visible sub-rect instead, so ShapeLayer::render() can
    // skip shapes entirely outside it (see its own class comment).
    void paint(BLContext& ctx) override {
        newui::Rect visibleRect(0.0f, 0.0f, bounds().width(), bounds().height());
        shapeLayer_.render(ctx, visibleRect);
    }

private:
    newui::shapes::ShapeLayer shapeLayer_;
};

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

// RoundRect at (20,20): a plain solid fill + a contrasting stroke - the
// baseline everything else in this file varies from.
void AddRoundRectShape(newui::shapes::ShapeLayer& layer) {
    auto* rect = new newui::shapes::RoundRect();
    rect->setX(20.0f);
    rect->setY(20.0f);
    rect->setWidth(150.0f);
    rect->setHeight(100.0f);
    rect->setRadiusX(16.0f);
    rect->setRadiusY(16.0f);

    rect->style().fill().setColor(newui::Color(0x4a90d9u, false));
    rect->style().fill().setKind(newui::gfx::PaintKind::Color);
    rect->style().stroke().setColor(newui::Color(0x1c4a75u, false));
    rect->style().stroke().setKind(newui::gfx::PaintKind::Color);
    rect->style().stroke().setWidth(3.0f);

    layer.addShape(rect);
}

// Circle at (255,70): a Linear gradient fill, left-to-right across its own
// diameter.
void AddGradientCircleShape(newui::shapes::ShapeLayer& layer) {
    auto* circle = new newui::shapes::Circle();
    circle->setCenterX(255.0f);
    circle->setCenterY(70.0f);
    circle->setRadius(55.0f);

    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Linear);
    gradient.setLinearStart(newui::Point(circle->centerX() - circle->radius(), circle->centerY()));
    gradient.setLinearEnd(newui::Point(circle->centerX() + circle->radius(), circle->centerY()));
    gradient.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(0xffcc00u, false)));
    gradient.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(0xff3366u, false)));

    circle->style().fill().setGradient(gradient);
    circle->style().fill().setKind(newui::gfx::PaintKind::Gradient);

    layer.addShape(circle);
}

// Circle at (390,70): a solid fill plus an enabled Glow - see Shape::
// render()'s own doc comment (shapes.h) for how this gets approximated
// (a box-blurred silhouette, since blend2d has no built-in blur filter).
void AddGlowCircleShape(newui::shapes::ShapeLayer& layer) {
    auto* circle = new newui::shapes::Circle();
    circle->setCenterX(390.0f);
    circle->setCenterY(70.0f);
    circle->setRadius(45.0f);

    circle->style().fill().setColor(newui::Color(0x33cc99u, false));
    circle->style().fill().setKind(newui::gfx::PaintKind::Color);

    circle->style().glow().setEnabled(true);
    circle->style().glow().setColor(newui::Color(0x33cc99u, false));
    circle->style().glow().setAmount(0.85f);
    circle->style().glow().setSoftness(14.0f);

    layer.addShape(circle);
}

// Arc (pie slice) centered at (85,225): a solid fill plus an enabled
// DropShadow, offset down-and-right.
void AddDropShadowArcShape(newui::shapes::ShapeLayer& layer) {
    auto* arc = new newui::shapes::Arc();
    arc->setCenterX(85.0f);
    arc->setCenterY(225.0f);
    arc->setRadius(60.0f);
    arc->setStartRadians(0.0f);
    arc->setSweepRadians(4.0f);  // a bit under 230 degrees - visibly not a full circle
    arc->setPie(true);

    arc->style().fill().setColor(newui::Color(0xf2994au, false));
    arc->style().fill().setKind(newui::gfx::PaintKind::Color);

    arc->style().dropShadow().setEnabled(true);
    arc->style().dropShadow().setOffset(newui::Point(6.0f, 6.0f));
    arc->style().dropShadow().setSoftness(8.0f);

    layer.addShape(arc);
}

// A stroked Line from (200,180) to (330,270).
void AddLineShape(newui::shapes::ShapeLayer& layer) {
    auto* line = new newui::shapes::Line();
    line->setP0(newui::Point(200.0f, 180.0f));
    line->setP1(newui::Point(330.0f, 270.0f));

    line->style().stroke().setColor(newui::Color(0x333333u, false));
    line->style().stroke().setKind(newui::gfx::PaintKind::Color);
    line->style().stroke().setWidth(4.0f);

    layer.addShape(line);
}

// A stroked Curve running smoothly through four points - see Curve's own
// class comment (shapes.h) for the Catmull-Rom-to-Bezier construction.
void AddCurveShape(newui::shapes::ShapeLayer& layer) {
    auto* curve = new newui::shapes::Curve();
    curve->points().push_back(newui::Point(370.0f, 170.0f));
    curve->points().push_back(newui::Point(410.0f, 280.0f));
    curve->points().push_back(newui::Point(460.0f, 160.0f));
    curve->points().push_back(newui::Point(510.0f, 270.0f));

    curve->style().stroke().setColor(newui::Color(0x7b2cbfu, false));
    curve->style().stroke().setKind(newui::gfx::PaintKind::Color);
    curve->style().stroke().setWidth(3.0f);

    layer.addShape(curve);
}

// --- "Glass" UI panel -------------------------------------------------
// A rough glassmorphism look (frosted-glass cards over a colorful
// backdrop) built entirely out of Shape primitives - no real backdrop
// blur (that would need Shape::render() to sample whatever's already in
// the destination buffer and blur *that*, which nothing here does; Glow/
// DropShadow only ever blur a shape's own silhouette). The translucency,
// border highlight, and soft shadow read close enough to sell it anyway:
// a low-alpha near-white fill, a brighter-but-still-translucent stroke
// standing in for the usual light-catching edge highlight, and a soft
// low-amount DropShadow for lift off the backdrop.

// A big rounded panel behind everything else in this section, filled
// with a GradientKind::Point gradient - four corner anchors blended by
// inverse-distance weighting (Gradient::rasterizePoints(), graphics.cpp)
// rather than laid out along one axis the way Linear/Radial are. This is
// the one gradient kind blend2d has no native equivalent for.
void AddAuroraPanelShape(newui::shapes::ShapeLayer& layer) {
    auto* panel = new newui::shapes::RoundRect();
    panel->setX(20.0f);
    panel->setY(320.0f);
    panel->setWidth(650.0f);
    panel->setHeight(190.0f);
    panel->setRadiusX(24.0f);
    panel->setRadiusY(24.0f);

    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Point);
    gradient.points().push_back(newui::gfx::GradientPoint(newui::Point(20.0f, 320.0f), newui::Color(0x7b2cbfu, false)));
    gradient.points().push_back(newui::gfx::GradientPoint(newui::Point(670.0f, 320.0f), newui::Color(0xff3366u, false)));
    gradient.points().push_back(newui::gfx::GradientPoint(newui::Point(20.0f, 510.0f), newui::Color(0x2e6fdbu, false)));
    gradient.points().push_back(newui::gfx::GradientPoint(newui::Point(670.0f, 510.0f), newui::Color(0xffa733u, false)));
    gradient.setPointRasterMax(96);

    panel->style().fill().setGradient(gradient);
    panel->style().fill().setKind(newui::gfx::PaintKind::Gradient);

    layer.addShape(panel);
}

// A translucent "glass card" floating on the aurora panel: low-alpha
// white fill, a brighter (but still translucent) stroke standing in for
// a light-catching edge, and a soft DropShadow for lift.
void AddGlassCardShape(newui::shapes::ShapeLayer& layer) {
    auto* card = new newui::shapes::RoundRect();
    card->setX(45.0f);
    card->setY(345.0f);
    card->setWidth(190.0f);
    card->setHeight(140.0f);
    card->setRadiusX(20.0f);
    card->setRadiusY(20.0f);

    card->style().fill().setColor(newui::Color(1.0f, 1.0f, 1.0f, 0.18f));
    card->style().fill().setKind(newui::gfx::PaintKind::Color);
    card->style().stroke().setColor(newui::Color(1.0f, 1.0f, 1.0f, 0.45f));
    card->style().stroke().setKind(newui::gfx::PaintKind::Color);
    card->style().stroke().setWidth(1.5f);

    card->style().dropShadow().setEnabled(true);
    card->style().dropShadow().setColor(newui::Color(0.0f, 0.0f, 0.0f, 0.35f));
    card->style().dropShadow().setOffset(newui::Point(0.0f, 10.0f));
    card->style().dropShadow().setSoftness(16.0f);

    layer.addShape(card);
}

// A pill-shaped "glass button" - same translucent treatment as the card,
// just a fully-rounded (radius == half the height) RoundRect instead.
void AddGlassButtonShape(newui::shapes::ShapeLayer& layer) {
    auto* button = new newui::shapes::RoundRect();
    button->setX(260.0f);
    button->setY(360.0f);
    button->setWidth(150.0f);
    button->setHeight(46.0f);
    button->setRadiusX(23.0f);
    button->setRadiusY(23.0f);

    button->style().fill().setColor(newui::Color(1.0f, 1.0f, 1.0f, 0.22f));
    button->style().fill().setKind(newui::gfx::PaintKind::Color);
    button->style().stroke().setColor(newui::Color(1.0f, 1.0f, 1.0f, 0.5f));
    button->style().stroke().setKind(newui::gfx::PaintKind::Color);
    button->style().stroke().setWidth(1.5f);

    button->style().dropShadow().setEnabled(true);
    button->style().dropShadow().setColor(newui::Color(0.0f, 0.0f, 0.0f, 0.3f));
    button->style().dropShadow().setOffset(newui::Point(0.0f, 6.0f));
    button->style().dropShadow().setSoftness(10.0f);

    layer.addShape(button);
}

// An iOS-style toggle switch, "on": a pill track (same glass treatment)
// plus a solid, opaque thumb Circle offset toward the right edge, itself
// with a small DropShadow for its own separate lift off the track.
void AddGlassToggleShape(newui::shapes::ShapeLayer& layer) {
    auto* track = new newui::shapes::RoundRect();
    track->setX(440.0f);
    track->setY(365.0f);
    track->setWidth(76.0f);
    track->setHeight(36.0f);
    track->setRadiusX(18.0f);
    track->setRadiusY(18.0f);

    track->style().fill().setColor(newui::Color(0x33cc99u, false));
    track->style().fill().setKind(newui::gfx::PaintKind::Color);
    track->style().fill().setOpacity(0.55f);
    track->style().stroke().setColor(newui::Color(1.0f, 1.0f, 1.0f, 0.5f));
    track->style().stroke().setKind(newui::gfx::PaintKind::Color);
    track->style().stroke().setWidth(1.5f);

    layer.addShape(track);

    auto* thumb = new newui::shapes::Circle();
    thumb->setCenterX(482.0f);
    thumb->setCenterY(383.0f);
    thumb->setRadius(14.0f);

    thumb->style().fill().setColor(newui::Color(1.0f, 1.0f, 1.0f, 1.0f));
    thumb->style().fill().setKind(newui::gfx::PaintKind::Color);

    thumb->style().dropShadow().setEnabled(true);
    thumb->style().dropShadow().setColor(newui::Color(0.0f, 0.0f, 0.0f, 0.35f));
    thumb->style().dropShadow().setOffset(newui::Point(0.0f, 2.0f));
    thumb->style().dropShadow().setSoftness(4.0f);

    layer.addShape(thumb);
}

// --- Blend modes, Conic gradient, Transform2D --------------------------
// Three more ShapeStyle/Gradient/Transform2D facets the demo above never
// touches: compositingOp (Multiply/Screen - blend2d's own BLCompOp,
// wrapped as CompositingFlag, viewstyle.h), GradientKind::Conic (an
// angular sweep around a center, unlike Linear/Radial/Point), and
// Transform2D's rotationRadians()/pivot() (every earlier shape above
// left its own transform() at the identity).

// Three overlapping, translucent circles in a classic Venn arrangement -
// the second and third use compositingOp() (Multiply, then Screen)
// against whatever the earlier circles already painted, instead of the
// default CompSrcOver every other shape in this file uses.
void AddBlendModeVennShapes(newui::shapes::ShapeLayer& layer) {
    auto* red = new newui::shapes::Circle();
    red->setCenterX(110.0f);
    red->setCenterY(575.0f);
    red->setRadius(55.0f);
    red->style().fill().setColor(newui::Color(1.0f, 0.2f, 0.3f, 0.75f));
    red->style().fill().setKind(newui::gfx::PaintKind::Color);
    layer.addShape(red);

    auto* green = new newui::shapes::Circle();
    green->setCenterX(80.0f);
    green->setCenterY(630.0f);
    green->setRadius(55.0f);
    green->style().fill().setColor(newui::Color(0.2f, 0.9f, 0.4f, 0.75f));
    green->style().fill().setKind(newui::gfx::PaintKind::Color);
    green->style().setCompositingOp(newui::CompMultiply);
    layer.addShape(green);

    auto* blue = new newui::shapes::Circle();
    blue->setCenterX(140.0f);
    blue->setCenterY(630.0f);
    blue->setRadius(55.0f);
    blue->style().fill().setColor(newui::Color(0.2f, 0.5f, 1.0f, 0.75f));
    blue->style().fill().setKind(newui::gfx::PaintKind::Color);
    blue->style().setCompositingOp(newui::CompScreen);
    layer.addShape(blue);
}

// A color-wheel Circle: a Conic gradient (an angular sweep around
// conicCenter(), rather than laid out along a line like Linear or
// radiating outward like Radial) with 7 stops running all the way around
// the hue circle (0/360 degrees both land on red, so the wheel joins up
// with no visible seam).
void AddConicColorWheelShape(newui::shapes::ShapeLayer& layer) {
    auto* wheel = new newui::shapes::Circle();
    wheel->setCenterX(320.0f);
    wheel->setCenterY(600.0f);
    wheel->setRadius(65.0f);

    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Conic);
    gradient.setConicCenter(newui::Point(wheel->centerX(), wheel->centerY()));
    for (int i = 0; i <= 6; ++i) {
        float t = float(i) / 6.0f;
        gradient.stops().push_back(newui::gfx::GradientStop(t, newui::Color::fromHSL(t * 360.0f, 1.0f, 0.5f)));
    }

    wheel->style().fill().setGradient(gradient);
    wheel->style().fill().setKind(newui::gfx::PaintKind::Gradient);

    layer.addShape(wheel);
}

// Five identical small RoundRects, each rotated a bit further than the
// last via transform().setRotationRadians() - every other shape in this
// file leaves its own transform() at the identity (no rotation/scale/
// translation at all). Rotating around each square's own center needs
// pivot() set to that center (in the shape's own local space, same
// space x()/y()/width()/height() are already in) - rotation always
// happens about the pivot, not about local (0,0).
void AddRotatedSquaresShapes(newui::shapes::ShapeLayer& layer) {
    const float kBaseX = 420.0f;
    const float kCenterY = 600.0f;
    const float kSize = 40.0f;
    const float kSpacing = 55.0f;

    for (int i = 0; i < 5; ++i) {
        float centerX = kBaseX + float(i) * kSpacing;

        auto* square = new newui::shapes::RoundRect();
        square->setX(centerX - kSize * 0.5f);
        square->setY(kCenterY - kSize * 0.5f);
        square->setWidth(kSize);
        square->setHeight(kSize);
        square->setRadiusX(6.0f);
        square->setRadiusY(6.0f);

        square->style().fill().setColor(newui::Color::fromHSL(200.0f, 0.6f, 0.5f - float(i) * 0.06f));
        square->style().fill().setKind(newui::gfx::PaintKind::Color);

        square->transform().setPivot(newui::Point(centerX, kCenterY));
        square->transform().setRotationRadians(float(i) * 0.3927f);  // 0, 22.5, 45, 67.5, 90 degrees

        layer.addShape(square);
    }
}

// Five identical small RoundRects again, this time via transform().
// setSkewX() instead of rotation - skewX_ radians ranging from negative
// to positive across the row turns each square into an increasingly
// slanted parallelogram. Unlike rotation, skew has no pivot() to correct
// for - it always shears about local (0,0) (see Transform2D::skewX()'s
// own comment). Every other shape in this file places its geometry
// directly at large, canvas-scale x()/y() coordinates and leaves
// transform().position() at the default (0,0) - fine for translate (a
// no-op there) and rotate (pivot() already corrects for it), but skewing
// that same large y through local (0,0) would shear by tan(skewX)*y -
// with y around 700 and skewX up to ~23 degrees, on the order of 300
// pixels, well past the edge of this window. So unlike the rest of this
// file, each square here keeps small, near-origin local geometry
// (x()=y()=0) and is placed on the canvas via transform().setPosition()
// instead - translate() runs before skew() in Transform2D::applyTo(), so
// the shear ends up relative to each square's own small local origin,
// not the large canvas position.
void AddSkewedSquaresShapes(newui::shapes::ShapeLayer& layer) {
    const float kBaseX = 380.0f;
    const float kTopY = 690.0f;
    const float kSize = 40.0f;
    const float kSpacing = 50.0f;

    for (int i = 0; i < 5; ++i) {
        float x = kBaseX + float(i) * kSpacing;

        auto* square = new newui::shapes::RoundRect();
        square->setX(0.0f);
        square->setY(0.0f);
        square->setWidth(kSize);
        square->setHeight(kSize);
        square->setRadiusX(6.0f);
        square->setRadiusY(6.0f);

        square->style().fill().setColor(newui::Color::fromHSL(320.0f, 0.55f, 0.5f - float(i) * 0.05f));
        square->style().fill().setKind(newui::gfx::PaintKind::Color);

        square->transform().setPosition(newui::Point(x, kTopY));
        square->transform().setSkewX((float(i) - 2.0f) * 0.2f);  // -0.4 .. +0.4 radians (~-23 .. +23 degrees)

        layer.addShape(square);
    }
}

// A thick-stroked, glowing Curve with no fill at all - confirms Glow (and
// DropShadow) track a shape's *stroke* outline, not just its fill: Shape::
// paintEffect() (shapes.cpp) draws both the fill *and* the stroke into
// the blurred silhouette mask, using the real stroke().width(), so a
// stroke-only shape like this one still gets a glow shaped like its
// actual painted outline - not the invisible zero-area fill a fill-only
// mask would produce for a shape whose fill is PaintKind::None.
void AddGlowingStrokedCurveShape(newui::shapes::ShapeLayer& layer) {
    auto* curve = new newui::shapes::Curve();
    curve->points().push_back(newui::Point(30.0f, 830.0f));
    curve->points().push_back(newui::Point(180.0f, 780.0f));
    curve->points().push_back(newui::Point(330.0f, 870.0f));
    curve->points().push_back(newui::Point(480.0f, 780.0f));
    curve->points().push_back(newui::Point(630.0f, 830.0f));

    curve->style().stroke().setColor(newui::Color(0x33e0ffu, false));
    curve->style().stroke().setKind(newui::gfx::PaintKind::Color);
    curve->style().stroke().setWidth(10.0f);

    curve->style().glow().setEnabled(true);
    curve->style().glow().setColor(newui::Color(0x33e0ffu, false));
    curve->style().glow().setAmount(0.9f);
    curve->style().glow().setSoftness(16.0f);

    layer.addShape(curve);
}

// Text: buildPath() gets its geometry from BLFont::get_glyph_run_outlines()
// (shapes.cpp) - real glyph outlines, not a special-cased text draw - so
// fill/stroke/glow/dropShadow all apply to it exactly like any other
// shape's silhouette. A bold gradient-filled title plus a smaller
// stroke-only (no fill) caption underneath it, to show off both. x()/y()
// is the baseline origin, matching BLContext::fill_utf8_text()'s own
// convention (see controls.cpp/menus.cpp).
void AddTextShapes(newui::shapes::ShapeLayer& layer) {
    auto* title = new newui::shapes::Text();
    title->setText("newui::shapes");
    title->setX(30.0f);
    title->setY(985.0f);
    title->font() = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
    title->font().setSize(30.0f);
    title->font().setBold(true);

    newui::gfx::Gradient titleGradient;
    titleGradient.setKind(newui::gfx::GradientKind::Linear);
    titleGradient.setLinearStart(newui::Point(30.0f, 985.0f));
    titleGradient.setLinearEnd(newui::Point(330.0f, 985.0f));
    titleGradient.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(0x6a5acdu, false)));
    titleGradient.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(0xff6b6bu, false)));
    title->style().fill().setGradient(titleGradient);
    title->style().fill().setKind(newui::gfx::PaintKind::Gradient);

    layer.addShape(title);

    auto* caption = new newui::shapes::Text();
    caption->setText("a Shape whose geometry is real glyph outlines");
    caption->setX(30.0f);
    caption->setY(1015.0f);
    caption->font() = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
    caption->font().setSize(16.0f);

    caption->style().stroke().setColor(newui::Color(0x555555u, false));
    caption->style().stroke().setKind(newui::gfx::PaintKind::Color);
    caption->style().stroke().setWidth(1.0f);

    layer.addShape(caption);

    // Text has a Transform2D like every other shape - rotated and skewed
    // here to show that a Text's real glyph-outline geometry (not a
    // special-cased text draw) goes through the exact same transform
    // pipeline Shape::render() already applies to everything else. Local
    // geometry stays at the baseline origin (x()=y()=0) and is placed via
    // transform().setPosition() instead of a large x()/y() - same reason
    // AddSkewedSquaresShapes() does this (shapes.h's own Transform2D
    // comment): skew (and rotation about a non-default pivot) has no
    // built-in "rotate/skew about this point on the canvas" correction, so
    // transforming geometry that's already far from the local origin would
    // fling it off-screen.
    auto* transformed = new newui::shapes::Text();
    transformed->setText("transformed + skewed!");
    transformed->setX(0.0f);
    transformed->setY(0.0f);
    transformed->font() = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
    transformed->font().setSize(20.0f);
    transformed->font().setBold(true);

    transformed->style().fill().setColor(newui::Color(0x2a9d8fu, false));
    transformed->style().fill().setKind(newui::gfx::PaintKind::Color);

    transformed->transform().setPosition(newui::Point(390.0f, 1000.0f));
    transformed->transform().setRotationRadians(-0.25f);  // ~-14 degrees
    transformed->transform().setSkewX(0.3f);              // ~17 degrees

    layer.addShape(transformed);
}

// Writes layer out as JSON5 via ObjectWriter - the same single-object
// write()/json5::to_string()/std::ofstream shape unittests/test_shapes.cpp's
// own roundTrip() helper and examples/reflection2.cpp's demoDelegateAndEnum
// FileRoundTrip() both already use, just without that second one's
// writeObjects()/multi-object machinery (ShapeLayer has no delegates to
// reconnect, so a single top-level write() is all this needs). Each Shape
// in layer.shapes() writes with its own real "type" tag (Circle/Rectangle/
// ...) via TypedPropertyCollection::writeItem()'s runtime-type lookup - see
// shapes.h's own class comment on ShapeLayer for why that's what makes
// reading the file back (TypedClass<T>::read()'s polymorphic-dispatch
// branch, reflection.h) reconstruct the right concrete classes again.
void WriteShapeLayerToDisk(newui::shapes::ShapeLayer& layer, const std::string& path) {
    using namespace newui::reflection;

    ObjectWriter writer;
    writer.write(&layer);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << json5::to_string(writer.doc);
    out.close();

    printf("wrote %s\n", path.c_str());
}

int main() {
    printf("newui %s - shapes example\n", newui::version());
    printf("RoundRect, a gradient Circle, a glowing Circle, a drop-shadowed Arc, a Line, a Curve,\n");
    printf("a glassmorphism panel (card, pill button, toggle switch) over a Point-gradient backdrop,\n");
    printf("a compositingOp blend-mode Venn diagram, a Conic-gradient color wheel, rotated squares,\n");
    printf("skewed squares, a thick-stroked glowing Curve, a gradient-filled Text title, a stroke-only\n");
    printf("Text caption, and a rotated+skewed Text.\n");

    registerReflectionData();

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("shapes1");
    app.setFrame(&frame);

    frame.setTitle("Shapes Example");
    frame.setBounds(newui::Rect(10, 10, 760, 1060));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.getView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(0.0f);
    rootLayout->setPadding(0.0f);
    root.setLayout(std::move(rootLayout));

    auto* shapeView = new ShapeLayerView();
    shapeView->setVisible(true);
    shapeView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    shapeView->style().setBackgroundColor(newui::Color(0xffffffu, false));
    root.addChild(shapeView);

    newui::shapes::ShapeLayer& layer = shapeView->shapeLayer();
    AddRoundRectShape(layer);
    
    AddGradientCircleShape(layer);
    AddGlowCircleShape(layer);
    AddDropShadowArcShape(layer);
    AddLineShape(layer);
    AddCurveShape(layer);
    AddAuroraPanelShape(layer);
    AddGlassCardShape(layer);
    AddGlassButtonShape(layer);
    AddGlassToggleShape(layer);
    AddBlendModeVennShapes(layer);
    AddConicColorWheelShape(layer);
    AddRotatedSquaresShapes(layer);
    AddSkewedSquaresShapes(layer);
    AddGlowingStrokedCurveShape(layer);
    AddTextShapes(layer);

    //WriteShapeLayerToDisk(layer, "shapes1_layer.json5");

    app.run();

    return 0;
}
