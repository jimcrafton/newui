#include "newui/layout.h"

#include <utility>
#include <vector>

#include "newui/subview.h"
#include "newui/view.h"

namespace {

    // Returns how much to offset the first item/line (startOffset) and how
    // much extra gap to add between each pair of items/lines (extraGap) to
    // distribute `leftover` space across `count` things via alignment -
    // shared by FlexLayout::arrangeFlexLine()'s main-axis item
    // distribution (called once for a non-wrapped FlexLayout, once per
    // line for a wrapped one) and FlexLayout::arrange()'s cross-axis line
    // distribution (alignContent(), only when wrap() is on) - both are "N
    // things with uniform gaps, some leftover space to place" with
    // identical semantics, just applied to different things.
    struct LeftoverDistribution {
        float startOffset = 0.0f;
        float extraGap = 0.0f;
    };

    LeftoverDistribution distributeLeftover(newui::MainAxisAlignment alignment, float leftover, std::size_t count) {
        LeftoverDistribution d;
        if (leftover <= 0.0f || count == 0) {
            return d;
        }
        switch (alignment) {
            case newui::MainAxisAlignment::Start:
                break;
            case newui::MainAxisAlignment::Center:
                d.startOffset = leftover * 0.5f;
                break;
            case newui::MainAxisAlignment::End:
                d.startOffset = leftover;
                break;
            case newui::MainAxisAlignment::SpaceBetween:
                d.extraGap = (count > 1) ? leftover / static_cast<float>(count - 1) : 0.0f;
                break;
            case newui::MainAxisAlignment::SpaceAround:
                d.extraGap = leftover / static_cast<float>(count);
                d.startOffset = d.extraGap * 0.5f;
                break;
            case newui::MainAxisAlignment::SpaceEvenly:
                d.extraGap = leftover / static_cast<float>(count + 1);
                d.startOffset = d.extraGap;
                break;
        }
        return d;
    }

    // One FlexLayout child, gathered once by FlexLayout::arrange() before
    // either the non-wrap or wrap path runs.
    struct FlexEntry {
        newui::SubView* child;
        newui::FlexLayoutParams* params;
        float mainSize;   // natural size (see FlexLayout::arrange()'s comment), before weighted leftover is added
        float crossSize;  // this child's own cross-axis size, before Stretch
    };

    // Places entries[first,last) - one "line" - along the main axis within
    // (crossStart, crossThickness) on the cross axis. Shared by
    // FlexLayout::arrange()'s non-wrap path (one call, the whole container
    // is "one line") and its wrap path (one call per line, crossThickness
    // is that line's own thickness rather than the whole container's cross
    // size) - identical math either way, just parameterized by which
    // slice of entries and which cross-axis band it's placing into.
    void arrangeFlexLine(const std::vector<FlexEntry>& entries, std::size_t first, std::size_t last,
                          const newui::Rect& clientBounds, bool horizontal, float mainSize, float spacing,
                          float mainStartOffset, newui::MainAxisAlignment mainAxisAlignment,
                          float crossStart, float crossThickness, newui::CrossAxisAlignment crossAxisAlignment) {
        const std::size_t count = last - first;
        if (count == 0) {
            return;
        }

        float totalWeight = 0.0f;
        float totalNaturalMain = 0.0f;
        for (std::size_t i = first; i < last; ++i) {
            totalWeight += (entries[i].params != nullptr) ? entries[i].params->weight : 0.0f;
            totalNaturalMain += entries[i].mainSize;
        }

        const float totalSpacing = spacing * static_cast<float>(count - 1);
        float leftover = mainSize - totalNaturalMain - totalSpacing;
        if (leftover < 0.0f) {
            leftover = 0.0f;  // over-full: children keep natural size, spill past the container
        }

        std::vector<float> resolvedMain(count);
        for (std::size_t i = 0; i < count; ++i) {
            const FlexEntry& entry = entries[first + i];
            resolvedMain[i] = entry.mainSize;
            if (entry.params != nullptr && entry.params->weight > 0.0f && totalWeight > 0.0f) {
                resolvedMain[i] += leftover * (entry.params->weight / totalWeight);
            }
        }

        // mainAxisAlignment only applies when leftover space wasn't
        // already claimed by a weighted child - a weighted child having
        // absorbed it is exactly what totalWeight > 0.0f means here.
        float startOffset = mainStartOffset;
        float extraSpacing = 0.0f;
        if (totalWeight == 0.0f) {
            LeftoverDistribution d = distributeLeftover(mainAxisAlignment, leftover, count);
            startOffset += d.startOffset;
            extraSpacing = d.extraGap;
        }

        float mainPos = startOffset;
        for (std::size_t i = 0; i < count; ++i) {
            const FlexEntry& entry = entries[first + i];
            newui::CrossAxisAlignment crossAlign = crossAxisAlignment;
            if (entry.params != nullptr && entry.params->crossAxisAlignment.has_value()) {
                crossAlign = *entry.params->crossAxisAlignment;
            }

            float crossPos = crossStart;
            float finalCross = entry.crossSize;
            switch (crossAlign) {
                case newui::CrossAxisAlignment::Start:
                    break;
                case newui::CrossAxisAlignment::Center:
                    crossPos += (crossThickness - finalCross) * 0.5f;
                    break;
                case newui::CrossAxisAlignment::End:
                    crossPos += crossThickness - finalCross;
                    break;
                case newui::CrossAxisAlignment::Stretch:
                    finalCross = crossThickness;
                    break;
            }

            const newui::Rect bounds = horizontal
                ? newui::Rect(clientBounds.left() + mainPos, clientBounds.top() + crossPos, resolvedMain[i], finalCross)
                : newui::Rect(clientBounds.left() + crossPos, clientBounds.top() + mainPos, finalCross, resolvedMain[i]);
            entry.child->setBounds(bounds);

            mainPos += resolvedMain[i] + spacing + extraSpacing;
        }
    }

}

