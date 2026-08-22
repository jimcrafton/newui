#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <blend2d/blend2d.h>

#include "newui/newui.h"
#include "newui/geometry.h"
#include "newui/viewstyle.h"

namespace newui {
    class ItemController;

    // A lightweight, non-View "cell renderer" - owned and recycled by an
    // ItemController (controllers.h), borrowed by whatever SubView is
    // walking a Model's visible range (a future ListView/TreeView/
    // TableView), never by the SubView itself - see items-plan.md and
    // ItemController's own class comment for the full ownership story.
    //
    // Deliberately holds no model data of its own: an Item's job is to
    // paint whatever the Model currently says for a given index, fetched
    // fresh every call via ItemController::model() - never cached here,
    // since the same Item instance is expected to be reused across many
    // different indices over its lifetime (that's the whole point of the
    // pool in ItemController).
    //
    // Does own a ViewStyle though, the same way View does (view.h) - even
    // without being a View itself, ViewStyle::paint()/computeClientBounds()
    // only ever need a Size/highlighted flag, not a live View, so the same
    // background/border/theme-chrome machinery View::paintStyle()/
    // getClientBounds() already provide works here unchanged. Swap in a
    // ThemedListItemStyle/ThemedTreeItemStyle (viewstyle.h) via setStyle()
    // for real themed row chrome (selection, hot/pressed state, ...); the
    // default plain ViewStyle just gives a background fill/border.
    //
    // No shared virtual paint(index, ...) here on purpose: ListItem/
    // TreeItem/TableItem below each take the index shape natural to their
    // own Controller (a plain std::size_t for a flat list, a row/col pair
    // for a table, a root-to-node path for a tree) rather than one
    // contrived common signature - nothing in this codebase ever needs to
    // treat a ListItem and a TableItem interchangeably through a single
    // pointer, since a ListView only ever holds ListItems (paired with its
    // own ListController), same for TreeView/TreeItem and TableView/
    // TableItem. paint(ctx, rect) below - no index - is the one part that
    // *is* shared: basic chrome, common to every kind of Item.
    class Item {
    public:
        Item();
        virtual ~Item() = default;

        ViewStyle& style() { return *style_; }
        const ViewStyle& style() const { return *style_; }

        // Swaps in a different ViewStyle (e.g. std::make_unique<ThemedListItemStyle>())
        // - see View::setStyle()'s own doc comment (view.h), same idea.
        void setStyle(std::unique_ptr<ViewStyle> style) { style_ = std::move(style); }

        void setHighlighted(bool highlighted) { highlighted_ = highlighted; }
        bool isHighlighted() const { return highlighted_; }

        // Draws this Item's background/border chrome from style() into
        // rect (translating ctx to rect's own top-left first, since
        // ViewStyle::paint() itself only ever draws at local (0,0) - see
        // View::paintStyle(), the same pattern). Call this first thing
        // from a derived paint() override (ListItem::paint() etc.), then
        // draw whatever content is specific to that Item (text, glyphs,
        // ...) within clientBounds() afterward, same "chrome, then
        // content" split View::paintStyle()/paint() already have.
        virtual void paint(BLContext& ctx, const Rect& rect);

        // The rect (in the same coordinate space paint()'s own rect
        // argument was in) left over for content after style()'s chrome -
        // only valid after paint() has run at least once for the current
        // rect, same "computed live from the last paint" contract as
        // View::getClientBounds() other than needing paint() to have run
        // first (View::getClientBounds() can be computed style()-only,
        // paint-free - this can't, since it needs rect's own position,
        // which nothing else here stores independently).
        const Rect& clientBounds() const { return clientBounds_; }

    private:
        std::unique_ptr<ViewStyle> style_;
        bool highlighted_ = false;
        Rect clientBounds_;
    };

    // Item for a flat, 0-based-indexed list (ListController's own kind of
    // Model access) - paint() calls Item::paint() for chrome, then fetches
    // controller.model()->value(index) (index boxes into the std::any key
    // Model::value() already takes, models.h) and draws it as left-
    // aligned, vertically centered text within clientBounds(), same
    // BLFont/glyph-buffer/fill_utf8_text drawing this toolkit's own
    // LabelStyle::paint() uses (viewstyle.h).
    class ListItem : public Item {
    public:
        // Explicit, even though trivial - reflectgen's constructor walk
        // only sees a class's constructors when at least one is written
        // out, not purely-implicit ones (the same odr-use limitation
        // documented in tools/reflectgen/reflectgen.py for implicit copy
        // constructors) - without this, ItemController::instantiateItem()
        // couldn't construct a ListItem by name via reflection at all.
        ListItem() = default;

        virtual void paint(BLContext& ctx, const Rect& rect, std::size_t index, ItemController& controller);
    };

    // Item for a tree, addressed by path: the sequence of child indices
    // from the root down to this node (an empty path is the root itself).
    // Same paint() shape as ListItem otherwise - see its own comment.
    class TreeItem : public Item {
    public:
        // See ListItem::ListItem()'s own comment - same reflectgen reason.
        TreeItem() = default;

        virtual void paint(BLContext& ctx, const Rect& rect, const std::vector<std::size_t>& path, ItemController& controller);
    };

    // Item for a table, addressed by (row, col). Same paint() shape as
    // ListItem otherwise - see its own comment.
    class TableItem : public Item {
    public:
        // See ListItem::ListItem()'s own comment - same reflectgen reason.
        TableItem() = default;

        virtual void paint(BLContext& ctx, const Rect& rect, std::size_t row, std::size_t col, ItemController& controller);
    };
}
