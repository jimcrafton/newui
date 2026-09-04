#include "newui/viewbuilder.h"
#include "newui/subview.h"
#include "newui/controls.h"

#include <gtest/gtest.h>

TEST(ViewBuilder, FluentSettersApplyToDefaultConstructedView) {
    auto* view = newui::ViewBuilder<newui::SubView>()
        .name("myView")
        .bounds(newui::Rect(1, 2, 30, 40))
        .visible(false)
        .build();

    EXPECT_EQ(view->name(), "myView");
    EXPECT_EQ(view->bounds(), newui::Rect(1, 2, 30, 40));
    EXPECT_FALSE(view->isVisible());

    delete view;
}

TEST(ViewBuilder, VisibleDefaultsToTrueWithNoArgument) {
    auto* view = newui::ViewBuilder<newui::SubView>().visible().build();

    EXPECT_TRUE(view->isVisible());

    delete view;
}

TEST(ViewBuilder, WrapsAlreadyConstructedInstanceInsteadOfBuildingANewOne) {
    auto* existing = new newui::SubView();

    auto* view = newui::ViewBuilder<newui::SubView>(existing).name("wrapped").build();

    EXPECT_EQ(view, existing);
    EXPECT_EQ(view->name(), "wrapped");

    delete view;
}

TEST(ViewBuilder, LayoutParamsAttachesRealLayoutParamsInstance) {
    auto* child = newui::ViewBuilder<newui::SubView>()
        .layoutParams(std::make_unique<newui::FlexLayoutParams>(2.0f))
        .build();

    auto* params = dynamic_cast<newui::FlexLayoutParams*>(child->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->weight, 2.0f);

    delete child;
}

TEST(ViewBuilder, TemplatedLayoutParamsConfiguresPublicFieldsInPlace) {
    auto* child = newui::ViewBuilder<newui::SubView>()
        .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& p) {
            p.anchors = newui::Anchor::Left | newui::Anchor::Top;
            p.leftMargin = 4.0f;
        })
        .build();

    auto* params = dynamic_cast<newui::AnchorLayoutParams*>(child->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Left));
    EXPECT_FLOAT_EQ(params->leftMargin, 4.0f);

    delete child;
}

TEST(ViewBuilder, TemplatedLayoutConfiguresRealLayoutBeforeAttaching) {
    auto* view = newui::ViewBuilder<newui::SubView>()
        .layout<newui::FlexLayout>([](newui::FlexLayout& l) {
            l.setOrientation(newui::Orientation::Horizontal);
            l.setSpacing(8.0f);
        })
        .build();

    auto* flex = dynamic_cast<newui::FlexLayout*>(view->layout());
    ASSERT_NE(flex, nullptr);
    EXPECT_EQ(flex->orientation(), newui::Orientation::Horizontal);
    EXPECT_FLOAT_EQ(flex->spacing(), 8.0f);

    delete view;
}

TEST(ViewBuilder, TemplatedStyleConfiguresRealStyleBeforeAttaching) {
    auto* view = newui::ViewBuilder<newui::SubView>()
        .style<newui::ViewStyle>([](newui::ViewStyle& s) { s.borderWidth = 3.0f; })
        .build();

    EXPECT_FLOAT_EQ(view->style().borderWidth, 3.0f);

    delete view;
}

TEST(ViewBuilder, ConfigureRunsArbitraryCallbackAgainstConcreteType) {
    auto* label = newui::ViewBuilder<newui::Label>()
        .configure([](newui::Label& l) { l.setText("hello"); })
        .build();

    EXPECT_EQ(label->text(), "hello");

    delete label;
}

TEST(ViewBuilder, ChildWithConfigureBuildsAndAttachesAChild) {
    auto* parent = newui::ViewBuilder<newui::SubView>()
        .name("parent")
        .child<newui::Label>([](newui::ViewBuilder<newui::Label>& b) {
            b.name("childLabel").configure([](newui::Label& l) { l.setText("hi"); });
        })
        .build();

    ASSERT_EQ(parent->childViews().size(), 1u);
    auto* child = dynamic_cast<newui::Label*>(parent->childViews()[0]);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->name(), "childLabel");
    EXPECT_EQ(child->text(), "hi");
    EXPECT_EQ(child->parent(), parent);

    delete parent;
}

TEST(ViewBuilder, ChildWithNoConfigureAttachesAPlainDefaultChild) {
    auto* parent = newui::ViewBuilder<newui::SubView>()
        .child<newui::SubView>()
        .build();

    EXPECT_EQ(parent->childViews().size(), 1u);

    delete parent;
}

TEST(ViewBuilder, ChildAttachesAnAlreadyBuiltSubtree) {
    auto* separatelyBuilt = newui::ViewBuilder<newui::SubView>().name("prebuilt").build();

    auto* parent = newui::ViewBuilder<newui::SubView>()
        .child(separatelyBuilt)
        .build();

    ASSERT_EQ(parent->childViews().size(), 1u);
    EXPECT_EQ(parent->childViews()[0], separatelyBuilt);

    delete parent;
}

TEST(ViewBuilder, NestedChildrenBuildAMultiLevelTree) {
    auto* root = newui::ViewBuilder<newui::SubView>()
        .name("root")
        .child<newui::SubView>([](newui::ViewBuilder<newui::SubView>& mid) {
            mid.name("mid").child<newui::Label>([](newui::ViewBuilder<newui::Label>& leaf) {
                leaf.name("leaf");
            });
        })
        .build();

    ASSERT_EQ(root->childViews().size(), 1u);
    auto* mid = root->childViews()[0];
    EXPECT_EQ(mid->name(), "mid");
    ASSERT_EQ(mid->childViews().size(), 1u);
    EXPECT_EQ(mid->childViews()[0]->name(), "leaf");

    delete root;
}

TEST(ViewBuilder, ImplicitConversionToViewTPointerWorks) {
    newui::SubView* view = newui::ViewBuilder<newui::SubView>().name("implicit");

    EXPECT_EQ(view->name(), "implicit");

    delete view;
}
