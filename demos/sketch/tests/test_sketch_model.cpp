#include "sketchcolor.h"
#include "sketchdocument.h"
#include "sketchobject.h"
#include "sketchsvg.h"

#include <filesystem>
#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

// Headless coverage of sketch's model layer: SketchColor, SketchObject (geometry, bounds, hit
// testing) and SketchDocument (ids, ordering, hit testing). Painting needs a live window.

using namespace sketch;

TEST(SketchColor, RgbRoundTripsThroughHex) {
    SketchColor color = SketchColor::fromHex("#336699");
    EXPECT_EQ(color.space(), ColorSpace::RGB);
    EXPECT_EQ(color.toHex(), "#336699");
    BLRgba32 rgba = color.toRgba32();
    EXPECT_EQ(rgba.r(), 0x33u);
    EXPECT_EQ(rgba.g(), 0x66u);
    EXPECT_EQ(rgba.b(), 0x99u);
    EXPECT_EQ(rgba.a(), 0xFFu);
}

TEST(SketchColor, HexAcceptsOptionalHashAndAlpha) {
    SketchColor color = SketchColor::fromHex("ff000080");
    EXPECT_EQ(color.toHex(), "#ff0000");
    EXPECT_NEAR(color.alpha(), 128.0f / 255.0f, 1e-4f);
}

TEST(SketchColor, MalformedHexThrows) {
    EXPECT_THROW(SketchColor::fromHex("#12345"), std::runtime_error);
    EXPECT_THROW(SketchColor::fromHex("#gg0000"), std::runtime_error);
    EXPECT_THROW(SketchColor::fromHex(""), std::runtime_error);
}

TEST(SketchColor, CmykKeepsItsSpaceAndConvertsNaively) {
    SketchColor color = SketchColor::cmyk(0.0f, 1.0f, 1.0f, 0.0f);
    EXPECT_EQ(color.space(), ColorSpace::CMYK);
    EXPECT_FLOAT_EQ(color.component(1), 1.0f);
    EXPECT_EQ(color.toHex(), "#ff0000");
    EXPECT_EQ(SketchColor::cmyk(0, 0, 0, 1).toHex(), "#000000");
    EXPECT_EQ(SketchColor::cmyk(0, 0, 0, 0).toHex(), "#ffffff");
}

TEST(SketchColor, GrayAndHsb) {
    EXPECT_EQ(SketchColor::gray(1.0f).toHex(), "#ffffff");
    EXPECT_EQ(SketchColor::gray(0.0f).toHex(), "#000000");
    EXPECT_EQ(SketchColor::hsb(0.0f, 1.0f, 1.0f).toHex(), "#ff0000");
    EXPECT_EQ(SketchColor::hsb(1.0f / 3.0f, 1.0f, 1.0f).toHex(), "#00ff00");
    // Hue wraps: a full turn is the same color.
    EXPECT_EQ(SketchColor::hsb(1.0f, 1.0f, 1.0f).toHex(), "#ff0000");
}

TEST(SketchColor, ComponentsAndAlphaClamp) {
    SketchColor color = SketchColor::rgb(2.0f, -1.0f, 0.5f, 7.0f);
    EXPECT_FLOAT_EQ(color.component(0), 1.0f);
    EXPECT_FLOAT_EQ(color.component(1), 0.0f);
    EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
    color.setAlpha(-3.0f);
    EXPECT_FLOAT_EQ(color.alpha(), 0.0f);
}

TEST(SketchColor, EqualityComparesSpaceAndComponents) {
    EXPECT_EQ(SketchColor::rgb(1, 0, 0), SketchColor::rgb(1, 0, 0));
    EXPECT_NE(SketchColor::rgb(1, 0, 0), SketchColor::rgb(0, 1, 0));
    // Same display color, different authoring space: not equal.
    EXPECT_NE(SketchColor::rgb(1, 0, 0), SketchColor::cmyk(0, 1, 1, 0));
}

TEST(SketchPaint, NoneAndSolid) {
    EXPECT_TRUE(SketchPaint::none().isNone());
    SketchPaint paint = SketchPaint::solid(SketchColor::gray(0.5f));
    EXPECT_FALSE(paint.isNone());
    EXPECT_EQ(paint.kind(), SketchPaint::Kind::Solid);
    EXPECT_EQ(paint.color(), SketchColor::gray(0.5f));
}

TEST(SketchObject, FactoriesSetTypeAndClosedness) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    EXPECT_EQ(rect->type(), SketchObjectType::Rect);
    EXPECT_TRUE(rect->isClosed());

    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 0, 10, 10));
    EXPECT_EQ(line->type(), SketchObjectType::Line);
    EXPECT_FALSE(line->isClosed());

    std::unique_ptr<SketchObject> poly(SketchObject::makePolyline({BLPoint(0, 0), BLPoint(5, 5), BLPoint(10, 0)}));
    EXPECT_FALSE(poly->isClosed());
    std::unique_ptr<SketchObject> gon(SketchObject::makePolygon({BLPoint(0, 0), BLPoint(5, 5), BLPoint(10, 0)}));
    EXPECT_TRUE(gon->isClosed());
}

TEST(SketchObject, BoundsIncludeHalfTheStroke) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(10, 20, 100, 50));
    rect->setStrokeWidth(4.0);
    BLBox box = rect->bounds();
    EXPECT_NEAR(box.x0, 8.0, 1e-6);
    EXPECT_NEAR(box.y0, 18.0, 1e-6);
    EXPECT_NEAR(box.x1, 112.0, 1e-6);
    EXPECT_NEAR(box.y1, 72.0, 1e-6);
}

TEST(SketchObject, EllipseBounds) {
    std::unique_ptr<SketchObject> ellipse(SketchObject::makeEllipse(50, 50, 30, 20));
    ellipse->setStroke(SketchPaint::none());
    BLBox box = ellipse->bounds();
    EXPECT_NEAR(box.x0, 20.0, 1e-3);
    EXPECT_NEAR(box.x1, 80.0, 1e-3);
    EXPECT_NEAR(box.y0, 30.0, 1e-3);
    EXPECT_NEAR(box.y1, 70.0, 1e-3);
}

TEST(SketchObject, HitTestFillInteriorAndMiss) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    EXPECT_TRUE(rect->hitTest(BLPoint(50, 50), 2.0));
    EXPECT_FALSE(rect->hitTest(BLPoint(150, 50), 2.0));
}

TEST(SketchObject, HitTestUnfilledShapeOnlyHitsTheStroke) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setFill(SketchPaint::none());
    EXPECT_FALSE(rect->hitTest(BLPoint(50, 50), 2.0));
    EXPECT_TRUE(rect->hitTest(BLPoint(50, 1), 2.0));
}

TEST(SketchObject, HitTestLineUsesTolerance) {
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 0, 100, 0));
    EXPECT_TRUE(line->hitTest(BLPoint(50, 2), 3.0));
    EXPECT_FALSE(line->hitTest(BLPoint(50, 20), 3.0));
}

TEST(SketchObject, SettersRejectOutOfRange) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 1, 1));
    rect->setOpacity(0.5);
    EXPECT_DOUBLE_EQ(rect->opacity(), 0.5);
    EXPECT_THROW(rect->setOpacity(3.0), std::runtime_error);
    EXPECT_THROW(rect->setOpacity(-1.0), std::runtime_error);
    EXPECT_DOUBLE_EQ(rect->opacity(), 0.5);
    EXPECT_THROW(rect->setStrokeWidth(-1.0), std::runtime_error);
}

TEST(SketchDocument, AddAssignsIncreasingIdsAndKeepsOrder) {
    SketchDocument doc;
    EXPECT_TRUE(doc.empty());
    SketchObject& a = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    SketchObject& b = doc.add(SketchObject::makeRect(5, 5, 10, 10));
    EXPECT_NE(a.id(), 0u);
    EXPECT_LT(a.id(), b.id());
    EXPECT_EQ(doc.objectCount(), 2u);
    EXPECT_EQ(&doc.object(0), &a);
    EXPECT_EQ(&doc.object(1), &b);
    EXPECT_THROW(doc.object(2), std::runtime_error);
}

TEST(SketchDocument, FindAndRemoveById) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    EXPECT_NE(doc.findById(id), nullptr);
    EXPECT_EQ(doc.findById(id + 100), nullptr);
    EXPECT_TRUE(doc.remove(id));
    EXPECT_FALSE(doc.remove(id));
    EXPECT_TRUE(doc.empty());
}

TEST(SketchDocument, IdsAreNotReusedAfterRemove) {
    SketchDocument doc;
    std::size_t first = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    doc.remove(first);
    EXPECT_GT(doc.add(SketchObject::makeRect(0, 0, 1, 1)).id(), first);
}

TEST(SketchDocument, HitTestPicksTopmostVisibleUnlocked) {
    SketchDocument doc;
    SketchObject& bottom = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchObject& top = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    EXPECT_EQ(doc.hitTest(BLPoint(50, 50), 2.0), &top);
    top.setLocked(true);
    EXPECT_EQ(doc.hitTest(BLPoint(50, 50), 2.0), &bottom);
    bottom.setVisible(false);
    EXPECT_EQ(doc.hitTest(BLPoint(50, 50), 2.0), nullptr);
}

TEST(SketchDocument, ChangesMarkItModified) {
    SketchDocument doc;
    EXPECT_FALSE(doc.isModified());
    doc.add(SketchObject::makeRect(0, 0, 1, 1));
    EXPECT_TRUE(doc.isModified());
}

TEST(SketchDocument, ClearObjectsKeepsPageSettings) {
    SketchDocument doc;
    doc.setPageSize(300, 200);
    doc.setPpi(96);
    doc.add(SketchObject::makeRect(0, 0, 10, 10));
    doc.add(SketchObject::makeEllipse(5, 5, 3, 3));
    EXPECT_FALSE(doc.empty());
    doc.clearObjects();
    EXPECT_TRUE(doc.empty());
    EXPECT_FLOAT_EQ(doc.pageWidth(), 300.0f);
    EXPECT_FLOAT_EQ(doc.pageHeight(), 200.0f);
    EXPECT_FLOAT_EQ(doc.ppi(), 96.0f);
}

TEST(SketchDocument, BadPageSizeThrows) {
    SketchDocument doc;
    EXPECT_THROW(doc.setPageSize(0, 10), std::runtime_error);
    EXPECT_THROW(doc.setPpi(-1), std::runtime_error);
}

TEST(SketchDocument, SvgIoIsNotYetAvailable) {
    SketchDocument doc;
    EXPECT_FALSE(doc.load("nonexistent.svg"));
}

TEST(SketchObject, TransformBakesIntoGeometry) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    rect->transform(BLMatrix2D::make_translation(5, 7));
    BLBox box = rect->geometryBounds();
    EXPECT_NEAR(box.x0, 5.0, 1e-9);
    EXPECT_NEAR(box.y1, 17.0, 1e-9);
    EXPECT_DOUBLE_EQ(rect->strokeWidth(), 1.0);
}

TEST(SketchObject, CloneCopiesAppearanceButNotId) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    rect->setId(9);
    rect->setStrokeWidth(3.0);
    rect->setName("r");
    std::unique_ptr<SketchObject> copy(rect->clone());
    EXPECT_EQ(copy->id(), 0u);
    EXPECT_EQ(copy->name(), "r");
    EXPECT_DOUBLE_EQ(copy->strokeWidth(), 3.0);
    EXPECT_EQ(copy->type(), SketchObjectType::Rect);
}

TEST(SketchSelection, SetFiltersUnknownAndDuplicateIds) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a, a, 999});
    ASSERT_EQ(doc.selection().size(), 1u);
    EXPECT_TRUE(doc.isSelected(a));
}

