#pragma once

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/frame.h>
#include <newui/geometry.h>
#include <newui/rootview.h>

#include <string>
#include <vector>

namespace newui {

    // Shared outcome for both Dialog::close()/showModal() and
    // Dialog::ShowMessageBox() below - a custom Dialog and a native
    // message box both ultimately report "which button did the user
    // pick", so one enum covers both instead of two near-identical ones.
    enum class DialogResult {
        None,  // never closed yet - Dialog::result() before close()/showModal() finishes
        Ok,
        Cancel,
        Yes,
        No,
        Abort,
        Retry,
        Ignore,
    };

    // ------------------------------------------------------------------
    // Simple message-box dialogs - a native, always-modal ::MessageBoxA()
    // with a title, body text, a button set and an icon.
    // ------------------------------------------------------------------

    enum class MessageBoxButtons {
        Ok,
        OkCancel,
        YesNo,
        YesNoCancel,
        RetryCancel,
        AbortRetryIgnore,
    };

    enum class MessageBoxIcon {
        None,
        Information,
        Warning,
        Error,
        Question,
    };

    // ------------------------------------------------------------------
    // Common Item Dialog (IFileDialog) options - native Open/Save/browse-
    // for-folder pickers (Vista and later), shown via Dialog::ShowOpenFile/
    // ShowOpenFileMulti/ShowSaveFile/ShowBrowseForFolder below.
    // ------------------------------------------------------------------

    struct FileDialogFilter {
        std::string name;     // e.g. "Text Files"
        std::string pattern;  // e.g. "*.txt;*.log"
    };

    struct FileDialogOptions {
        std::string title;
        std::string defaultFileName;
        std::string defaultExtension;  // no leading dot, e.g. "txt" - only
                                        // meaningful to ShowSaveFile,
                                        // appended when the user doesn't
                                        // type one of their own
        std::string initialDirectory;  // absolute filesystem path, optional
        std::vector<FileDialogFilter> filters;  // empty = no filtering (all files) -
                                                 // ignored entirely by ShowBrowseForFolder
    };

    // ------------------------------------------------------------------
    // Dialog - a top-level window hosting real newui content (via
    // getView(), same as Frame), shown either non-modally ("floating" -
    // show() returns immediately, the caller's own RunLoop keeps pumping
    // it) or modally (showModal() blocks the caller and disables owner
    // until closed).
    //
    // Composes a Frame rather than subclassing it - Frame's window
    // creation/message handling (WndProc, handleMessage) isn't virtual,
    // so there's no seam to add modal-loop/owner-disable behavior via
    // inheritance; Dialog builds that on top of Frame's already-public
    // onClosed delegate and setTitle()/setBounds()/getView() instead.
    //
    // Single-show: once closed, its native window is torn down for good
    // (Frame itself has no way to recreate a destroyed window) - build a
    // new Dialog to show another one. Add content via getView() any time
    // before or after the first show()/showModal() call (both lazily
    // create the native window the first time they're called), same as
    // Frame's own getView()/setTitle()/setBounds() work before
    // initialize().
    // ------------------------------------------------------------------

    class Dialog {
    public:
        Dialog();
        ~Dialog();

        Dialog(const Dialog&) = delete;
        Dialog& operator=(const Dialog&) = delete;

        // ------------------------------------------------------------------
        // Simple message-box dialog - static, not tied to any particular
        // Dialog instance. Blocks until the user picks a button - native
        // modal loop, same as any Win32 MessageBox (owner, if given, is
        // disabled for the duration and the box is centered over it).
        // Maps the pressed button back from Win32's IDOK/IDCANCEL/... to
        // DialogResult; DialogResult::None if the box couldn't be shown
        // at all.
        // ------------------------------------------------------------------

        static DialogResult ShowMessageBox(HWND owner, const std::string& text, const std::string& title,
                                            MessageBoxButtons buttons = MessageBoxButtons::Ok,
                                            MessageBoxIcon icon = MessageBoxIcon::None);

        // ------------------------------------------------------------------
        // Native Common Item Dialog (IFileDialog) pickers - static, not
        // tied to any particular Dialog instance. Each call is self-
        // contained: COM is initialized (STA) for the duration if it isn't
        // already on this thread - newui::RunLoop::run() already does that
        // once at startup (see OleInitialize() in runloop.cpp), so on the
        // normal UI thread this is a no-op - and released again before
        // returning. Each returns false if the user cancelled, or the
        // dialog couldn't be shown at all (e.g. COM failure) - outPath/
        // outPaths are left untouched in that case. owner may be nullptr
        // (no owner window).
        // ------------------------------------------------------------------

        static bool ShowOpenFile(HWND owner, const FileDialogOptions& options, std::string& outPath);
        static bool ShowOpenFileMulti(HWND owner, const FileDialogOptions& options, std::vector<std::string>& outPaths);
        static bool ShowSaveFile(HWND owner, const FileDialogOptions& options, std::string& outPath);

        // FOS_PICKFOLDERS on an IFileOpenDialog - the modern Common Item
        // Dialog replacement for the old SHBrowseForFolder.
        static bool ShowBrowseForFolder(HWND owner, const FileDialogOptions& options, std::string& outPath);

        void setTitle(const std::string& title) {
            frame_.setTitle(title);
        }

        std::string getTitle() const {
            return frame_.getTitle();
        }

        void setBounds(const Rect& bounds) {
            frame_.setBounds(bounds);
        }

        const Rect& getBounds() const {
            return frame_.getBounds();
        }

        RootView& getView() {
            return frame_.getView();
        }

        const RootView& getView() const {
            return frame_.getView();
        }

        HWND dialogHandle() const {
            return frame_.frameHandle();
        }

        typedef Delegate<Dialog> ClosedDelegate;

        // Fires exactly once, right as the dialog's result becomes final -
        // for show() that's whenever something later calls close(); for
        // showModal() it fires before showModal() itself returns.
        ClosedDelegate onClosed;

        // Shows the dialog and returns immediately - the caller's own
        // RunLoop keeps pumping it alongside everything else. Returns
        // false if the native window couldn't be created, or this Dialog
        // was already closed (see class comment - single-show).
        bool show();

        // Shows the dialog, then blocks the caller via
        // Application::instance().runLoop().runModal() - see RunLoop::
        // runModal() for exactly what that means (disables owner,
        // pauses RunLoop::post()/postIdle() processing, requires
        // RunLoop::run() to already be pumping on this same thread) -
        // until close() is called or the user closes the dialog window
        // directly (X button / Alt+F4), which behaves like
        // close(DialogResult::Cancel). Returns DialogResult::Cancel
        // immediately, without showing anything, if the native window
        // couldn't be created or this Dialog was already closed.
        DialogResult showModal(Frame* owner = nullptr);

        // Closes the dialog, recording result - the first close() call
        // (whether this explicit call, or the user closing the window
        // directly) wins; later calls are ignored. Safe to call before
        // the dialog has ever been shown (just finalizes result() - no
        // window to tear down).
        void close(DialogResult result = DialogResult::Cancel);

        // DialogResult::None until close() (by either path above) has
        // actually finished.
        DialogResult result() const {
            return result_;
        }

        bool isClosed() const {
            return closed_;
        }

    private:
        bool ensureInitialized();

        SyncReturn handleFrameClosed(Frame& frame);

        Frame frame_;
        DialogResult result_ = DialogResult::None;
        bool closed_ = false;
    };

}