namespace newui {

    void AnchorLayout::arrange(View& container) {
        const Rect clientBounds = container.getClientBounds();
        const Size containerSize = clientBounds.size();

        for (SubView* child : container.childViews()) {
            if (!child->isVisible()) {
                continue;
            }

            auto* params = dynamic_cast<AnchorLayoutParams*>(child->layoutParams());
            if (params == nullptr) {
                continue;  // unconfigured child - left exactly where it was
            }

            const Rect current = child->bounds();
            float x = current.left();
            float y = current.top();
            float width = current.size().width;
            float height = current.size().height;

            const bool left = hasAnchor(params->anchors, Anchor::Left);
            const bool right = hasAnchor(params->anchors, Anchor::Right);
            const bool top = hasAnchor(params->anchors, Anchor::Top);
            const bool bottom = hasAnchor(params->anchors, Anchor::Bottom);

            if (left && right) {
                x = params->leftMargin;
                width = containerSize.width - params->leftMargin - params->rightMargin;
            } else if (left) {
                x = params->leftMargin;
                width = params->width;
            } else if (right) {
                width = params->width;
                x = containerSize.width - params->rightMargin - width;
            } else if (hasAnchor(params->anchors, Anchor::CenterX)) {
                width = params->width;
                x = (containerSize.width - width) * 0.5f;
            }

            if (top && bottom) {
                y = params->topMargin;
                height = containerSize.height - params->topMargin - params->bottomMargin;
            } else if (top) {
                y = params->topMargin;
                height = params->height;
            } else if (bottom) {
                height = params->height;
                y = containerSize.height - params->bottomMargin - height;
            } else if (hasAnchor(params->anchors, Anchor::CenterY)) {
                height = params->height;
                y = (containerSize.height - height) * 0.5f;
            }

            width = width < 0.0f ? 0.0f : width;
            height = height < 0.0f ? 0.0f : height;

            child->setBounds(Rect(clientBounds.left() + x, clientBounds.top() + y, width, height));
        }
    }

