#include "timedtableview.h"

#include "cellitem.h"

#include <newui/keyboard_constants.h>
#include <newui/rootview.h>
#include <newui/text.h>
#include <newui/utils.h>

#include <chrono>
#include <cmath>

namespace manyrows {

    using namespace newui;

    TimedTableView::TimedTableView() {
        setName("TimedTableView");
        // Tab must reach handleKeyDown() (cell navigation) instead of moving keyboard focus away.
        setWantsTabKey(true);
        onScrollOffsetChanged.add(this, &TimedTableView::handleScroll);
        onKeyDown.add(this, &TimedTableView::handleKeyDownForEdit);
        onKeyPress.add(this, &TimedTableView::handleKeyPressForEdit);
        onMouseDblClick.add(this, &TimedTableView::handleDoubleClick);

        editor_ = new TextField();
        editor_->setName("cellEditor");
        editor_->setWantsTabKey(true);
        editor_->setVisible(false);
        editor_->onKeyDown.add(this, &TimedTableView::handleEditorKeyDown);
        editor_->onReturnPressed.add(this, &TimedTableView::handleEditorReturn);
        editor_->onLostFocus.add(this, &TimedTableView::handleEditorLostFocus);
        addChild(editor_);
    }

    SyncReturn TimedTableView::handleScroll(View& /*sender*/, const Point& offset) {
        scrollX_ = offset.x;
        scrollY_ = offset.y;
        if (editing_) {
            placeEditor();
        }
        return SyncReturn::Ignored;
    }

    std::size_t TimedTableView::firstVisibleRow() const {
        std::size_t rows = controller().rowCount();
        if (rows == 0) {
            return 0;
        }
        std::size_t first = std::size_t(std::floor(double(scrollY_) / double(controller().rowHeight())));
        return first < rows ? first : rows - 1;
    }

    std::size_t TimedTableView::visibleRowCount() const {
        std::size_t rows = controller().rowCount();
        if (rows == 0) {
            return 0;
        }
        float header = headersVisible() ? controller().headerHeight() : 0.0f;
        double body = double(bounds().height()) - double(header);
        std::size_t fit = body > 0.0 ? std::size_t(std::ceil(body / double(controller().rowHeight()))) : 0;
        std::size_t first = firstVisibleRow();
        return first + fit < rows ? fit : rows - first;
    }

    void TimedTableView::resetPaintStats() {
        lastPaintMs_ = 0.0;
        smoothedPaintMs_ = 0.0;
        worstPaintMs_ = 0.0;
    }

    void TimedTableView::paint(BLContext& ctx) {
        auto start = std::chrono::steady_clock::now();
        CellItem::beginPass(model());   // font and colors resolved once for all the cells below
        TableView::paint(ctx);
        CellItem::endPass();
        std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;

        lastPaintMs_ = elapsed.count();
        smoothedPaintMs_ = smoothedPaintMs_ == 0.0 ? lastPaintMs_ : smoothedPaintMs_ * 0.85 + lastPaintMs_ * 0.15;
        worstPaintMs_ = lastPaintMs_ > worstPaintMs_ ? lastPaintMs_ : worstPaintMs_;
        onPainted(*this);
    }

    // ---------------------------------------------------------------------
    // In-place editing
    // ---------------------------------------------------------------------

    void TimedTableView::placeEditor() {
        float gutter = headersVisible() ? controller().rowHeaderWidth() : 0.0f;
        float header = headersVisible() ? controller().headerHeight() : 0.0f;
        // Row y in double: a million rows x 22 px is past what a float holds exactly.
        double x = double(gutter) + double(controller().columnOffset(editColumn_)) - double(scrollX_);
        double y = double(header) + double(editRow_) * double(controller().rowHeight()) - double(scrollY_);
        editor_->setBounds(Rect(float(x), float(y), controller().columnWidth(editColumn_), controller().rowHeight()));
    }