TEST(SketchSelection, RemovedObjectLeavesSelection) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    doc.remove(a);
    EXPECT_TRUE(doc.selection().empty());
}

TEST(SketchSelection, BoundsAreTheUnionOfGeometry) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 30, 10, 10)).id();
    BLBox box;
    EXPECT_FALSE(doc.selectionBounds(box));
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.selectionBounds(box));
    EXPECT_NEAR(box.x0, 0.0, 1e-9);
    EXPECT_NEAR(box.x1, 30.0, 1e-9);
    EXPECT_NEAR(box.y1, 40.0, 1e-9);
}

TEST(SketchSelection, FiresChangedOnlyWhenItChanges) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    int fired = 0;
    doc.onSelectionChanged += [&fired](SketchDocument&) { ++fired; return newui::SyncReturn::Handled; };
    doc.setSelection({a});
    doc.setSelection({a});
    EXPECT_EQ(fired, 1);
}

TEST(SketchUndo, AddUndoableUndoRedo) {
    SketchDocument doc;
    std::size_t id = doc.addUndoable(SketchObject::makeRect(0, 0, 10, 10), "Draw").id();
    EXPECT_EQ(doc.objectCount(), 1u);
    EXPECT_TRUE(doc.isSelected(id));
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 0u);
    EXPECT_TRUE(doc.selection().empty());
    doc.undoStack().redo();
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_EQ(doc.object(0).id(), id);
    EXPECT_TRUE(doc.isSelected(id));
}

TEST(SketchUndo, DeleteSelectionRestoresStackOrder) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    std::size_t b = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    std::size_t c = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    doc.setSelection({a, c});
    doc.deleteSelection();
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_EQ(doc.object(0).id(), b);
    doc.undoStack().undo();
    ASSERT_EQ(doc.objectCount(), 3u);
    EXPECT_EQ(doc.object(0).id(), a);
    EXPECT_EQ(doc.object(1).id(), b);
    EXPECT_EQ(doc.object(2).id(), c);
    EXPECT_TRUE(doc.isSelected(a));
    EXPECT_TRUE(doc.isSelected(c));
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 1u);
}

TEST(SketchUndo, DuplicateOffsetsAndSelectsCopies) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    doc.duplicateSelection(5, 5);
    ASSERT_EQ(doc.objectCount(), 2u);
    ASSERT_EQ(doc.selection().size(), 1u);
    EXPECT_NE(doc.selection()[0], a);
    EXPECT_NEAR(doc.object(1).geometryBounds().x0, 5.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 1u);
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 2u);
}

TEST(SketchUndo, TransformObjectsUndoRestoresOutline) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.transformObjects({a}, BLMatrix2D::make_translation(100, 0), "Move");
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 100.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 0.0, 1e-9);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 100.0, 1e-9);
}

TEST(SketchUndo, CommitGeometryRecordsALiveDrag) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotSelection();
    doc.findById(a)->transform(BLMatrix2D::make_scaling(2.0));  // the live drag edit
    doc.commitGeometry(before, "Resize");
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 20.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 10.0, 1e-9);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 20.0, 1e-9);
}

TEST(SketchObject, RotateAboutCenterStaysPut) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 50));
    BLMatrix2D m = BLMatrix2D::make_translation(-50, -25);  // 90 degrees about the center (50, 25)
    m.post_rotate(1.5707963267948966);
    m.post_translate(50, 25);
    rect->transform(m);
    BLBox box = rect->geometryBounds();
    EXPECT_NEAR(box.x0, 25.0, 1e-6);
    EXPECT_NEAR(box.x1, 75.0, 1e-6);
    EXPECT_NEAR(box.y0, -25.0, 1e-6);
    EXPECT_NEAR(box.y1, 75.0, 1e-6);
}

TEST(SketchFrame, UprightWhenNotRotated) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(10, 20, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_NEAR(frame.angle, 0.0, 1e-12);
    EXPECT_NEAR(frame.local.x0, 10.0, 1e-9);
    EXPECT_NEAR(frame.local.y1, 70.0, 1e-9);
}

TEST(SketchFrame, FollowsTheObjectsRotation) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    const double angle = 0.5;
    SketchObject* object = doc.findById(a);
    BLMatrix2D m = BLMatrix2D::make_translation(-50, -25);
    m.post_rotate(angle);
    m.post_translate(50, 25);
    object->transform(m);
    object->setFrameAngle(angle);

    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_NEAR(frame.angle, angle, 1e-12);
    // In the rotated frame the box is again 100 x 50, and its center is the rotation center.
    EXPECT_NEAR(frame.local.x1 - frame.local.x0, 100.0, 1e-6);
    EXPECT_NEAR(frame.local.y1 - frame.local.y0, 50.0, 1e-6);
    EXPECT_NEAR(frame.center().x, 50.0, 1e-6);
    EXPECT_NEAR(frame.center().y, 25.0, 1e-6);
    // toLocal inverts toWorld.
    BLPoint world = frame.toWorld(12.0, 34.0);
    BLPoint back = frame.toLocal(world);
    EXPECT_NEAR(back.x, 12.0, 1e-9);
    EXPECT_NEAR(back.y, 34.0, 1e-9);
}

TEST(SketchFrame, MixedAnglesFallBackToUpright) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.findById(b)->setFrameAngle(0.7);
    doc.setSelection({a, b});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.0);
}

TEST(SketchFrame, UndoRestoresTheFrameAngle) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotSelection();
    doc.findById(a)->transform(BLMatrix2D::make_rotation(0.3));
    doc.findById(a)->setFrameAngle(0.3);
    doc.commitGeometry(before, "Rotate");
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.0);
    doc.undoStack().redo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.3);
}

TEST(SketchFrame, CloneKeepsTheFrameAngle) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    rect->setFrameAngle(1.1);
    std::unique_ptr<SketchObject> copy(rect->clone());
    EXPECT_DOUBLE_EQ(copy->frameAngle(), 1.1);
}

TEST(SketchObject, WarpMapsEveryVertex) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    rect->warp([](const BLPoint& p) { return BLPoint(p.x * 2.0 + 1.0, p.y + 5.0); });
    BLBox box = rect->geometryBounds();
    EXPECT_NEAR(box.x0, 1.0, 1e-9);
    EXPECT_NEAR(box.x1, 21.0, 1e-9);
    EXPECT_NEAR(box.y0, 5.0, 1e-9);
    EXPECT_NEAR(box.y1, 15.0, 1e-9);
    EXPECT_TRUE(rect->isClosed());
}

TEST(SketchObject, WarpKeepsCurvesAndClosure) {
    std::unique_ptr<SketchObject> ellipse(SketchObject::makeEllipse(0, 0, 10, 10));
    std::size_t before = ellipse->geometry().size();
    ellipse->warp([](const BLPoint& p) { return BLPoint(p.x + 100.0, p.y); });
    EXPECT_EQ(ellipse->geometry().size(), before);
    EXPECT_NEAR(ellipse->geometryBounds().x0, 90.0, 1e-6);
}

TEST(SketchOrigin, DefaultsToTheCenterAndRidesWithTheFrame) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    BLPoint origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 50.0, 1e-9);
    EXPECT_NEAR(origin.y, 25.0, 1e-9);

    doc.setSelectionOrigin(frame, BLPoint(0, 0));  // move the pivot to the top-left corner
    origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 0.0, 1e-9);
    EXPECT_NEAR(origin.y, 0.0, 1e-9);

    // Scale the object 2x about the corner: the origin fraction keeps pointing at the same corner.
    doc.findById(a)->transform(BLMatrix2D::make_scaling(2.0));
    doc.selectionFrame(frame);
    origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 0.0, 1e-9);
    doc.findById(a)->transform(BLMatrix2D::make_translation(30, 0));
    doc.selectionFrame(frame);
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 30.0, 1e-9);
}

TEST(SketchOrigin, ResetsWhenTheSelectionChanges) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    std::size_t b = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    doc.selectionFrame(frame);
    doc.setSelectionOrigin(frame, BLPoint(0, 0));
    doc.setSelection({b});
    doc.selectionFrame(frame);
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 50.0, 1e-9);
}

TEST(SketchFrame, RotatedGroupKeepsItsOwnAngleWhenMembersDiffer) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.findById(b)->setFrameAngle(0.7);
    doc.setSelection({a, b});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.0);

    doc.setGroupAngle(0.4);
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.4);

    // A shared member angle wins over the stored group angle (they agree after a group rotation).
    doc.findById(a)->setFrameAngle(0.7);
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.7);

    // Changing the selection drops the group angle.
    doc.findById(a)->setFrameAngle(0.0);
    doc.setSelection({b, a});
    EXPECT_FALSE(doc.groupAngle().has_value());
}

TEST(SketchFrame, UndoRestoresTheGroupAngle) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.findById(b)->setFrameAngle(0.7);
    doc.setSelection({a, b});
    auto before = doc.snapshotSelection();
    FrameState groupBefore = doc.frameState();
    doc.findById(a)->setFrameAngle(0.3);
    doc.findById(b)->setFrameAngle(1.0);
    doc.setGroupAngle(0.3);
    doc.commitGeometry(before, "Rotate", groupBefore);
    doc.undoStack().undo();
    EXPECT_FALSE(doc.groupAngle().has_value());
    EXPECT_DOUBLE_EQ(doc.findById(b)->frameAngle(), 0.7);
    doc.undoStack().redo();
    ASSERT_TRUE(doc.groupAngle().has_value());
    EXPECT_DOUBLE_EQ(*doc.groupAngle(), 0.3);
}

TEST(SketchQuad, MapAndUnmapRoundTrip) {
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(100, 10);
    quad.p[2] = BLPoint(80, 90);
    quad.p[3] = BLPoint(-10, 60);
    EXPECT_NEAR(quad.map(0, 0).x, 0.0, 1e-9);
    EXPECT_NEAR(quad.map(1, 0).x, 100.0, 1e-9);
    EXPECT_NEAR(quad.map(1, 1).y, 90.0, 1e-9);
    EXPECT_NEAR(quad.map(0, 1).x, -10.0, 1e-9);
    for (double u : {0.0, 0.25, 0.5, 1.0}) {
        for (double v : {0.0, 0.3, 0.5, 1.0}) {
            double ru = -1, rv = -1;
            ASSERT_TRUE(quad.unmap(quad.map(u, v), ru, rv));
            EXPECT_NEAR(ru, u, 1e-9);
            EXPECT_NEAR(rv, v, 1e-9);
        }
    }
}

TEST(SketchQuad, RectangleFrameAsQuadMatchesItsBox) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(10, 20, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    QuadFrame quad = frame.asQuad();
    EXPECT_NEAR(quad.p[0].x, 10.0, 1e-9);
    EXPECT_NEAR(quad.p[2].y, 70.0, 1e-9);
    BLPoint mid = frame.pointAt(0.5, 0.5);
    EXPECT_NEAR(mid.x, 60.0, 1e-9);
    EXPECT_NEAR(mid.y, 45.0, 1e-9);
}

TEST(SketchQuad, FrameQuadSurvivesTransformsAndUndo) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(12, 1);
    quad.p[2] = BLPoint(10, 10);
    quad.p[3] = BLPoint(-1, 9);
    doc.setQuadFrame(quad);
    doc.transformObjects({a}, BLMatrix2D::make_translation(100, 0), "Move");
    ASSERT_TRUE(doc.quadFrame().has_value());
    EXPECT_NEAR(doc.quadFrame()->p[1].x, 112.0, 1e-9);
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    ASSERT_TRUE(frame.quad.has_value());
    doc.undoStack().undo();
    EXPECT_NEAR(doc.quadFrame()->p[1].x, 12.0, 1e-9);
    doc.setSelection({});
    EXPECT_FALSE(doc.quadFrame().has_value());
}

