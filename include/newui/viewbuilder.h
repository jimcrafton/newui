#pragma once

#include <type_traits>

#include <newui/subview.h>
#include <newui/view.h>

namespace newui {

// Fluent construction helper for a View/SubView tree in code - mirrors
// reflection::ClassBuilder<T>'s chainable-setter shape (reflection.h), but
// builds a real live instance instead of registering reflection metadata.
//
// View/SubView are heap-only, parent-owned-by-raw-pointer (see View's own
// class comment: a parent's childViews_ delete()s them in its destructor).
// ViewBuilder doesn't change that convention - build() just hands back the
// ViewT* built so far, unattached to anything until addChild() runs on it
// (child<ChildT>() below does that automatically for a nested build).
//
//@reflect ignore=true
template<typename ViewT>
class ViewBuilder {
    static_assert(std::is_base_of_v<View, ViewT>, "ViewBuilder<ViewT>: ViewT must derive from newui::View");

public:
    ViewBuilder() : view_(new ViewT()) {}

    // Wrap an already-constructed instance (e.g. one obtained from
    // reflection::Class::createInstanceTyped<ViewT>(), as the Toolbox's
    // control registry does) instead of default-constructing a new one.
    explicit ViewBuilder(ViewT* existing) : view_(existing) {}

    ViewBuilder& name(std::string name) { view_->setName(std::move(name)); return *this; }
    ViewBuilder& bounds(const Rect& bounds) { view_->setBounds(bounds); return *this; }
    ViewBuilder& visible(bool visible = true) { view_->setVisible(visible); return *this; }
    ViewBuilder& desiredSize(const Size& size) { view_->setDesiredSize(size); return *this; }

    // Attach an already-built Layout/ViewStyle rather than constructing one
    // inline - see the templated layout<LayoutT>()/style<StyleT>() overloads
    // below for building+configuring one in place.
    ViewBuilder& layout(std::unique_ptr<Layout> layout) { view_->setLayout(std::move(layout)); return *this; }
    ViewBuilder& style(std::unique_ptr<ViewStyle> style) { view_->setStyle(std::move(style)); return *this; }

    // Constructs a LayoutT, configures it via fn(LayoutT&) (e.g.
    // FlexLayout::setOrientation()/setSpacing()), then setLayout()s it.
    template<typename LayoutT, typename Fn>
    ViewBuilder& layout(Fn&& fn) {
        static_assert(std::is_base_of_v<Layout, LayoutT>,
            "ViewBuilder<ViewT>::layout<LayoutT>(): LayoutT must derive from newui::Layout");
        auto instance = std::make_unique<LayoutT>();
        fn(*instance);
        view_->setLayout(std::move(instance));
        return *this;
    }

    // Same, with no configuration - just attach a plain default-constructed LayoutT.
    template<typename LayoutT>
    ViewBuilder& layout() {
        return layout<LayoutT>([](LayoutT&) {});
    }

    // Constructs a StyleT, configures it via fn(StyleT&) (ViewStyle and its
    // subclasses expose most of their drawing state as plain public fields
    // - see viewstyle.h), then setStyle()s it.
    template<typename StyleT, typename Fn>
    ViewBuilder& style(Fn&& fn) {
        static_assert(std::is_base_of_v<ViewStyle, StyleT>,
            "ViewBuilder<ViewT>::style<StyleT>(): StyleT must derive from newui::ViewStyle");
        auto instance = std::make_unique<StyleT>();
        fn(*instance);
        view_->setStyle(std::move(instance));
        return *this;
    }

    template<typename StyleT>
    ViewBuilder& style() {
        return style<StyleT>([](StyleT&) {});
    }

    // Only a SubView is ever addChild()'d onto a parent Layout to arrange,
    // so only a SubView has LayoutParams to set - static_assert rather than
    // silently no-op-ing for a ViewBuilder<RootView>.
    ViewBuilder& layoutParams(std::unique_ptr<LayoutParams> params) {
        static_assert(std::is_base_of_v<SubView, ViewT>,
            "ViewBuilder<ViewT>::layoutParams(): ViewT must derive from newui::SubView");
        static_cast<SubView*>(view_)->setLayoutParams(std::move(params));
        return *this;
    }

    // Constructs a ParamsT, configures it via fn(ParamsT&) (e.g.
    // AnchorLayoutParams::anchors/leftMargin/..., FlexLayoutParams::weight -
    // both plain public fields, see layout.h), then setLayoutParams()s it.
    template<typename ParamsT, typename Fn>
    ViewBuilder& layoutParams(Fn&& fn) {
        static_assert(std::is_base_of_v<SubView, ViewT>,
            "ViewBuilder<ViewT>::layoutParams<ParamsT>(): ViewT must derive from newui::SubView");
        static_assert(std::is_base_of_v<LayoutParams, ParamsT>,
            "ViewBuilder<ViewT>::layoutParams<ParamsT>(): ParamsT must derive from newui::LayoutParams");
        auto instance = std::make_unique<ParamsT>();
        fn(*instance);
        static_cast<SubView*>(view_)->setLayoutParams(std::move(instance));
        return *this;
    }

    template<typename ParamsT>
    ViewBuilder& layoutParams() {
        return layoutParams<ParamsT>([](ParamsT&) {});
    }

    // Escape hatch for anything not covered by the fluent setters above
    // (a control-specific setter, e.g. Button::setText()) - runs fn against
    // the concrete ViewT& directly.
    template<typename Fn>
    ViewBuilder& configure(Fn&& fn) { fn(*view_); return *this; }

    // Builds a ChildT, configures it via fn(ViewBuilder<ChildT>&), then
    // addChild()s the result onto this View - chaining continues on the
    // parent builder, not the child's.
    template<typename ChildT, typename Fn>
    ViewBuilder& child(Fn&& fn) {
        static_assert(std::is_base_of_v<SubView, ChildT>,
            "ViewBuilder<ViewT>::child<ChildT>(): ChildT must derive from newui::SubView (only a SubView can be addChild()'d)");
        ViewBuilder<ChildT> childBuilder;
        fn(childBuilder);
        view_->addChild(childBuilder.build());
        return *this;
    }

    // Same as above with no configuration - just build+attach a plain
    // default-constructed ChildT.
    template<typename ChildT>
    ViewBuilder& child() {
        return child<ChildT>([](ViewBuilder<ChildT>&) {});
    }

    // Attach an already-built subtree (e.g. one produced by a separately-
    // kept ViewBuilder<ChildT>().build() call) rather than building and
    // configuring it inline.
    ViewBuilder& child(SubView* built) {
        view_->addChild(built);
        return *this;
    }

    // Hands back the built ViewT* - still unattached to any parent unless
    // this builder was itself reached via child<ViewT>() above. Ownership
    // follows the same raw-pointer convention as the rest of newui: from
    // here, whatever addChild()s it (or, at the root, whatever explicitly
    // delete()s it) owns it.
    ViewT* build() { return view_; }

    // Implicit conversion, for a call site that just wants the pointer
    // without a separate build() call (e.g. rootView->addChild(ViewBuilder<SubView>()...)).
    operator ViewT*() { return view_; }

private:
    ViewT* view_;
};

}
