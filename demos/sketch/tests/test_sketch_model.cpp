#include "sketchcolor.h"
#include "sketchdocument.h"
#include "sketchobject.h"

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
    doc.buildDemoScene();
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