TEST(SketchQuad, OriginFollowsTheQuad) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(20, 0);
    quad.p[2] = BLPoint(20, 20);
    quad.p[3] = BLPoint(0, 20);
    doc.setQuadFrame(quad);
    SelectionFrame frame;
    doc.selectionFrame(frame);
    doc.setSelectionOrigin(frame, BLPoint(20, 20));
    doc.transformObjects({a}, BLMatrix2D::make_translation(5, 5), "Move");
    doc.selectionFrame(frame);
    BLPoint origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 25.0, 1e-9);
    EXPECT_NEAR(origin.y, 25.0, 1e-9);
}

TEST(SketchQuad, ResetBoundingBoxStraightensTheFrameAndUndoes) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    EXPECT_FALSE(doc.resetBoundingBox());  // nothing selected
    doc.setSelection({a, b});
    EXPECT_FALSE(doc.resetBoundingBox());  // already upright
    doc.findById(a)->setFrameAngle(0.5);
    doc.setGroupAngle(0.2);
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(30, 2);
    quad.p[2] = BLPoint(30, 10);
    quad.p[3] = BLPoint(0, 10);
    doc.setQuadFrame(quad);

    EXPECT_TRUE(doc.resetBoundingBox());
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.0);
    EXPECT_FALSE(doc.groupAngle().has_value());
    EXPECT_FALSE(doc.quadFrame().has_value());
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.0);
    EXPECT_FALSE(frame.quad.has_value());

    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.5);
    ASSERT_TRUE(doc.groupAngle().has_value());
    EXPECT_TRUE(doc.quadFrame().has_value());
}

TEST(SketchStyle, LiveEditsThenOneUndoStep) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    SketchPaint original = doc.findById(a)->fill();
    auto before = doc.snapshotStyle();
    doc.setSelectionFill(SketchPaint::solid(SketchColor::fromHex("#112233")));
    doc.setSelectionFill(SketchPaint::solid(SketchColor::fromHex("#445566")));  // a drag: many live changes
    EXPECT_TRUE(doc.commitStyle(before, "Change Fill"));
    EXPECT_EQ(doc.findById(a)->fill().color().toHex(), "#445566");
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(a)->fill(), original);
    EXPECT_FALSE(doc.undoStack().canUndo());
    doc.undoStack().redo();
    EXPECT_EQ(doc.findById(a)->fill().color().toHex(), "#445566");
}

TEST(SketchStyle, CommitWithNoDifferenceRecordsNothing) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotStyle();
    EXPECT_FALSE(doc.commitStyle(before, "Nothing"));
    EXPECT_FALSE(doc.undoStack().canUndo());
}

TEST(SketchStyle, StrokeWidthAndOpacityUndo) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotStyle();
    doc.setSelectionStrokeWidth(6.0);
    doc.setSelectionOpacity(0.25);
    doc.commitStyle(before, "Style");
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->strokeWidth(), 1.0);
    EXPECT_DOUBLE_EQ(doc.findById(a)->opacity(), 1.0);
}

TEST(SketchStyle, DefaultsApplyToNewShapesAndFireChanged) {
    SketchDocument doc;
    int changes = 0;
    doc.onChanged += [&changes](SketchDocument&) { ++changes; return newui::SyncReturn::Handled; };
    doc.setDefaultFill(SketchPaint::none());
    doc.setDefaultStrokeWidth(5.0);
    EXPECT_TRUE(doc.defaultFill().isNone());
    EXPECT_DOUBLE_EQ(doc.defaultStrokeWidth(), 5.0);
    EXPECT_EQ(changes, 2);
}

TEST(SketchStyle, PaintEquality) {
    EXPECT_EQ(SketchPaint::none(), SketchPaint::none());
    EXPECT_EQ(SketchPaint::solid(SketchColor::gray(0.5f)), SketchPaint::solid(SketchColor::gray(0.5f)));
    EXPECT_NE(SketchPaint::none(), SketchPaint::solid(SketchColor::gray(0.5f)));
}

namespace {
    // Renders one object onto a transparent w x h image and returns its pixels (0xAARRGGBB, premultiplied).
    std::vector<std::uint32_t> render(const SketchObject& object, int w, int h) {
        BLImage image(w, h, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        object.paint(ctx);
        ctx.end();
        BLImageData data;
        image.get_data(&data);
        std::vector<std::uint32_t> pixels(std::size_t(w) * std::size_t(h));
        for (int y = 0; y < h; ++y) {
            const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
            for (int x = 0; x < w; ++x) {
                pixels[std::size_t(y) * std::size_t(w) + std::size_t(x)] = row[x];
            }
        }
        return pixels;
    }

    SketchGradient redToBlue() {
        SketchGradient gradient;
        gradient.stops.push_back({0.0, SketchColor::rgb(1, 0, 0)});
        gradient.stops.push_back({1.0, SketchColor::rgb(0, 0, 1)});
        return gradient;
    }
}

TEST(SketchGradient, ColorAtInterpolatesAndClamps) {
    SketchGradient gradient = redToBlue();
    EXPECT_EQ(gradient.colorAt(-1.0).toHex(), "#ff0000");
    EXPECT_EQ(gradient.colorAt(2.0).toHex(), "#0000ff");
    SketchColor mid = gradient.colorAt(0.5);
    EXPECT_NEAR(double(mid.toRgba32().r()), 128.0, 1.5);
    EXPECT_NEAR(double(mid.toRgba32().b()), 128.0, 1.5);
}

TEST(SketchGradient, ReverseAndSort) {
    SketchGradient gradient = redToBlue();
    gradient.stops.push_back({0.25, SketchColor::rgb(0, 1, 0)});
    gradient.sortStops();
    EXPECT_DOUBLE_EQ(gradient.stops[1].offset, 0.25);
    gradient.reverse();
    EXPECT_EQ(gradient.stops.front().color.toHex(), "#0000ff");
    EXPECT_DOUBLE_EQ(gradient.stops[1].offset, 0.75);
}

TEST(SketchGradient, PaintKindsAndEquality) {
    SketchPaint linear = SketchPaint::linear(redToBlue());
    SketchPaint radial = SketchPaint::radial(redToBlue());
    EXPECT_TRUE(linear.isGradient());
    EXPECT_FALSE(linear.isNone());
    EXPECT_EQ(linear.color().toHex(), "#ff0000");  // first stop stands in for color-only callers
    EXPECT_NE(linear, radial);
    EXPECT_EQ(linear, SketchPaint::linear(redToBlue()));
    SketchGradient other = redToBlue();
    other.angleDegrees = 45.0;
    EXPECT_NE(linear, SketchPaint::linear(other));
}

TEST(SketchGradient, LinearGradientRendersLeftToRight) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::linear(redToBlue()));
    std::vector<std::uint32_t> px = render(*rect, 100, 40);
    std::uint32_t left = px[20 * 100 + 3], right = px[20 * 100 + 96];
    EXPECT_GT((left >> 16) & 0xFF, 200u);   // red end
    EXPECT_LT(left & 0xFF, 40u);
    EXPECT_GT(right & 0xFF, 200u);          // blue end
    EXPECT_LT((right >> 16) & 0xFF, 40u);
}

TEST(SketchGradient, AngleTurnsTheGradient) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 40, 100));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    gradient.angleDegrees = 90.0;  // top to bottom
    rect->setFill(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*rect, 40, 100);
    std::uint32_t top = px[3 * 40 + 20], bottom = px[96 * 40 + 20];
    EXPECT_GT((top >> 16) & 0xFF, 200u);
    EXPECT_GT(bottom & 0xFF, 200u);
}

TEST(SketchGradient, RadialGradientRunsFromCenterOut) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::radial(redToBlue()));
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    std::uint32_t center = px[50 * 100 + 50], edge = px[50 * 100 + 98];
    EXPECT_GT((center >> 16) & 0xFF, 200u);
    EXPECT_GT(edge & 0xFF, 200u);
}

TEST(SketchGradient, GradientFollowsTheObjectWhenItMoves) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::linear(redToBlue()));
    rect->transform(BLMatrix2D::make_translation(100, 0));
    std::vector<std::uint32_t> px = render(*rect, 200, 40);
    EXPECT_GT((px[20 * 200 + 103] >> 16) & 0xFF, 200u);  // still red at its (new) left edge
    EXPECT_GT(px[20 * 200 + 196] & 0xFF, 200u);
}

TEST(SketchGradient, GradientEditsUndoLikeAnyStyle) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotStyle();
    doc.setSelectionFill(SketchPaint::linear(redToBlue()));
    EXPECT_TRUE(doc.commitStyle(before, "Gradient"));
    EXPECT_TRUE(doc.findById(a)->fill().isGradient());
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(a)->fill().isGradient());
}

TEST(SketchGradient, ExplicitAxisIsKeptRelativeToTheBox) {
    SketchGradient gradient = redToBlue();
    BLBox box(10, 20, 110, 70);  // 100 x 50
    BLPoint start, end;
    gradient.axisPoints(box, false, start, end);  // derived: edge to edge through the center
    EXPECT_NEAR(start.x, 10.0, 1e-9);
    EXPECT_NEAR(end.x, 110.0, 1e-9);
    EXPECT_NEAR(start.y, 45.0, 1e-9);

    gradient.setAxis(box, BLPoint(30, 30), BLPoint(60, 70));
    EXPECT_TRUE(gradient.hasAxis);
    EXPECT_NEAR(gradient.angleDegrees, std::atan2(40.0, 30.0) * 180.0 / 3.14159265358979323846, 1e-9);
    // The same fractions over a moved and doubled box.
    gradient.axisPoints(BLBox(0, 0, 200, 100), false, start, end);
    EXPECT_NEAR(start.x, 40.0, 1e-9);
    EXPECT_NEAR(start.y, 20.0, 1e-9);
    EXPECT_NEAR(end.x, 100.0, 1e-9);
    EXPECT_NEAR(end.y, 100.0, 1e-9);

    // Turning the angle of an explicit axis keeps its midpoint and length.
    gradient.axisPoints(box, false, start, end);
    double length = std::hypot(end.x - start.x, end.y - start.y);
    gradient.setAngle(box, 0.0);
    BLPoint s2, e2;
    gradient.axisPoints(box, false, s2, e2);
    EXPECT_NEAR(s2.y, e2.y, 1e-9);
    EXPECT_NEAR(e2.x - s2.x, length, 1e-9);
    EXPECT_NEAR((s2.x + e2.x) * 0.5, (start.x + end.x) * 0.5, 1e-9);
}

TEST(SketchGradient, AxisChangesTheRenderedGradient) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    gradient.setAxis(rect->geometryBounds(), BLPoint(60, 20), BLPoint(100, 20));  // red until x = 60, then fades to blue
    rect->setFill(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*rect, 100, 40);
    EXPECT_GT((px[20 * 100 + 30] >> 16) & 0xFF, 240u);  // padded red before the start
    EXPECT_LT(px[20 * 100 + 30] & 0xFF, 15u);
    EXPECT_GT(px[20 * 100 + 97] & 0xFF, 200u);
}

TEST(SketchGradient, RadialAxisSetsCenterAndRadius) {
    SketchGradient gradient = redToBlue();
    BLBox box(0, 0, 100, 100);
    gradient.setAxis(box, BLPoint(25, 25), BLPoint(25, 75));
    BLPoint center, edge;
    gradient.axisPoints(box, true, center, edge);
    EXPECT_NEAR(center.x, 25.0, 1e-9);
    EXPECT_NEAR(std::hypot(edge.x - center.x, edge.y - center.y), 50.0, 1e-9);
}

