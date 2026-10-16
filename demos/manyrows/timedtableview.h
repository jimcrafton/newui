#pragma once

#include <newui/controls.h>

#include <functional>
#include <string>

namespace manyrows {

    // The manyrows table: a TableView that times its own paint() and edits cells in place.
    //
    // Timing: lastPaintMs() is the wall time of the most recent paint, smoothedPaintMs() an
    // exponential moving average (so the readout is steady while scrolling), worstPaintMs() the slowest
    // paint since resetPaintStats(). onPainted fires after each paint with the stats updated; keep its
    // handler cheap - it runs inside the paint pass.
    //
    // In-place editing: a TextField child is moved over the active cell while editing. F2, a double
    // click, or just typing a character starts it (typing replaces the text with that character); Enter
    // commits and moves down, Tab / Shift+Tab commit and move right / left, Esc cancels, and clicking
    // elsewhere commits. TableView itself never changes data - on commit the view asks the handler set
    // with setCommitHandler(), which validates and applies the edit (SpreadsheetEditor in main.cpp) and
    // returns false to reject it, which keeps the editor open (unless focus already left, then it cancels).
    // @reflect category=basic
    class TimedTableView : public newui::TableView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        TimedTableView();
        virtual ~TimedTableView() {}

        // Not properties - measurements, not settings.
        // @reflect ignore=true
        double lastPaintMs() const { return lastPaintMs_; }
        // @reflect ignore=true
        double smoothedPaintMs() const { return smoothedPaintMs_; }
        // @reflect ignore=true
        double worstPaintMs() const { return worstPaintMs_; }
        void resetPaintStats();

        // The rows currently on screen (first row and how many, a partly visible last row counts),
        // derived from the scroll offset the ScrollView reports. visibleRowCount() is 0 for an empty table.
        // @reflect ignore=true
        std::size_t firstVisibleRow() const;
        // @reflect ignore=true
        std::size_t visibleRowCount() const;

        typedef newui::Delegate<TimedTableView> PaintedDelegate;
        PaintedDelegate onPainted;

        // --- editing ---
        typedef std::function<bool(std::size_t row, std::size_t column, const std::string& text)> CommitHandler;
        // @reflect ignore=true
        void setCommitHandler(CommitHandler handler) { commitHandler_ = std::move(handler); }

        // @reflect ignore=true
        bool isEditing() const { return editing_; }
        // Starts editing the active cell, showing its current text - or `initialText` when given.
        // Does nothing without an active cell or while already editing.
        void beginEdit(const std::string* initialText = nullptr);
        // Ends editing with the typed text: false (editor stays open) when the commit handler rejects it.
        bool commitEdit();
        void cancelEdit();

        // The editor TextField (a child of this view), for tests.
        // @reflect ignore=true
        newui::TextField& editor() { return *editor_; }

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleScroll(newui::View& sender, const newui::Point& offset);
        newui::SyncReturn handleKeyDownForEdit(newui::View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkey);
        newui::SyncReturn handleKeyPressForEdit(newui::View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkey);
        newui::SyncReturn handleDoubleClick(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleEditorKeyDown(newui::View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkey);
        newui::SyncReturn handleEditorReturn(newui::TextField& sender);
        newui::SyncReturn handleEditorLostFocus(newui::View& sender);

        void placeEditor();
        void endEdit(bool refocusTable);
        // Commits, and on success moves the active cell by (dRow, dColumn) clamped to the table.
        void commitAndMove(int dRow, int dColumn);

        float scrollX_ = 0.0f;
        float scrollY_ = 0.0f;
        double lastPaintMs_ = 0.0;
        double smoothedPaintMs_ = 0.0;
        double worstPaintMs_ = 0.0;

        newui::TextField* editor_ = nullptr;   // owned by this view as a child
        bool editing_ = false;
        std::size_t editRow_ = 0;
        std::size_t editColumn_ = 0;
        CommitHandler commitHandler_;
    };

}
