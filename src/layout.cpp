#include "newui/layout.h"

#include <vector>

#include "newui/subview.h"
#include "newui/view.h"

namespace newui {

    void AnchorLayout::arrange(View& container) {
        const Size containerSize = container.getBounds().size();

        for (SubView* child : container.childViews()) {
            if (!child->isVisible()) {
                continue;
            }

            auto* params = dynamic_cast<AnchorLayoutParams*>(child->layoutParams());
            if (params == nullptr) {
                continue;  // unconfigured child - left exactly where it was
            }

            const Rect current = child->getBounds();
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

            child->setBounds(Rect(x, y, width, height));
        }
    }

    void StackLayout::arrange(View& container) {
        const Size containerSize = container.getBounds().size();
        const bool horizontal = (orientation_ == Orientation::Horizontal);

        const float mainSize = (horizontal ? containerSize.width : containerSize.height) - padding_ * 2.0f;
        const float crossSize = (horizontal ? containerSize.height : containerSize.width) - padding_ * 2.0f;

        struct Entry {
            SubView* child;
            StackLayoutParams* params;
            float mainSize;   // natural size, before weighted leftover is added
            float crossSize;  // this child's own cross-axis size, before Stretch
        };

        std::vector<Entry> entries;
        entries.reserve(container.childViews().size());

        float totalWeight = 0.0f;
        float totalNaturalMain = 0.0f;

        for (SubView* child : container.childViews()) {
            if (!child->isVisible()) {
                continue;
            }

            auto* params = dynamic_cast<StackLayoutParams*>(child->layoutParams());
            const Size childSize = child->getBounds().size();
            const bool weighted = (params != nullptr && params->weight > 0.0f);

            // A weighted child's main-axis size is entirely computed by
            // this layout (natural + its leftover share below) - so
            // reading "natural" back from its own bounds would feed
            // this pass's output into the next pass's input, and that
            // compounds across repeated arrange() calls (most visibly
            // on a shrink: the stale, already-inflated "natural" size
            // makes leftover clamp to 0 forever, so the child never
            // shrinks back down). Treat a weighted child's natural
            // main-axis size as 0 instead - CSS flexbox's
            // flex-basis: 0, the same convention flex-grow shorthand
            // uses - so its resolved size is purely its leftover share
            // each time, unaffected by whatever arrange() computed last
            // time. Only affects the main axis; cross-axis size below
            // still reads current bounds, which is safe since nothing
            // ever adds to it (Stretch overwrites it outright, and
            // Start/Center/End never touch a child's stored size at
            // all - see the alignment switch further down).
            const float natural = weighted ? 0.0f : (horizontal ? childSize.width : childSize.height);
            const float cross = horizontal ? childSize.height : childSize.width;

            entries.push_back(Entry{child, params, natural, cross});
            totalWeight += (params != nullptr) ? params->weight : 0.0f;
            totalNaturalMain += natural;
        }

        if (entries.empty()) {
            return;
        }

        const float totalSpacing = spacing_ * static_cast<float>(entries.size() - 1);
        float leftover = mainSize - totalNaturalMain - totalSpacing;
        if (leftover < 0.0f) {
            leftover = 0.0f;  // over-full: children keep natural size, spill past the container
        }

        for (Entry& entry : entries) {
            if (entry.params != nullptr && entry.params->weight > 0.0f && totalWeight > 0.0f) {
                entry.mainSize += leftover * (entry.params->weight / totalWeight);
            }
        }

        // mainAxisAlignment only applies when leftover space wasn't
        // already claimed by a weighted child - a weighted child having
        // absorbed it is exactly what totalWeight > 0.0f means here.
        float startOffset = padding_;
        float extraSpacing = 0.0f;
        if (totalWeight == 0.0f && leftover > 0.0f) {
            const std::size_t count = entries.size();
            switch (mainAxisAlignment_) {
                case MainAxisAlignment::Start:
                    break;
                case MainAxisAlignment::Center:
                    startOffset += leftover * 0.5f;
                    break;
                case MainAxisAlignment::End:
                    startOffset += leftover;
                    break;
                case MainAxisAlignment::SpaceBetween:
                    extraSpacing = (count > 1) ? leftover / static_cast<float>(count - 1) : 0.0f;
                    break;
                case MainAxisAlignment::SpaceAround:
                    extraSpacing = leftover / static_cast<float>(count);
                    startOffset += extraSpacing * 0.5f;
                    break;
                case MainAxisAlignment::SpaceEvenly:
                    extraSpacing = leftover / static_cast<float>(count + 1);
                    startOffset += extraSpacing;
                    break;
            }
        }

        float mainPos = startOffset;
        for (Entry& entry : entries) {
            CrossAxisAlignment crossAlign = crossAxisAlignment_;
            if (entry.params != nullptr && entry.params->crossAxisAlignment.has_value()) {
                crossAlign = *entry.params->crossAxisAlignment;
            }

            float crossPos = padding_;
            float finalCross = entry.crossSize;
            switch (crossAlign) {
                case CrossAxisAlignment::Start:
                    break;
                case CrossAxisAlignment::Center:
                    crossPos += (crossSize - finalCross) * 0.5f;
                    break;
                case CrossAxisAlignment::End:
                    crossPos += crossSize - finalCross;
                    break;
                case CrossAxisAlignment::Stretch:
                    finalCross = crossSize;
                    break;
            }

            const Rect bounds = horizontal
                ? Rect(mainPos, crossPos, entry.mainSize, finalCross)
                : Rect(crossPos, mainPos, finalCross, entry.mainSize);
            entry.child->setBounds(bounds);

            mainPos += entry.mainSize + spacing_ + extraSpacing;
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

        const Rect bounds(0.0f, 0.0f, container.getBounds().size().width, container.getBounds().size().height);

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
            if (children[i]->getName() == name) {
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

}