TEST(SketchGradient, AspectSquashesARadialGradientIntoAnEllipse) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    // Center in the middle, radius 40 along x, half of that across (y).
    gradient.setAxis(rect->geometryBounds(), BLPoint(50, 50), BLPoint(90, 50));
    gradient.axisAspect = 0.5;
    rect->setFill(SketchPaint::radial(gradient));
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    // 10 px out along x (a quarter of the way) is mostly red; 25 px out along y is past the 20 px ellipse
    // edge, so fully blue - a circle of radius 40 would still be purple there.
    EXPECT_GT((px[50 * 100 + 60] >> 16) & 0xFF, px[50 * 100 + 60] & 0xFF);
    EXPECT_GT(px[75 * 100 + 50] & 0xFF, 240u);
    EXPECT_LT((px[75 * 100 + 50] >> 16) & 0xFF, 20u);

    BLMatrix2D m = gradient.axisMatrix(BLPoint(50, 50), BLPoint(90, 50), true);
    BLPoint p = m.map_point(90, 50);
    EXPECT_NEAR(p.x, 90.0, 1e-9);
    BLPoint q = m.map_point(50, 90);
    EXPECT_NEAR(q.y, 70.0, 1e-9);
}

TEST(SketchGradient, SkewLeansTheLinesOfEqualColor) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    gradient.setAxis(rect->geometryBounds(), BLPoint(0, 50), BLPoint(100, 50));
    gradient.axisSkew = 1.0;  // t = (x - (y - 50)) / 100
    rect->setFill(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    EXPECT_GT((px[95 * 100 + 50] >> 16) & 0xFF, 200u);  // lower middle: t = 0.05, red
    EXPECT_GT(px[5 * 100 + 50] & 0xFF, 200u);            // upper middle: t = 0.95, blue
}

TEST(SketchGradient, AlongStrokeFollowsThePathNotTheBox) {
    std::vector<BLPoint> corner = {BLPoint(10, 10), BLPoint(10, 90), BLPoint(90, 90)};
    std::unique_ptr<SketchObject> line(SketchObject::makePolyline(corner));
    line->setFill(SketchPaint::none());
    line->setStrokeWidth(10.0);
    SketchGradient gradient = redToBlue();
    gradient.alongStroke = true;
    line->setStroke(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    std::uint32_t start = px[15 * 100 + 10], bend = px[88 * 100 + 10], end = px[90 * 100 + 88];
    EXPECT_GT((start >> 16) & 0xFF, 200u);   // the first end is red
    EXPECT_GT(end & 0xFF, 200u);              // the last end is blue
    // Halfway along the path (at the bend) both colors are present; a box gradient would still be pure red here.
    EXPECT_GT(bend & 0xFF, 80u);
    EXPECT_GT((bend >> 16) & 0xFF, 80u);
    // The same stroke with the plain box gradient is red along the whole left leg (a left-to-right gradient).
    gradient.alongStroke = false;
    line->setStroke(SketchPaint::linear(gradient));
    px = render(*line, 100, 100);
    EXPECT_LT(px[88 * 100 + 10] & 0xFF, 60u);
}

// ---- Arrange / align / transform / lock / hide ---------------------------------------------------

namespace {
    // Three squares a, b, c (back to front) at x = 0, 100, 200.
    struct Row {
        SketchDocument doc;
        std::size_t a = 0, b = 0, c = 0;
        Row() {
            a = doc.add(SketchObject::makeRect(0, 10, 40, 40)).id();
            b = doc.add(SketchObject::makeRect(100, 20, 20, 20)).id();
            c = doc.add(SketchObject::makeRect(200, 0, 60, 80)).id();
        }
        std::vector<std::size_t> order() const {
            std::vector<std::size_t> ids;
            for (std::size_t i = 0; i < doc.objectCount(); ++i) { ids.push_back(doc.object(i).id()); }
            return ids;
        }
    };
}

TEST(SketchArrange, BringToFrontAndSendToBackUndo) {
    Row row;
    row.doc.setSelection({row.a});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringToFront));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.b, row.c, row.a}));
    row.doc.undoStack().undo();
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.a, row.b, row.c}));
    row.doc.undoStack().redo();
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.b, row.c, row.a}));
    row.doc.setSelection({row.c});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::SendToBack));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.c, row.b, row.a}));
}

TEST(SketchArrange, ForwardAndBackwardStepOneNeighborAndDoNothingAtTheEnds) {
    Row row;
    row.doc.setSelection({row.b});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.a, row.c, row.b}));
    EXPECT_FALSE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));  // already in front
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::SendBackward));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.a, row.b, row.c}));
    // Two together keep their relative order and each steps past one unselected neighbor.
    row.doc.setSelection({row.a, row.b});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.c, row.a, row.b}));
    EXPECT_FALSE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringToFront));
}

TEST(SketchAlign, ObjectsLineUpOnTheirCombinedBoundsAndUndo) {
    Row row;
    row.doc.setSelection({row.a, row.b, row.c});
    EXPECT_TRUE(row.doc.alignSelection(SketchDocument::AlignMode::Top));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().y0, 0.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().y0, 0.0, 1e-9);
    EXPECT_FALSE(row.doc.alignSelection(SketchDocument::AlignMode::Top));  // nothing left to move
    EXPECT_TRUE(row.doc.alignSelection(SketchDocument::AlignMode::Right));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().x1, 260.0, 1e-9);
    row.doc.undoStack().undo();
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().x0, 0.0, 1e-9);
    row.doc.setSelection({row.a, row.b, row.c});
    EXPECT_TRUE(row.doc.alignSelection(SketchDocument::AlignMode::Middle));
    EXPECT_NEAR((row.doc.findById(row.b)->geometryBounds().y0 + row.doc.findById(row.b)->geometryBounds().y1) * 0.5, 40.0, 1e-9);
}

TEST(SketchAlign, OneObjectAlignsToThePage) {
    SketchDocument doc;
    doc.setPageSize(300, 200);
    std::size_t id = doc.add(SketchObject::makeRect(10, 10, 100, 50)).id();
    doc.setSelection({id});
    EXPECT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Center));
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 100.0, 1e-9);
    EXPECT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Bottom));
    EXPECT_NEAR(doc.findById(id)->geometryBounds().y1, 200.0, 1e-9);
}

TEST(SketchTransform, Rotate90AndFlipTurnTheFrameToo) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 100, 20)).id();
    doc.setSelection({id});
    EXPECT_TRUE(doc.rotateSelection90(true));
    BLBox box = doc.findById(id)->geometryBounds();
    EXPECT_NEAR(box.x1 - box.x0, 20.0, 1e-6);
    EXPECT_NEAR(box.y1 - box.y0, 100.0, 1e-6);
    EXPECT_NEAR((box.x0 + box.x1) * 0.5, 50.0, 1e-6);  // about its center
    EXPECT_NEAR(doc.findById(id)->frameAngle(), 3.14159265358979323846 * 0.5, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1, 100.0, 1e-6);
    EXPECT_NEAR(doc.findById(id)->frameAngle(), 0.0, 1e-12);

    doc.add(SketchObject::makeRect(300, 0, 10, 10));
    doc.setSelection({id, doc.object(1).id()});
    EXPECT_TRUE(doc.flipSelection(true));  // mirrored about x = 155
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 210.0, 1e-6);
    EXPECT_NEAR(doc.object(1).geometryBounds().x1, 10.0, 1e-6);
    EXPECT_TRUE(doc.flipSelection(false));
    EXPECT_EQ(doc.undoStack().undoDescription(), "Flip Vertical");
}

TEST(SketchLockHide, HiddenAndLockedObjectsLeaveTheSelectionAndComeBack) {
    Row row;
    row.doc.setSelection({row.a, row.b});
    EXPECT_TRUE(row.doc.hideSelection());
    EXPECT_FALSE(row.doc.findById(row.a)->isVisible());
    EXPECT_TRUE(row.doc.selection().empty());
    EXPECT_EQ(row.doc.hitTest(BLPoint(20, 30), 1.0), nullptr);
    row.doc.setSelection({row.c});
    EXPECT_TRUE(row.doc.lockSelection());
    EXPECT_TRUE(row.doc.findById(row.c)->isLocked());
    EXPECT_TRUE(row.doc.selection().empty());
    EXPECT_TRUE(row.doc.showAll());
    EXPECT_TRUE(row.doc.findById(row.a)->isVisible());
    EXPECT_FALSE(row.doc.showAll());  // nothing hidden any more
    EXPECT_TRUE(row.doc.unlockAll());
    EXPECT_FALSE(row.doc.findById(row.c)->isLocked());
    row.doc.undoStack().undo();  // back to locked
    EXPECT_TRUE(row.doc.findById(row.c)->isLocked());
}

TEST(SketchClipboard, CopySelectionAsSvgAndPasteIsOneUndoStep) {
    SketchDocument doc;
    EXPECT_TRUE(doc.selectionToSvg().empty());
    std::size_t a = doc.add(SketchObject::makeRect(10, 10, 40, 30)).id();
    std::size_t b = doc.add(SketchObject::makeEllipse(100, 100, 20, 20)).id();
    doc.add(SketchObject::makeLine(0, 0, 5, 5));
    doc.setSelection({a, b});
    std::string svg = doc.selectionToSvg();
    ASSERT_FALSE(svg.empty());

    EXPECT_FALSE(doc.pasteSvg("not svg at all", 0, 0));
    ASSERT_TRUE(doc.pasteSvg(svg, 12, 12));
    ASSERT_EQ(doc.objectCount(), 5u);
    EXPECT_EQ(doc.selection().size(), 2u);  // the pasted objects are selected
    BLBox pasted = doc.object(3).bounds();
    BLBox original = doc.object(0).bounds();
    EXPECT_NEAR(pasted.x0 - original.x0, 12.0, 1e-6);
    EXPECT_NEAR(pasted.y0 - original.y0, 12.0, 1e-6);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 3u);
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 5u);
}

TEST(SketchStrokeOptions, WidthProfileTapersRendersAndRoundTrips) {
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 50, 100, 50));
    line->setStrokeWidth(10.0);
    line->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    StrokeOptions taper;
    taper.widths = {WidthPoint{0.0, 1.0, 1.0}, WidthPoint{1.0, 0.1, 0.1}};
    line->setStrokeOptions(taper);
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_GT(px[46 * 100 + 5] >> 24, 200u);   // wide near the start (half width ~4.5)
    EXPECT_EQ(px[46 * 100 + 95] >> 24, 0u);    // thin near the end
    EXPECT_GT(px[50 * 100 + 95] >> 24, 100u);  // still drawn on the centerline

    BLPoint p, n;
    ASSERT_TRUE(line->pathSample(0.25, p, n));
    EXPECT_NEAR(p.x, 25.0, 1e-6);
    EXPECT_NEAR(std::fabs(n.y), 1.0, 1e-6);
    double t = 0.0, d = 0.0;
    ASSERT_TRUE(line->nearestOnPath(BLPoint(60, 53), t, d));
    EXPECT_NEAR(t, 0.6, 1e-6);
    EXPECT_NEAR(d, 3.0, 1e-6);
    EXPECT_NEAR(widthScaleAt(taper.widths, 0.5, true), 0.55, 1e-6);  // the middle of the ease

    SketchDocument doc;
    doc.add(line->clone());
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    ASSERT_EQ(copy.object(0).strokeOptions().widths.size(), 2u);
    EXPECT_NEAR(copy.object(0).strokeOptions().widths[1].left, 0.1, 1e-9);
}