    void FlexLayout::arrange(View& container) {
        const Rect clientBounds = container.getClientBounds();
        const Size containerSize = clientBounds.size();
        const bool horizontal = (orientation_ == Orientation::Horizontal);

        const float mainSize = (horizontal ? containerSize.width : containerSize.height) - padding_ * 2.0f;
        const float crossSize = (horizontal ? containerSize.height : containerSize.width) - padding_ * 2.0f;

        std::vector<FlexEntry> entries;
        entries.reserve(container.childViews().size());

        for (SubView* child : container.childViews()) {
            if (!child->isVisible()) {
                continue;
            }

            auto* params = dynamic_cast<FlexLayoutParams*>(child->layoutParams());
            const Size childSize = child->desiredSize();
            const bool weighted = (params != nullptr && params->weight > 0.0f);

            // A weighted child's main-axis size is entirely computed by
            // this layout (natural + its leftover share below) - so
            // reading "natural" back from its own desiredSize() would feed
            // this pass's output into the next pass's input if
            // desiredSize() ever fell back to bounds size (its default -
            // see View::desiredSize()), and that compounds across repeated
            // arrange() calls (most visibly on a shrink: the stale,
            // already-inflated "natural" size makes leftover clamp to 0
            // forever, so the child never shrinks back down). Treat a
            // weighted child's natural main-axis size as 0 instead - CSS
            // flexbox's flex-basis: 0, the same convention flex-grow
            // shorthand uses - so its resolved size is purely its leftover
            // share each time. Only affects the main axis; cross-axis size
            // below still reads desiredSize(), which is safe since nothing
            // ever adds to it (Stretch overwrites it outright, and
            // Start/Center/End never touch it at all - see
            // arrangeFlexLine()'s alignment switch).
            const float natural = weighted ? 0.0f : (horizontal ? childSize.width : childSize.height);
            const float cross = horizontal ? childSize.height : childSize.width;

            entries.push_back(FlexEntry{child, params, natural, cross});
        }

        if (entries.empty()) {
            return;
        }

        if (!wrap_) {
            // The whole container is "one line" - identical math to a
            // wrapped line, just with the container's full cross size
            // instead of one line's own thickness.
            arrangeFlexLine(entries, 0, entries.size(), clientBounds, horizontal, mainSize, spacing_,
                             padding_, mainAxisAlignment_, padding_, crossSize, crossAxisAlignment_);
            return;
        }

        // Partition entries into lines: greedily accumulate while the next
        // child still fits (natural sizes + spacing so far), closing the
        // current line and starting a new one otherwise - unless the line
        // is still empty (a single child wider than mainSize still gets
        // its own line, never split).
        std::vector<std::pair<std::size_t, std::size_t>> lines;  // [first, last)
        {
            std::size_t lineStart = 0;
            float accumulated = 0.0f;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const float itemSize = entries[i].mainSize;
                const float withItem = (i == lineStart) ? itemSize : accumulated + spacing_ + itemSize;
                if (i > lineStart && withItem > mainSize) {
                    lines.emplace_back(lineStart, i);
                    lineStart = i;
                    accumulated = itemSize;
                } else {
                    accumulated = withItem;
                }
            }
            lines.emplace_back(lineStart, entries.size());
        }

        // Each line's cross thickness = the max of its entries' own
        // (natural) cross sizes.
        std::vector<float> lineThickness(lines.size());
        for (std::size_t li = 0; li < lines.size(); ++li) {
            float thickness = 0.0f;
            for (std::size_t i = lines[li].first; i < lines[li].second; ++i) {
                if (entries[i].crossSize > thickness) {
                    thickness = entries[i].crossSize;
                }
            }
            lineThickness[li] = thickness;
        }

        float totalLinesThickness = lineSpacing_ * static_cast<float>(lines.size() - 1);
        for (float t : lineThickness) {
            totalLinesThickness += t;
        }

        float crossLeftover = crossSize - totalLinesThickness;
        if (crossLeftover < 0.0f) {
            crossLeftover = 0.0f;  // over-full: lines keep natural thickness, spill past the container
        }

        // alignContent() distributes leftover cross-axis space across the
        // set of lines - exactly the same "N things, uniform gaps,
        // leftover space" problem mainAxisAlignment() solves for items
        // within a line, just applied to lines instead.
        const LeftoverDistribution crossDist = distributeLeftover(alignContent_, crossLeftover, lines.size());

