#pragma once

#include <newui/delegate.h>
#include <newui/subview.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace newui {

// A single-select row of labeled, mutually-exclusive segments joined by
// one shared rounded-rect outline (e.g. a "Design | Source | Data Flow"
// mode switch) - the real, generic control
// bluesky/designer-surface/Main.dc.html's own ".segmented" mockup (in
// cpp_codetools) called for, with no existing newui equivalent to reuse
// (unlike its own toolbar's ToolbarButton/ToolbarSeparator, already
// real).
//
// A plain SubView, not a Control, for the same reason Splitter is: Control's
// own click tracking assumes one single rectangular hit region for the
// whole control, but this needs N independent per-segment regions - see
// Splitter's own class comment for the fuller reasoning (Control's base
// constructor unconditionally subscribes onMouseDown/onMouseUp click
// tracking that would report every click Handled regardless of which
// segment, or none, it actually landed on).
//
// Segments size to their own natural text width (see naturalSize()) -
// left-aligned within whatever bounds this control is actually given, not
// stretched to fill it (matches ToolbarButton's own "given a size, render
// within it" convention - it never self-measures either). A caller sizes
// this control via naturalSize() the same way Toolbar/ToolbarSeparator
// size themselves via a fixed setDesiredSize() call in their own
// constructors, just computed from real content here instead of a fixed
// constant.
// @reflect category=menutoolbar
class SegmentedControl : public SubView {
public:
    SegmentedControl();
    ~SegmentedControl() override {}

    // Replaces the whole segment list - every segment starts enabled,
    // and selectedIndex() resets to 0. An empty list just leaves this
    // control with nothing to paint/hit-test (never a crash).
    void setSegments(std::vector<std::string> labels);
    const std::vector<std::string>& segments() const { return labels_; }

    std::size_t selectedIndex() const { return selectedIndex_; }

    // A no-op if index is out of range, already selected, or
    // !isSegmentEnabled(index) - the same gate a real click funnels
    // through (handleMouseDown), so a disabled segment can never become
    // selected either way, by click or direct call. Fires
    // onSelectionChanged only on an actual change.
    void setSelectedIndex(std::size_t index);

    bool isSegmentEnabled(std::size_t index) const;

    // A disabled segment still paints (grayed - matching a "not built
    // yet" mode like cpp_codetools's own Source/Data Flow segments) but
    // never responds to a click and can never become selectedIndex() -
    // see setSelectedIndex()'s own comment.
    void setSegmentEnabled(std::size_t index, bool enabled);

    typedef Delegate<SegmentedControl> SelectionChangedDelegate;
    SelectionChangedDelegate onSelectionChanged;

    // Sum of every segment's own natural (text + padding) width - the
    // size a caller should assign via setDesiredSize()/
    // ViewBuilder::desiredSize() for this control to render at its
    // intended size (this class never self-assigns its own bounds - see
    // this class's own header comment). Height is a fixed, reasonable
    // constant (this control has no per-segment vertical content to
    // measure), not derived from anything.
    Size naturalSize() const;

    void paint(BLContext& ctx) override;

private:
    // segmentAt()'s own left edge and width, needed by paint() too so
    // they can never disagree - same "one shared helper" shape Splitter's
    // own firstPaneSize() already establishes.
    float segmentLeft(std::size_t index) const;
    float segmentWidth(std::size_t index) const;

    // Segment index at localPt, or std::nullopt if localPt falls outside
    // every segment (e.g. past the last one, in extra space left over if
    // this control's real bounds exceed naturalSize()'s own width).
    std::optional<std::size_t> segmentAt(const Point& localPt) const;

    SyncReturn handleMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);

    std::vector<std::string> labels_;
    std::vector<bool> enabled_;
    std::size_t selectedIndex_ = 0;
};

}