TEST(SketchBlend, ModesBlendWithWhatIsBelowUndoAndSave) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(10, 10, 50, 50)).id();
    doc.findById(id)->setFill(SketchPaint::solid(SketchColor::fromHex("#ffffff")));
    doc.findById(id)->setStroke(SketchPaint::none());
    doc.setSelection({id});

    auto pixelOverWhite = [&](const SketchObject& object) {
        BLImage image(80, 80, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.fill_all(BLRgba32(0xFFFFFFFFu));
        object.paint(ctx);
        ctx.end();
        BLImageData data;
        image.get_data(&data);
        return reinterpret_cast<const std::uint32_t*>(data.pixel_data)[30 * (data.stride / 4) + 30] & 0x00FFFFFFu;
    };
    EXPECT_EQ(pixelOverWhite(*doc.findById(id)), 0x00FFFFFFu);  // Normal: white over white

    auto before = doc.snapshotStyle();
    doc.setSelectionBlendMode(BlendMode::Difference);
    EXPECT_TRUE(doc.commitStyle(before, "Blend Mode"));
    EXPECT_EQ(pixelOverWhite(*doc.findById(id)), 0x00000000u);  // white difference white = black
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->blendMode(), BlendMode::Normal);
    doc.undoStack().redo();
    EXPECT_EQ(doc.findById(id)->blendMode(), BlendMode::Difference);
    std::unique_ptr<SketchObject> copyOf(doc.findById(id)->clone());
    EXPECT_EQ(copyOf->blendMode(), BlendMode::Difference);

    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    EXPECT_EQ(loaded.object(0).blendMode(), BlendMode::Difference);
    EXPECT_STREQ(blendModeName(BlendMode::ColorDodge), "Color Dodge");
    EXPECT_EQ(blendModeFromCss("soft-light"), BlendMode::SoftLight);
}

TEST(SketchGroups, GroupSelectUngroupArrangeAlignDuplicateAndSave) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(100, 0, 10, 10)).id();
    std::size_t c = doc.add(SketchObject::makeRect(200, 0, 10, 10)).id();
    doc.setSelection({a});
    EXPECT_FALSE(doc.groupSelection());  // needs two objects
    doc.setSelection({a, c});
    EXPECT_FALSE(doc.canUngroup());
    ASSERT_TRUE(doc.groupSelection());
    // The members sit together where the topmost one was: b, a, c.
    EXPECT_EQ(doc.object(0).id(), b);
    EXPECT_EQ(doc.object(1).id(), a);
    EXPECT_EQ(doc.object(2).id(), c);
    ASSERT_EQ(doc.findById(a)->groups().size(), 1u);
    EXPECT_EQ(doc.findById(a)->groups(), doc.findById(c)->groups());
    EXPECT_EQ(doc.withGroupMembers({a}), (std::vector<std::size_t>{a, c}));
    EXPECT_EQ(doc.withGroupMembers({b}), (std::vector<std::size_t>{b}));

    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(a)->groups().empty());
    EXPECT_EQ(doc.object(0).id(), a);
    doc.undoStack().redo();
    EXPECT_FALSE(doc.findById(c)->groups().empty());

    // Sending the loose object forward steps past the whole group, not into it.
    doc.setSelection({b});
    ASSERT_TRUE(doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));
    EXPECT_EQ(doc.object(0).id(), a);
    EXPECT_EQ(doc.object(1).id(), c);
    EXPECT_EQ(doc.object(2).id(), b);

    // Aligning a group and an object treats the group as one piece.
    doc.setSelection({a, c, b});
    ASSERT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Left));
    EXPECT_NEAR(doc.findById(c)->geometryBounds().x0, 200.0, 1e-9);  // c stayed with a
    EXPECT_NEAR(doc.findById(b)->geometryBounds().x0, 0.0, 1e-9);

    // The file keeps the group as a <g>.
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 3u);
    EXPECT_FALSE(loaded.object(0).groups().empty());
    EXPECT_EQ(loaded.object(0).groups(), loaded.object(1).groups());
    EXPECT_TRUE(loaded.object(2).groups().empty());

    // A duplicate is a group of its own.
    doc.setSelection({a, c});
    doc.duplicateSelection(5, 5);
    ASSERT_EQ(doc.objectCount(), 5u);
    EXPECT_FALSE(doc.object(3).groups().empty());
    EXPECT_NE(doc.object(3).groups(), doc.object(0).groups());
    EXPECT_EQ(doc.object(3).groups(), doc.object(4).groups());

    doc.setSelection({a, c});
    ASSERT_TRUE(doc.ungroupSelection());
    EXPECT_TRUE(doc.findById(a)->groups().empty());
    EXPECT_FALSE(doc.canUngroup());
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(a)->groups().empty());
}

TEST(SketchAppearance, StackPaintsInOrderHitTestsAndSaves) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(20, 20, 40, 40));
    rect->setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    rect->setStroke(SketchPaint::none());
    // A second fill on top (blue, half the rectangle's width would show nothing different - so use opacity): 50% blue.
    std::vector<AppearanceItem> items = rect->appearance();
    AppearanceItem blue = AppearanceItem::makeFill(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    blue.opacity = 0.5;
    items.push_back(blue);
    // And a thick stroke below everything: it only shows outside the fills.
    AppearanceItem wide = AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::fromHex("#00ff00")), 20.0);
    items.insert(items.begin(), wide);
    rect->setAppearance(items);
    ASSERT_EQ(rect->appearance().size(), 4u);  // wide stroke, red fill, no-paint stroke, blue fill

    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    std::uint32_t center = px[40 * 100 + 40];
    EXPECT_GT((center >> 16) & 0xFFu, 100u);   // red shows through half-transparent blue
    EXPECT_GT(center & 0xFFu, 100u);           // and so does the blue
    std::uint32_t outside = px[40 * 100 + 15]; // inside the stroke's reach, left of the rect
    EXPECT_GT((outside >> 8) & 0xFFu, 200u);   // green
    EXPECT_EQ(outside & 0xFFu, 0u);

    // Hiding the item drops it from painting and the hit test; its bounds shrink too.
    EXPECT_TRUE(rect->hitTest(BLPoint(15, 40), 0.5));  // on the wide stroke
    items = rect->appearance();
    items[0].visible = false;
    rect->setAppearance(items);
    EXPECT_FALSE(rect->hitTest(BLPoint(15, 40), 0.5));
    EXPECT_LT(rect->bounds().x1 - rect->bounds().x0, 41.0);

    // The primary accessors still mean the first fill / first stroke.
    EXPECT_EQ(rect->fill().color().toHex(), "#ff0000");
    EXPECT_EQ(rect->stroke().color().toHex(), "#00ff00");  // the first stroke is the wide one
    EXPECT_DOUBLE_EQ(rect->strokeWidth(), 20.0);

    // Undo goes through StyleState; the file keeps the whole stack.
    SketchDocument doc;
    std::size_t id = doc.add(rect->clone()).id();
    doc.setSelection({id});
    auto before = doc.snapshotStyle();
    std::vector<AppearanceItem> edited = doc.findById(id)->appearance();
    edited.pop_back();
    doc.findById(id)->setAppearance(edited);
    EXPECT_TRUE(doc.commitStyle(before, "Delete Item"));
    EXPECT_EQ(doc.findById(id)->appearance().size(), 3u);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->appearance().size(), 4u);

    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 1u);
    const std::vector<AppearanceItem>& back = loaded.object(0).appearance();
    ASSERT_EQ(back.size(), 4u);
    EXPECT_EQ(back[0].kind, AppearanceKind::Stroke);
    EXPECT_FALSE(back[0].visible);
    EXPECT_DOUBLE_EQ(back[0].strokeWidth, 20.0);
    EXPECT_EQ(back[3].kind, AppearanceKind::Fill);
    EXPECT_DOUBLE_EQ(back[3].opacity, 0.5);
}

TEST(SketchAppearance, ActiveItemEditsAddDuplicateDeleteMoveAndClear) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 50, 50)).id();
    doc.setSelection({id});
    SketchObject* object = doc.findById(id);
    ASSERT_EQ(object->appearance().size(), 2u);  // fill, stroke

    ASSERT_TRUE(doc.addAppearanceItem(AppearanceKind::Stroke));
    ASSERT_EQ(object->appearance().size(), 3u);
    EXPECT_EQ(object->activeIndex(AppearanceKind::Stroke), 2u);
    // The panels' accessors follow the active item: this edit lands on the new stroke, not the first.
    object->setStrokeWidth(9.0);
    EXPECT_DOUBLE_EQ(object->appearance()[2].strokeWidth, 9.0);
    EXPECT_DOUBLE_EQ(object->appearance()[1].strokeWidth, 1.0);

    doc.setActiveItem(AppearanceKind::Stroke, 1);  // choose the first stroke again - not an undo step
    EXPECT_DOUBLE_EQ(object->strokeWidth(), 1.0);

    doc.setActiveItem(AppearanceKind::Stroke, 2);
    ASSERT_TRUE(doc.duplicateActiveItem(AppearanceKind::Stroke));
    ASSERT_EQ(object->appearance().size(), 4u);
    EXPECT_EQ(object->activeIndex(AppearanceKind::Stroke), 3u);
    EXPECT_DOUBLE_EQ(object->appearance()[3].strokeWidth, 9.0);
    object->setStrokeWidth(5.0);  // make the copy differ so moving it is visible

    ASSERT_TRUE(doc.moveActiveItem(AppearanceKind::Stroke, -1));
    EXPECT_EQ(object->activeIndex(AppearanceKind::Stroke), 2u);
    EXPECT_FALSE(doc.moveActiveItem(AppearanceKind::Stroke, 1) && doc.moveActiveItem(AppearanceKind::Stroke, 1) && doc.moveActiveItem(AppearanceKind::Stroke, 1));  // runs off the top

    // Live per-item opacity / blend / visibility, committed as one step.
    auto before = doc.snapshotStyle();
    doc.setSelectionItemOpacity(AppearanceKind::Stroke, 0.25);
    doc.setSelectionItemBlendMode(AppearanceKind::Stroke, BlendMode::Multiply);
    doc.setSelectionItemVisible(AppearanceKind::Stroke, false);
    EXPECT_TRUE(doc.commitStyle(before, "Item"));
    const AppearanceItem& edited = object->activeItem(AppearanceKind::Stroke);
    EXPECT_DOUBLE_EQ(edited.opacity, 0.25);
    EXPECT_EQ(edited.blendMode, BlendMode::Multiply);
    EXPECT_FALSE(edited.visible);
    doc.undoStack().undo();
    EXPECT_TRUE(object->activeItem(AppearanceKind::Stroke).visible);

    // Deleting walks back to a neighbour; the last item of a kind turns None instead of vanishing.
    std::size_t before_count = object->appearance().size();
    while (object->appearance().size() > 2) {
        ASSERT_TRUE(doc.deleteActiveItem(AppearanceKind::Stroke));
    }
    EXPECT_EQ(object->appearance().size(), 2u);
    EXPECT_GT(before_count, 2u);
    ASSERT_TRUE(doc.deleteActiveItem(AppearanceKind::Stroke));  // the last stroke
    EXPECT_EQ(object->appearance().size(), 2u);
    EXPECT_TRUE(object->stroke().isNone());
    doc.undoStack().undo();
    EXPECT_FALSE(object->stroke().isNone());

    object->setOpacity(0.5);
    ASSERT_TRUE(doc.clearAppearance());
    EXPECT_TRUE(object->fill().isNone());
    EXPECT_TRUE(object->stroke().isNone());
    EXPECT_DOUBLE_EQ(object->opacity(), 1.0);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(object->opacity(), 0.5);
    EXPECT_FALSE(object->fill().isNone());
}