        float crossPos = padding_ + crossDist.startOffset;
        for (std::size_t li = 0; li < lines.size(); ++li) {
            arrangeFlexLine(entries, lines[li].first, lines[li].second, clientBounds, horizontal, mainSize,
                             spacing_, padding_, mainAxisAlignment_, crossPos, lineThickness[li], crossAxisAlignment_);
            crossPos += lineThickness[li] + lineSpacing_ + crossDist.extraGap;
        }
    }

    void CardLayout::arrange(View& container) {
        container_ = &container;

        const auto& children = container.childViews();
        if (children.empty()) {
            return;
        }

        if (activeIndex_ >= children.size()) {
            activeIndex_ = children.size() - 1;
        }

        const Rect bounds = container.getClientBounds();

        for (std::size_t i = 0; i < children.size(); ++i) {
            SubView* child = children[i];
            const bool active = (i == activeIndex_);
            child->setVisible(active);
            if (active) {
                child->setBounds(bounds);
            }
        }
    }

    void CardLayout::show(std::size_t index) {
        activeIndex_ = index;
        if (container_ != nullptr) {
            arrange(*container_);
        }
    }

    void CardLayout::show(const std::string& name) {
        if (container_ == nullptr) {
            return;
        }

        const auto& children = container_->childViews();
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i]->name() == name) {
                show(i);
                return;
            }
        }
    }

    void CardLayout::next() {
        if (container_ == nullptr) {
            return;
        }

        const auto& children = container_->childViews();
        if (children.empty()) {
            return;
        }

        show((activeIndex_ + 1) % children.size());
    }

    void CardLayout::previous() {
        if (container_ == nullptr) {
            return;
        }

        const auto& children = container_->childViews();
        if (children.empty()) {
            return;
        }

        show((activeIndex_ + children.size() - 1) % children.size());
    }

    namespace {
        // Resolves one axis's track pixel sizes: Auto tracks first (sized
        // to the largest desiredSize() among their own non-spanning
        // children - see GridLayout::arrange()'s comment), then Fixed
        // tracks take their literal value, then Star tracks split what's
        // left (after Fixed + resolved Auto sizes and N-1 spacing gaps)
        // proportional to weight - the same leftover-distribution shape
        // FlexLayoutParams::weight already uses.
        std::vector<float> resolveGridTrackSizes(const std::vector<GridTrack>& tracks,
                                                    const std::vector<float>& autoSizes,
                                                    float availableSize, float spacing) {
            std::vector<float> sizes(tracks.size(), 0.0f);
            if (tracks.empty()) {
                return sizes;
            }

            float fixedAndAutoTotal = 0.0f;
            float starWeightTotal = 0.0f;
            for (std::size_t i = 0; i < tracks.size(); ++i) {
                switch (tracks[i].kind) {
                    case GridTrackKind::Fixed:
                        sizes[i] = tracks[i].value;
                        fixedAndAutoTotal += sizes[i];
                        break;
                    case GridTrackKind::Auto:
                        sizes[i] = autoSizes[i];
                        fixedAndAutoTotal += sizes[i];
                        break;
                    case GridTrackKind::Star:
                        starWeightTotal += tracks[i].value;
                        break;
                }
            }

            const float totalSpacing = spacing * static_cast<float>(tracks.size() - 1);
            float leftoverForStars = availableSize - totalSpacing - fixedAndAutoTotal;
            if (leftoverForStars < 0.0f) {
                leftoverForStars = 0.0f;
            }

            for (std::size_t i = 0; i < tracks.size(); ++i) {
                if (tracks[i].kind == GridTrackKind::Star) {
                    sizes[i] = (starWeightTotal > 0.0f) ? leftoverForStars * (tracks[i].value / starWeightTotal) : 0.0f;
                }
            }

            return sizes;
        }

        // Prefix-sums track sizes into each track's start offset.
        std::vector<float> gridTrackOffsets(const std::vector<float>& sizes, float spacing) {
            std::vector<float> offsets(sizes.size(), 0.0f);
            float pos = 0.0f;
            for (std::size_t i = 0; i < sizes.size(); ++i) {
                offsets[i] = pos;
                pos += sizes[i] + spacing;
            }
            return offsets;
        }
    }

    void GridLayout::arrange(View& container) {
        if (rows_.empty() || columns_.empty()) {
            return;  // nothing to place into
        }

        const Rect clientBounds = container.getClientBounds();
        const Size containerSize = clientBounds.size();

        // Auto tracks first: walk visible children once per axis, taking
        // the max desiredSize() among non-spanning children landing in
        // each Auto track - spanning children are ignored for auto-sizing
        // (a deliberate v1 simplification, avoids the iterative resolution
        // CSS Grid's full algorithm needs).
        std::vector<float> autoColumnSizes(columns_.size(), 0.0f);
        std::vector<float> autoRowSizes(rows_.size(), 0.0f);
        for (SubView* child : container.childViews()) {
            if (!child->isVisible()) {
                continue;
            }
            auto* params = dynamic_cast<GridLayoutParams*>(child->layoutParams());
            if (params == nullptr) {
                continue;
            }
            const Size desired = child->desiredSize();
            if (params->columnSpan <= 1 && params->column < columns_.size() &&
                    columns_[params->column].kind == GridTrackKind::Auto &&
                    desired.width > autoColumnSizes[params->column]) {
                autoColumnSizes[params->column] = desired.width;
            }
            if (params->rowSpan <= 1 && params->row < rows_.size() &&
                    rows_[params->row].kind == GridTrackKind::Auto &&
                    desired.height > autoRowSizes[params->row]) {
                autoRowSizes[params->row] = desired.height;
            }
        }

        const std::vector<float> columnSizes = resolveGridTrackSizes(columns_, autoColumnSizes, containerSize.width, columnSpacing_);
        const std::vector<float> rowSizes = resolveGridTrackSizes(rows_, autoRowSizes, containerSize.height, rowSpacing_);
        const std::vector<float> columnOffsets = gridTrackOffsets(columnSizes, columnSpacing_);
        const std::vector<float> rowOffsets = gridTrackOffsets(rowSizes, rowSpacing_);

        for (SubView* child : container.childViews()) {
            if (!child->isVisible()) {
                continue;
            }

            auto* params = dynamic_cast<GridLayoutParams*>(child->layoutParams());
            if (params == nullptr) {
                continue;  // unconfigured child - left exactly where it was
            }
            if (params->row >= rows_.size() || params->column >= columns_.size()) {
                continue;  // out of range - left exactly where it was
            }

            const std::size_t rowSpan = (params->rowSpan > 0) ? params->rowSpan : std::size_t(1);
            const std::size_t columnSpan = (params->columnSpan > 0) ? params->columnSpan : std::size_t(1);
            std::size_t rowEnd = params->row + rowSpan;
            if (rowEnd > rows_.size()) {
                rowEnd = rows_.size();
            }
            std::size_t colEnd = params->column + columnSpan;
            if (colEnd > columns_.size()) {
                colEnd = columns_.size();
            }

            const float cellX = columnOffsets[params->column];
            const float cellY = rowOffsets[params->row];
            const float cellWidth = (columnOffsets[colEnd - 1] + columnSizes[colEnd - 1]) - cellX;
            const float cellHeight = (rowOffsets[rowEnd - 1] + rowSizes[rowEnd - 1]) - cellY;

            const Size desired = child->desiredSize();

            float x = cellX;
            float width = cellWidth;
            if (params->horizontalAlignment != CrossAxisAlignment::Stretch) {
                width = desired.width;
                switch (params->horizontalAlignment) {
                    case CrossAxisAlignment::Start: break;
                    case CrossAxisAlignment::Center: x += (cellWidth - width) * 0.5f; break;
                    case CrossAxisAlignment::End: x += cellWidth - width; break;
                    case CrossAxisAlignment::Stretch: break;  // unreachable (guarded above)
                }
            }

            float y = cellY;
            float height = cellHeight;
            if (params->verticalAlignment != CrossAxisAlignment::Stretch) {
                height = desired.height;
                switch (params->verticalAlignment) {
                    case CrossAxisAlignment::Start: break;
                    case CrossAxisAlignment::Center: y += (cellHeight - height) * 0.5f; break;
                    case CrossAxisAlignment::End: y += cellHeight - height; break;
                    case CrossAxisAlignment::Stretch: break;  // unreachable (guarded above)
                }
            }

            child->setBounds(Rect(clientBounds.left() + x, clientBounds.top() + y, width, height));
        }
    }

}