    void TimedTableView::beginEdit(const std::string* initialText) {
        if (editing_ || !hasActiveCell()) {
            return;
        }
        editRow_ = activeRow();
        editColumn_ = activeColumn();

        std::string text;
        if (initialText != nullptr) {
            text = *initialText;
        } else if (TableModel* tableModel = model()) {
            std::any value = tableModel->valueAt(editRow_, editColumn_);
            if (const std::string* s = std::any_cast<std::string>(&value)) {
                text = *s;
            }
        }

        editing_ = true;
        editor_->setText(utf8ToWide(text));
        placeEditor();
        editor_->setVisible(true);
        editor_->caret().setPosition(text::TextPosition(editor_->text().size()));
        if (RootView* root = rootView()) {
            root->setFocusedSubView(editor_);
        }
        style().markDirty();
    }

    void TimedTableView::endEdit(bool refocusTable) {
        editing_ = false;
        editor_->setVisible(false);
        if (refocusTable) {
            if (RootView* root = rootView()) {
                root->setFocusedSubView(this);
            }
        }
        style().markDirty();
    }

    bool TimedTableView::commitEdit() {
        if (!editing_) {
            return true;
        }
        std::string text = wideToUtf8(editor_->text());
        if (commitHandler_ && !commitHandler_(editRow_, editColumn_, text)) {
            return false;
        }
        endEdit(true);
        return true;
    }

    void TimedTableView::cancelEdit() {
        if (editing_) {
            endEdit(true);
        }
    }

    void TimedTableView::commitAndMove(int dRow, int dColumn) {
        std::size_t row = editRow_;
        std::size_t column = editColumn_;
        if (!commitEdit()) {
            return;
        }
        std::size_t rows = controller().rowCount();
        std::size_t columns = controller().columnCount();
        if (dRow > 0 && row + 1 < rows) {
            ++row;
        } else if (dRow < 0 && row > 0) {
            --row;
        }
        if (dColumn > 0 && column + 1 < columns) {
            ++column;
        } else if (dColumn < 0 && column > 0) {
            --column;
        }
        setActiveCell(row, column);
    }

    SyncReturn TimedTableView::handleKeyDownForEdit(View& /*sender*/, std::uint32_t /*keyMask*/, int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t vkey) {
        if (!editing_ && vkey == vkF2 && hasActiveCell()) {
            beginEdit();
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn TimedTableView::handleKeyPressForEdit(View& /*sender*/, std::uint32_t keyMask, int keyCharVal, int /*repeatCount*/, std::uint32_t /*vkey*/) {
        // A printable character with no Ctrl/Alt starts an edit that REPLACES the cell's text.
        if (editing_ || !hasActiveCell() || (keyMask & (kmCtrl | kmAlt)) != 0 || keyCharVal < 32 || keyCharVal == 127) {
            return SyncReturn::Ignored;
        }
        std::string typed = wideToUtf8(std::wstring(1, wchar_t(keyCharVal)));
        beginEdit(&typed);
        return SyncReturn::Handled;
    }

    SyncReturn TimedTableView::handleDoubleClick(View& /*sender*/, const Point& pt, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        float gutter = headersVisible() ? controller().rowHeaderWidth() : 0.0f;
        float header = headersVisible() ? controller().headerHeight() : 0.0f;
        if (!editing_ && hasActiveCell() && pt.x >= gutter && pt.y >= header) {
            beginEdit();
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn TimedTableView::handleEditorKeyDown(View& /*sender*/, std::uint32_t keyMask, int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t vkey) {
        if (vkey == vkEscape) {
            cancelEdit();
            return SyncReturn::Handled;
        }
        if (vkey == vkTab) {
            commitAndMove(0, (keyMask & kmShift) != 0 ? -1 : 1);
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn TimedTableView::handleEditorReturn(TextField& /*sender*/) {
        commitAndMove((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1, 0);
        return SyncReturn::Handled;
    }

    SyncReturn TimedTableView::handleEditorLostFocus(View& /*sender*/) {
        // Clicking elsewhere commits; a rejected value is dropped rather than trapping the user.
        if (editing_) {
            if (!commitEdit()) {
                endEdit(false);
            }
        }
        return SyncReturn::Ignored;
    }

}