TEST(SketchLayers, ObjectAndGroupOpacityBlendAsOnePiece) {
    // A black fill with a black stroke overlapping it, at 50% object opacity: where they overlap the result is still
    // 50% - not 75%, as it would be if each were drawn at 50% on its own.
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(20, 20, 40, 40));
    rect->setFill(SketchPaint::solid(SketchColor::fromHex("#000000")));
    rect->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    rect->setStrokeWidth(20.0);
    rect->setOpacity(0.5);
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    std::uint32_t overlap = px[30 * 100 + 30] >> 24;   // inside the fill and within the stroke's inner half
    std::uint32_t strokeOnly = px[15 * 100 + 40] >> 24;  // outside the rect, stroke only
    EXPECT_NEAR(double(overlap), 128.0, 3.0);
    EXPECT_NEAR(double(strokeOnly), 128.0, 3.0);

    // Under a user transform with a fractional offset and a scale the layer still lines up with the direct drawing.
    BLImage image(100, 100, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    ctx.translate(3.25, 1.5);
    ctx.scale(0.5);
    SketchObject* half = rect->clone();
    half->setOpacity(0.5);  // goes through a layer under a fractional, scaled transform
    half->paint(ctx);
    ctx.end();
    delete half;

    // A group with opacity: its members blend as one, undo restores it with the objects.
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(10, 10, 40, 40)).id();
    std::size_t b = doc.add(SketchObject::makeRect(30, 30, 40, 40)).id();
    for (std::size_t id : {a, b}) {
        doc.findById(id)->setFill(SketchPaint::solid(SketchColor::fromHex("#000000")));
        doc.findById(id)->setStroke(SketchPaint::none());
    }
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    EXPECT_EQ(doc.selectedGroup(), doc.findById(a)->groups().front());
    auto before = doc.snapshotStyle();
    doc.setSelectionGroupOpacity(0.5);
    EXPECT_TRUE(doc.commitStyle(before, "Group Opacity"));
    BLImage canvas(100, 100, BL_FORMAT_PRGB32);
    {
        BLContext c2(canvas);
        c2.clear_all();
        doc.paint(c2);
        c2.end();
    }
    BLImageData data;
    canvas.get_data(&data);
    const std::uint32_t* pixels = reinterpret_cast<const std::uint32_t*>(data.pixel_data);
    std::size_t stride = std::size_t(data.stride) / 4;
    EXPECT_NEAR(double(pixels[40 * stride + 40] >> 24), 128.0, 3.0);  // the squares overlap here - still 50%
    EXPECT_NEAR(double(pixels[20 * stride + 20] >> 24), 128.0, 3.0);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.groupProps(doc.findById(a)->groups().front()).opacity, 1.0);

    // Saved and read back.
    doc.undoStack().redo();
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 2u);
    EXPECT_DOUBLE_EQ(loaded.groupProps(loaded.object(0).groups().front()).opacity, 0.5);
}

TEST(SketchEffects, ShadowBlurRoundCornersAndSave) {
    auto make = []() {
        std::unique_ptr<SketchObject> rect(SketchObject::makeRect(20, 20, 40, 40));
        rect->setFill(SketchPaint::solid(SketchColor::fromHex("#000000")));
        rect->setStroke(SketchPaint::none());
        return rect;
    };
    auto alphaAt = [](const std::vector<std::uint32_t>& px, int x, int y) { return px[std::size_t(y) * 100 + std::size_t(x)] >> 24; };

    // Drop shadow: cast behind, offset down-right (4, 4), so it shows just outside the bottom-right corner only.
    std::unique_ptr<SketchObject> shadowed = make();
    std::vector<AppearanceItem> items = shadowed->appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::DropShadow));
    shadowed->setAppearance(items);
    EXPECT_GT(shadowed->effectPad(), 18.0);  // 3 x softness + offset
    std::vector<std::uint32_t> px = render(*shadowed, 100, 100);
    EXPECT_GT(alphaAt(px, 63, 63), 20u);
    EXPECT_EQ(alphaAt(px, 6, 6), 0u);          // far up-left of the rect: no shadow there
    EXPECT_GT(alphaAt(px, 40, 40), 200u);      // the rect itself is still on top
    items[2].visible = false;
    shadowed->setAppearance(items);
    px = render(*shadowed, 100, 100);
    EXPECT_EQ(alphaAt(px, 63, 63), 0u);

    // Gaussian blur softens the edges of what is below it.
    std::unique_ptr<SketchObject> blurred = make();
    items = blurred->appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::GaussianBlur));
    blurred->setAppearance(items);
    px = render(*blurred, 100, 100);
    EXPECT_GT(alphaAt(px, 62, 40), 0u);        // bled past the edge
    EXPECT_LT(alphaAt(px, 58, 40), 255u);      // and thinned inside it
    EXPECT_GT(alphaAt(px, 40, 40), 200u);

    // Round corners cut the corner of a rectangle but not the middle of its sides.
    std::unique_ptr<SketchObject> rounded = make();
    items = rounded->appearance();
    AppearanceItem round = AppearanceItem::makeEffect(EffectKind::RoundCorners);
    round.radius = 12.0;
    items.push_back(round);
    rounded->setAppearance(items);
    px = render(*rounded, 100, 100);
    EXPECT_EQ(alphaAt(px, 21, 21), 0u);
    EXPECT_GT(alphaAt(px, 40, 21), 200u);
    EXPECT_GT(alphaAt(px, 40, 40), 200u);

    // Effects survive undo snapshots and the file.
    SketchDocument doc;
    std::size_t id = doc.add(shadowed->clone()).id();
    doc.setSelection({id});
    ASSERT_TRUE(doc.addEffect(EffectKind::RoundCorners));
    EXPECT_EQ(doc.findById(id)->appearance().size(), 4u);
    ASSERT_TRUE(doc.changeActiveEffect([](AppearanceItem& e) { e.radius = 3.0; }, "Round Corners"));
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    const std::vector<AppearanceItem>& back = loaded.object(0).appearance();
    ASSERT_EQ(back.size(), 4u);
    EXPECT_EQ(back[2].kind, AppearanceKind::Effect);
    EXPECT_EQ(back[2].effect, EffectKind::DropShadow);
    EXPECT_FALSE(back[2].visible);
    EXPECT_DOUBLE_EQ(back[3].radius, 3.0);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(id)->appearance()[3].radius, 10.0);
    ASSERT_TRUE(doc.deleteActiveItem(AppearanceKind::Effect));  // effects can all be removed
    EXPECT_EQ(doc.findById(id)->appearance().size(), 3u);
}

TEST(SketchStrokeOptions, DashesRenderAndRoundTrip) {
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 50, 100, 50));
    line->setStrokeWidth(10.0);
    line->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    StrokeOptions dashed;
    dashed.dashes = {20.0, 20.0};
    line->setStrokeOptions(dashed);
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_GT(px[50 * 100 + 10] >> 24, 200u);  // inside the first dash
    EXPECT_EQ(px[50 * 100 + 30] >> 24, 0u);    // inside the first gap
    EXPECT_GT(px[50 * 100 + 50] >> 24, 200u);  // second dash

    SketchDocument doc;
    doc.add(line->clone());
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.object(0).strokeOptions().dashes, (std::vector<double>{20.0, 20.0}));
}

TEST(SketchStrokeOptions, CapsJoinsAndMiterRenderUndoAndSave) {
    // A round cap reaches half a stroke width past the end of the line; a butt cap does not.
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(30, 50, 70, 50));
    line->setStrokeWidth(20.0);
    line->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_EQ(px[50 * 100 + 25] >> 24, 0u);  // butt: nothing left of x = 30
    StrokeOptions round;
    round.cap = StrokeCap::Round;
    line->setStrokeOptions(round);
    px = render(*line, 100, 100);
    EXPECT_GT(px[50 * 100 + 25] >> 24, 200u);
    EXPECT_EQ(px[50 * 100 + 15] >> 24, 0u);
    EXPECT_TRUE(line->hitTest(BLPoint(22, 50), 0.5));  // hit testing sees the cap too
    line->setStrokeOptions(StrokeOptions());
    EXPECT_FALSE(line->hitTest(BLPoint(22, 50), 0.5));

    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makePolyline({BLPoint(0, 0), BLPoint(50, 50), BLPoint(100, 0)})).id();
    doc.setSelection({id});
    auto before = doc.snapshotStyle();
    doc.changeSelectionStrokeOptions([](StrokeOptions& o) { o.join = StrokeJoin::Round; o.miterLimit = 3.0; });
    EXPECT_TRUE(doc.commitStyle(before, "Round Join"));
    EXPECT_EQ(doc.findById(id)->strokeOptions().join, StrokeJoin::Round);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->strokeOptions().join, StrokeJoin::Miter);
    doc.undoStack().redo();

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.object(0).strokeOptions().join, StrokeJoin::Round);
    EXPECT_DOUBLE_EQ(copy.object(0).strokeOptions().miterLimit, 3.0);
    EXPECT_EQ(copy.object(0).strokeOptions().cap, StrokeCap::Butt);
}

// ---- SVG round trip -------------------------------------------------------------------------

namespace {
    SketchDocument roundTrip(const SketchDocument& source) {
        SketchDocument copy;
        EXPECT_TRUE(documentFromSvg(svgFromDocument(source), copy));
        return copy;
    }
}

TEST(SketchSvg, PageAndObjectsRoundTrip) {
    SketchDocument doc;
    doc.setPageSize(400, 300);
    doc.setPpi(144);
    SketchObject& rect = doc.add(SketchObject::makeRoundRect(10, 20, 100, 60, 8));
    rect.setName("Card & <co>");
    rect.setFill(SketchPaint::solid(SketchColor::fromHex("#336699")));
    rect.setStroke(SketchPaint::none());
    rect.setOpacity(0.5);
    rect.setLocked(true);
    SketchObject& line = doc.add(SketchObject::makeLine(0, 0, 50, 50));
    line.setVisible(false);
    line.setStrokeWidth(3.0);

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_FLOAT_EQ(copy.pageWidth(), 400.0f);
    EXPECT_FLOAT_EQ(copy.pageHeight(), 300.0f);
    EXPECT_FLOAT_EQ(copy.ppi(), 144.0f);
    ASSERT_EQ(copy.objectCount(), 2u);
    const SketchObject& a = copy.object(0);
    EXPECT_EQ(a.type(), SketchObjectType::RoundRect);
    EXPECT_EQ(a.name(), "Card & <co>");
    EXPECT_EQ(a.fill().color().toHex(), "#336699");
    EXPECT_TRUE(a.stroke().isNone());
    EXPECT_DOUBLE_EQ(a.opacity(), 0.5);
    EXPECT_TRUE(a.isLocked());
    EXPECT_NEAR(a.geometryBounds().x0, 10.0, 1e-3);
    EXPECT_NEAR(a.geometryBounds().x1, 110.0, 1e-3);
    const SketchObject& b = copy.object(1);
    EXPECT_EQ(b.type(), SketchObjectType::Line);
    EXPECT_FALSE(b.isVisible());
    EXPECT_DOUBLE_EQ(b.strokeWidth(), 3.0);
}

TEST(SketchSvg, CmykColorsSurviveTheRoundTrip) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    o.setFill(SketchPaint::solid(SketchColor::cmyk(0.1f, 0.2f, 0.3f, 0.4f, 0.75f)));
    o.setStroke(SketchPaint::solid(SketchColor::gray(0.25f)));
    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    const SketchColor& fill = copy.object(0).fill().color();
    EXPECT_EQ(fill.space(), ColorSpace::CMYK);
    EXPECT_NEAR(fill.component(0), 0.1f, 1e-4);
    EXPECT_NEAR(fill.component(3), 0.4f, 1e-4);
    EXPECT_NEAR(fill.alpha(), 0.75f, 1e-4);
    EXPECT_EQ(copy.object(0).stroke().color().space(), ColorSpace::Gray);
}

TEST(SketchSvg, GradientsRoundTrip) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    SketchGradient g;
    g.stops.push_back({0.0, SketchColor::fromHex("#ff0000")});
    g.stops.push_back({0.4, SketchColor::cmyk(1, 0, 0, 0)});
    g.stops.push_back({1.0, SketchColor::fromHex("#0000ff80")});
    g.angleDegrees = 35.0;
    g.spread = GradientSpread::Reflect;
    o.setFill(SketchPaint::linear(g));
    o.setStroke(SketchPaint::radial(g));

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    const SketchPaint& fill = copy.object(0).fill();
    ASSERT_EQ(fill.kind(), SketchPaint::Kind::Linear);
    EXPECT_NEAR(fill.gradient().angleDegrees, 35.0, 1e-3);
    EXPECT_EQ(fill.gradient().spread, GradientSpread::Reflect);
    ASSERT_EQ(fill.gradient().stops.size(), 3u);
    EXPECT_NEAR(fill.gradient().stops[1].offset, 0.4, 1e-5);
    EXPECT_EQ(fill.gradient().stops[1].color.space(), ColorSpace::CMYK);
    EXPECT_NEAR(fill.gradient().stops[2].color.alpha(), 0.5f, 0.01f);
    EXPECT_EQ(copy.object(0).stroke().kind(), SketchPaint::Kind::Radial);
}

TEST(SketchSvg, GradientAxisRoundTripsAndFollowsTheObject) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    g.setAxis(o.geometryBounds(), BLPoint(30, 20), BLPoint(90, 50));
    o.setFill(SketchPaint::linear(g));
    SketchObject& c = doc.add(SketchObject::makeEllipse(200, 100, 40, 40));
    SketchGradient r = SketchGradient::fromColor(SketchColor::fromHex("#00ff00"));
    r.setAxis(c.geometryBounds(), BLPoint(190, 90), BLPoint(190, 130));
    c.setFill(SketchPaint::radial(r));

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const SketchObject& was = doc.object(i);
        const SketchObject& now = copy.object(i);
        bool radial = i == 1;
        EXPECT_EQ(now.fill().kind(), was.fill().kind());
        ASSERT_TRUE(now.fill().gradient().hasAxis);
        BLPoint s0, e0, s1, e1;
        was.fill().gradient().axisPoints(was.geometryBounds(), radial, s0, e0);
        now.fill().gradient().axisPoints(now.geometryBounds(), radial, s1, e1);
        EXPECT_NEAR(s0.x, s1.x, 1e-3);
        EXPECT_NEAR(s0.y, s1.y, 1e-3);
        EXPECT_NEAR(e0.x, e1.x, 1e-3);
        EXPECT_NEAR(e0.y, e1.y, 1e-3);
    }
    // The axis is relative to the box, so moving the object moves it along.
    copy.object(0).transform(BLMatrix2D::make_translation(50, 0));
    BLPoint moved, ignored;
    copy.object(0).fill().gradient().axisPoints(copy.object(0).geometryBounds(), false, moved, ignored);
    EXPECT_NEAR(moved.x, 80.0, 1e-3);
}

TEST(SketchSvg, ForeignGradientVectorsAreKept) {
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<defs><linearGradient id='a' gradientUnits='userSpaceOnUse' x1='20' y1='10' x2='60' y2='10'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient>"
        "<radialGradient id='b' cx='0.25' cy='0.5' r='0.25'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
        "<rect x='10' y='0' width='100' height='20' fill='url(#a)'/>"
        "<rect x='0' y='0' width='40' height='40' fill='url(#b)'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 2u);
    BLPoint start, end;
    doc.object(0).fill().gradient().axisPoints(doc.object(0).geometryBounds(), false, start, end);
    EXPECT_NEAR(start.x, 20.0, 1e-6);
    EXPECT_NEAR(end.x, 60.0, 1e-6);
    EXPECT_NEAR(start.y, 10.0, 1e-6);
    doc.object(1).fill().gradient().axisPoints(doc.object(1).geometryBounds(), true, start, end);
    EXPECT_NEAR(start.x, 10.0, 1e-6);   // 0.25 of 40
    EXPECT_NEAR(start.y, 20.0, 1e-6);
    EXPECT_NEAR(end.x - start.x, 10.0, 1e-6);  // r = 0.25 of 40
}

TEST(SketchSvg, EllipticalRadialAspectRoundTrips) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    g.setAxis(o.geometryBounds(), BLPoint(50, 50), BLPoint(80, 80));
    g.axisAspect = 0.4;
    o.setFill(SketchPaint::radial(g));
    std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("gradientTransform"), std::string::npos);  // other viewers see the ellipse too
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    EXPECT_NEAR(copy.object(0).fill().gradient().axisAspect, 0.4, 1e-4);
}

TEST(SketchSvg, ForeignGradientTransformBecomesAxisAndAspect) {
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<defs><radialGradient id='b' gradientUnits='userSpaceOnUse' cx='50' cy='50' r='20' gradientTransform='translate(0 0) rotate(0 50 50) scale(1 1)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient>"
        "<linearGradient id='a' gradientUnits='userSpaceOnUse' x1='0' y1='0' x2='10' y2='0' gradientTransform='translate(20 30) scale(2)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient>"
        "<radialGradient id='c' gradientUnits='userSpaceOnUse' cx='0' cy='0' r='10' gradientTransform='translate(50 50) scale(3 1.5)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
        "<rect width='100' height='100' fill='url(#b)'/><rect width='100' height='100' fill='url(#a)'/><rect width='100' height='100' fill='url(#c)'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 3u);
    BLPoint start, end;
    doc.object(1).fill().gradient().axisPoints(doc.object(1).geometryBounds(), false, start, end);
    EXPECT_NEAR(start.x, 20.0, 1e-6);
    EXPECT_NEAR(start.y, 30.0, 1e-6);
    EXPECT_NEAR(end.x, 40.0, 1e-6);  // 10 * 2 + 20
    doc.object(2).fill().gradient().axisPoints(doc.object(2).geometryBounds(), true, start, end);
    EXPECT_NEAR(start.x, 50.0, 1e-6);
    EXPECT_NEAR(end.x - start.x, 30.0, 1e-6);  // r = 10 scaled 3x along x
    EXPECT_NEAR(doc.object(2).fill().gradient().axisAspect, 0.5, 1e-6);  // 1.5 / 3
}

TEST(SketchSvg, SkewAndAlongStrokeRoundTrip) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    g.setAxis(o.geometryBounds(), BLPoint(0, 50), BLPoint(100, 50));
    g.axisSkew = 0.75;
    o.setFill(SketchPaint::linear(g));
    SketchGradient along = SketchGradient::fromColor(SketchColor::fromHex("#00ff00"));
    along.alongStroke = true;
    o.setStroke(SketchPaint::linear(along));
    std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("gradientTransform"), std::string::npos);
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    EXPECT_NEAR(copy.object(0).fill().gradient().axisSkew, 0.75, 1e-4);
    EXPECT_TRUE(copy.object(0).stroke().gradient().alongStroke);
    EXPECT_FALSE(copy.object(0).fill().gradient().alongStroke);
}

TEST(SketchSvg, ForeignSkewIsKept) {
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<defs><linearGradient id='a' gradientUnits='userSpaceOnUse' x1='0' y1='0' x2='100' y2='0' gradientTransform='skewX(45)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient>"
        "<radialGradient id='b' gradientUnits='userSpaceOnUse' cx='50' cy='50' r='20' gradientTransform='rotate(0 50 50) translate(50 50) skewX(30) translate(-50 -50)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
        "<rect width='100' height='100' fill='url(#a)'/><rect width='100' height='100' fill='url(#b)'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 2u);
    EXPECT_NEAR(doc.object(0).fill().gradient().axisSkew, 1.0, 1e-6);
    EXPECT_NEAR(doc.object(1).fill().gradient().axisSkew, std::tan(30.0 * 3.14159265358979323846 / 180.0), 1e-6);
    EXPECT_NEAR(doc.object(1).fill().gradient().axisAspect, 1.0, 1e-6);
}

TEST(SketchSvg, RotatedObjectsKeepTheirFrameAngle) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 40, 20));
    o.transform(BLMatrix2D::make_rotation(0.5, 0.0, 0.0));
    o.setFrameAngle(0.5);
    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    EXPECT_NEAR(copy.object(0).frameAngle(), 0.5, 1e-5);
    EXPECT_NEAR(copy.object(0).geometryBounds().x1, o.geometryBounds().x1, 1e-3);
}

TEST(SketchSvg, ReadsForeignSvgShapesGroupsAndTransforms) {
    const char* svg =
        "<?xml version='1.0'?><!-- hi --><svg xmlns='http://www.w3.org/2000/svg' width='200' height='100' viewBox='0 0 100 50'>"
        "<defs><linearGradient id='g'><stop offset='0' stop-color='red'/><stop offset='100%' style='stop-color:#00f'/></linearGradient></defs>"
        "<g fill='#0f0' stroke='black' transform='translate(10,5)'>"
        "<rect x='0' y='0' width='20' height='10' rx='2'/>"
        "<circle cx='50' cy='20' r='5' fill='url(#g)'/>"
        "<path d='M0 0 h10 v10 h-10 z m20 0 l5 5' style='fill:rgb(255,0,0);stroke:none'/>"
        "<polygon points='0,0 10,0 5,8'/>"
        "<text>skipped</text>"
        "</g></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    EXPECT_FLOAT_EQ(doc.pageWidth(), 200.0f);
    ASSERT_EQ(doc.objectCount(), 4u);
    EXPECT_EQ(doc.object(0).type(), SketchObjectType::RoundRect);
    // viewBox scale 2 and translate(10,5): the rect's left edge lands at x = 20.
    EXPECT_NEAR(doc.object(0).geometryBounds().x0, 20.0, 1e-3);
    EXPECT_EQ(doc.object(0).fill().color().toHex(), "#00ff00");
    EXPECT_EQ(doc.object(1).type(), SketchObjectType::Ellipse);
    EXPECT_EQ(doc.object(1).fill().kind(), SketchPaint::Kind::Linear);
    EXPECT_EQ(doc.object(1).fill().gradient().stops.size(), 2u);
    EXPECT_EQ(doc.object(2).fill().color().toHex(), "#ff0000");
    EXPECT_TRUE(doc.object(2).stroke().isNone());
    EXPECT_EQ(doc.object(3).type(), SketchObjectType::Polygon);
}

TEST(SketchSvg, PathCommandsAndArcsParse) {
    const char* svg = "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<path d='M10,10 C 20,0 30,0 40,10 S 60,20 70,10 Q 80,0 90,10 T 95,15 A 5 5 0 0 1 90 20 L 10 20 Z'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 1u);
    BLBox b = doc.object(0).geometryBounds();
    EXPECT_NEAR(b.x0, 10.0, 1e-3);
    EXPECT_GE(b.x1, 94.9);
    EXPECT_GE(b.y1, 19.9);
}

TEST(SketchSvg, MalformedInputIsRejectedAndLeavesTheDocumentAlone) {
    SketchDocument doc;
    doc.add(SketchObject::makeRect(0, 0, 5, 5));
    EXPECT_FALSE(documentFromSvg("<svg><g></svg>", doc));
    EXPECT_FALSE(documentFromSvg("not xml at all", doc));
    EXPECT_FALSE(documentFromSvg("<html></html>", doc));
    EXPECT_EQ(doc.objectCount(), 1u);
}

TEST(SketchSvg, SavesAndLoadsThroughTheDocumentFileApi) {
    std::string path = (std::filesystem::temp_directory_path() / "sketch_test_roundtrip.svg").string();
    {
        SketchDocument doc;
        doc.add(SketchObject::makeEllipse(50, 50, 20, 10));
        ASSERT_TRUE(doc.save(path));
        EXPECT_FALSE(doc.isModified());
    }
    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path));
    EXPECT_EQ(loaded.objectCount(), 1u);
    EXPECT_EQ(loaded.filePath(), path);
    EXPECT_FALSE(loaded.isModified());
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".bak");
    EXPECT_FALSE(loaded.load("Z:/definitely/not/here.svg"));
}

// ---- Color conversion and the document color mode ---------------------------------------------

TEST(SketchColorConvert, RgbToCmykAndBackIsExact) {
    SketchColor rgb = SketchColor::rgb(0.2f, 0.4f, 0.6f, 0.5f);
    SketchColor cmyk = rgb.convertedTo(ColorSpace::CMYK);
    EXPECT_EQ(cmyk.space(), ColorSpace::CMYK);
    EXPECT_NEAR(cmyk.component(3), 0.4f, 1e-5);  // K = 1 - max
    EXPECT_FLOAT_EQ(cmyk.alpha(), 0.5f);
    SketchColor back = cmyk.convertedTo(ColorSpace::RGB);
    EXPECT_NEAR(back.component(0), 0.2f, 1e-5);
    EXPECT_NEAR(back.component(1), 0.4f, 1e-5);
    EXPECT_NEAR(back.component(2), 0.6f, 1e-5);
}

TEST(SketchColorConvert, BlackAndWhiteInCmyk) {
    SketchColor black = SketchColor::rgb(0, 0, 0).convertedTo(ColorSpace::CMYK);
    EXPECT_FLOAT_EQ(black.component(3), 1.0f);
    EXPECT_FLOAT_EQ(black.component(0), 0.0f);
    SketchColor white = SketchColor::rgb(1, 1, 1).convertedTo(ColorSpace::CMYK);
    EXPECT_FLOAT_EQ(white.component(3), 0.0f);
}

TEST(SketchColorConvert, HsbAndGrayRoundTripThroughRgb) {
    SketchColor red = SketchColor::rgb(1, 0, 0).convertedTo(ColorSpace::HSB);
    EXPECT_NEAR(red.component(0), 0.0f, 1e-5);
    EXPECT_NEAR(red.component(1), 1.0f, 1e-5);
    SketchColor teal = SketchColor::rgb(0.1f, 0.6f, 0.5f);
    SketchColor back = teal.convertedTo(ColorSpace::HSB).convertedTo(ColorSpace::RGB);
    EXPECT_NEAR(back.component(0), 0.1f, 1e-4);
    EXPECT_NEAR(back.component(1), 0.6f, 1e-4);
    EXPECT_NEAR(back.component(2), 0.5f, 1e-4);
    EXPECT_NEAR(SketchColor::rgb(0.5f, 0.5f, 0.5f).convertedTo(ColorSpace::Gray).component(0), 0.5f, 1e-5);
    EXPECT_EQ(SketchColor::gray(0.3f).convertedTo(ColorSpace::Gray), SketchColor::gray(0.3f));
}

TEST(SketchDocument, ColorModeConversionIsOneUndoStep) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    SketchGradient g;
    g.stops.push_back({0.0, SketchColor::rgb(1, 0, 0)});
    g.stops.push_back({1.0, SketchColor::rgb(0, 0, 1)});
    o.setFill(SketchPaint::linear(g));
    o.setStroke(SketchPaint::solid(SketchColor::rgb(0.2f, 0.4f, 0.6f)));
    std::size_t id = o.id();
    doc.undoStack().clear();

    EXPECT_FALSE(doc.convertToColorMode(ColorSpace::RGB));  // already RGB
    EXPECT_TRUE(doc.convertToColorMode(ColorSpace::CMYK));
    EXPECT_EQ(doc.colorMode(), ColorSpace::CMYK);
    EXPECT_EQ(doc.findById(id)->stroke().color().space(), ColorSpace::CMYK);
    EXPECT_EQ(doc.findById(id)->fill().gradient().stops[1].color.space(), ColorSpace::CMYK);
    EXPECT_EQ(doc.defaultFill().color().space(), ColorSpace::CMYK);

    doc.undoStack().undo();
    EXPECT_EQ(doc.colorMode(), ColorSpace::RGB);
    EXPECT_EQ(doc.findById(id)->stroke().color().space(), ColorSpace::RGB);
    EXPECT_EQ(doc.defaultFill().color().space(), ColorSpace::RGB);
    EXPECT_FALSE(doc.undoStack().canUndo());
    doc.undoStack().redo();
    EXPECT_EQ(doc.colorMode(), ColorSpace::CMYK);
}

TEST(SketchDocument, OnlyRgbAndCmykAreDocumentModes) {
    SketchDocument doc;
    EXPECT_THROW(doc.setColorMode(ColorSpace::Gray), std::runtime_error);
    EXPECT_THROW(doc.convertToColorMode(ColorSpace::HSB), std::runtime_error);
}

TEST(SketchSvg, ColorModeRoundTrips) {
    SketchDocument doc;
    doc.setColorMode(ColorSpace::CMYK);
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.colorMode(), ColorSpace::CMYK);
    EXPECT_EQ(copy.defaultFill().color().space(), ColorSpace::CMYK);
    // A file without the attribute is an RGB document, even when loaded over a CMYK one.
    ASSERT_TRUE(documentFromSvg("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'/>", copy));
    EXPECT_EQ(copy.colorMode(), ColorSpace::RGB);
}

// ---- New Document model --------------------------------------------------------------------------

#include "newdocumentmodel.h"

TEST(NewDocumentModel, StartsAsLetterCmyk) {
    NewDocumentModel model;
    EXPECT_EQ(model.name(), "Untitled-1");
    EXPECT_NEAR(model.widthPoints(), 612.0, 1e-6);
    EXPECT_NEAR(model.heightPoints(), 792.0, 1e-6);
    EXPECT_EQ(model.colorMode(), ColorSpace::CMYK);
    EXPECT_EQ(model.pixelWidth(), 2550);
    EXPECT_EQ(model.pixelHeight(), 3300);
    EXPECT_FALSE(model.isLandscape());
}

TEST(NewDocumentModel, LoadSetupShowsAnExistingPageInPoints) {
    NewDocumentModel model;
    model.loadSetup(595.0, 842.0, 150.0, ColorSpace::RGB, PageBackground::Transparent);
    EXPECT_EQ(model.unit(), LengthUnit::Points);
    EXPECT_DOUBLE_EQ(model.widthPoints(), 595.0);
    EXPECT_DOUBLE_EQ(model.heightPoints(), 842.0);
    EXPECT_DOUBLE_EQ(model.ppi(), 150.0);
    EXPECT_EQ(model.colorMode(), ColorSpace::RGB);
    EXPECT_EQ(model.background(), PageBackground::Transparent);
    model.setUnit(LengthUnit::Inches);  // converting keeps the page the same physical size
    EXPECT_NEAR(model.width(), 595.0 / 72.0, 1e-9);
    EXPECT_NEAR(model.widthPoints(), 595.0, 1e-9);
}

TEST(NewDocumentModel, UnitChangeKeepsThePhysicalSize) {
    NewDocumentModel model;
    model.setUnit(LengthUnit::Millimeters);
    EXPECT_NEAR(model.width(), 215.9, 0.01);
    EXPECT_NEAR(model.height(), 279.4, 0.01);
    EXPECT_NEAR(model.widthPoints(), 612.0, 1e-6);
    model.setUnit(LengthUnit::Pixels);  // 300 ppi
    EXPECT_NEAR(model.width(), 2550.0, 1e-6);
    EXPECT_NEAR(model.widthPoints(), 612.0, 1e-6);
}

TEST(NewDocumentModel, PixelUnitsFollowThePpi) {
    NewDocumentModel model;
    model.loadPreset(NewDocumentModel::presets()[10]);  // Full HD, px @ 72
    EXPECT_NEAR(model.widthPoints(), 1920.0, 1e-6);
    model.setPpi(144.0);  // same pixel count, half the physical size
    EXPECT_NEAR(model.widthPoints(), 960.0, 1e-6);
    EXPECT_EQ(model.pixelWidth(), 1920);
    model.setPpi(-5.0);  // ignored
    EXPECT_NEAR(model.ppi(), 144.0, 1e-9);
}

TEST(NewDocumentModel, OrientationSwapsOnlyWhenNeeded) {
    NewDocumentModel model;  // 8.5 x 11 portrait
    model.setLandscape(false);
    EXPECT_NEAR(model.width(), 8.5, 1e-9);
    model.setLandscape(true);
    EXPECT_NEAR(model.width(), 11.0, 1e-9);
    EXPECT_NEAR(model.height(), 8.5, 1e-9);
    EXPECT_TRUE(model.isLandscape());
    model.setLandscape(true);
    EXPECT_NEAR(model.width(), 11.0, 1e-9);
}

TEST(NewDocumentModel, KeepProportionsScalesTheOtherSide) {
    NewDocumentModel model;
    model.loadPreset(NewDocumentModel::presets()[19]);  // App icon would be square; use A4 instead
    model.loadPreset(NewDocumentModel::presets()[4]);   // A4 210 x 297 mm
    model.setKeepProportions(true);
    model.setWidth(105.0);
    EXPECT_NEAR(model.height(), 148.5, 0.01);
    model.setHeight(594.0);
    EXPECT_NEAR(model.width(), 420.0, 0.01);
    model.setKeepProportions(false);
    model.setWidth(100.0);
    EXPECT_NEAR(model.height(), 594.0, 1e-9);
    model.setWidth(0.0);  // ignored
    EXPECT_NEAR(model.width(), 100.0, 1e-9);
}

TEST(NewDocumentModel, PresetsAndCategories) {
    std::vector<std::string> categories = NewDocumentModel::categories();
    ASSERT_EQ(categories.size(), 4u);
    EXPECT_EQ(categories[0], "Print");
    EXPECT_EQ(categories[3], "Art");
    EXPECT_EQ(NewDocumentModel::presetsIn("Print").size(), 10u);
    EXPECT_EQ(NewDocumentModel::presetsIn("Web").size(), 6u);
    EXPECT_EQ(NewDocumentModel::rowText(NewDocumentModel::presets()[0]), "Letter   8.5 x 11 in   CMYK, 300 ppi");
}

TEST(NewDocumentModel, SummaryAndLabels) {
    NewDocumentModel model;
    EXPECT_EQ(model.widthLabel(), "8.5 in");
    EXPECT_EQ(model.heightLabel(), "11 in");
    EXPECT_EQ(model.summary(), "612 x 792 pt  =  8.5 x 11 in  =  215.9 x 279.4 mm\n2550 x 3300 px at 300 ppi  -  CMYK  -  Portrait");
}

TEST(NewDocumentModel, OnlyRgbAndCmykColorModes) {
    NewDocumentModel model;
    model.setColorMode(ColorSpace::HSB);
    EXPECT_EQ(model.colorMode(), ColorSpace::RGB);
    model.setColorMode(ColorSpace::CMYK);
    EXPECT_EQ(model.colorMode(), ColorSpace::CMYK);
}

TEST(SketchSvg, PageBackgroundRoundTrips) {
    SketchDocument doc;
    EXPECT_TRUE(doc.pageBackground().has_value());  // white by default
    doc.add(SketchObject::makeRect(0, 0, 5, 5));
    std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("id=\"pageBackground\""), std::string::npos);  // a real rect for other viewers

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    EXPECT_EQ(copy.objectCount(), 1u);  // the background rect is not an object
    ASSERT_TRUE(copy.pageBackground().has_value());
    EXPECT_EQ(copy.pageBackground()->toHex(), "#ffffff");

    doc.setPageBackground(SketchColor::cmyk(0.1f, 0.0f, 0.0f, 0.0f));
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.pageBackground()->space(), ColorSpace::CMYK);

    doc.setPageBackground(std::nullopt);
    std::string transparent = svgFromDocument(doc);
    EXPECT_EQ(transparent.find("id=\"pageBackground\""), std::string::npos);
    ASSERT_TRUE(documentFromSvg(transparent, copy));
    EXPECT_FALSE(copy.pageBackground().has_value());
}
