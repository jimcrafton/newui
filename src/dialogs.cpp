#include "newui/dialogs.h"

#include "newui/application.h"
#include "newui/runloop.h"

#include <shlobj.h>
#include <shobjidl.h>

namespace newui {

namespace {

    std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) {
            return std::wstring();
        }
        int required = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        if (required <= 0) {
            return std::wstring();
        }
        std::wstring result(static_cast<size_t>(required), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), required);
        return result;
    }

    std::string WideToUtf8(const wchar_t* text) {
        if (text == nullptr || *text == L'\0') {
            return std::string();
        }
        int len = static_cast<int>(wcslen(text));
        int required = ::WideCharToMultiByte(CP_UTF8, 0, text, len, nullptr, 0, nullptr, nullptr);
        if (required <= 0) {
            return std::string();
        }
        std::string result(static_cast<size_t>(required), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text, len, result.data(), required, nullptr, nullptr);
        return result;
    }

    // Initializes COM (STA) for the current thread if it isn't already,
    // and releases it again on destruction - only if this instance is the
    // one that actually initialized it (matching CoInitializeEx's own
    // refcounting: every successful call needs a matching CoUninitialize,
    // but only the caller that gets S_OK/S_FALSE took a reference to
    // release - RPC_E_CHANGED_MODE means someone else already owns the
    // apartment with an incompatible model, nothing for us to release).
    // newui::RunLoop::run() already calls OleInitialize() once at startup
    // (implies COINIT_APARTMENTTHREADED), so on the normal UI thread this
    // just bumps a refcount; a caller off that thread (e.g. a test) still
    // gets a working apartment.
    class ComScope {
    public:
        ComScope() {
            HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            ownsInit_ = SUCCEEDED(hr);
            valid_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        }

        ~ComScope() {
            if (ownsInit_) {
                ::CoUninitialize();
            }
        }

        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;

        bool valid() const {
            return valid_;
        }

    private:
        bool ownsInit_ = false;
        bool valid_ = false;
    };

    // storage keeps the wide strings alive - COMDLG_FILTERSPEC only holds
    // raw pointers into them, and SetFileTypes doesn't copy the array
    // itself. Two passes (fill storage completely, then point specs at
    // it) rather than interleaving pushes to each, so no spec pointer can
    // ever be taken before storage has settled into its final addresses.
    std::vector<COMDLG_FILTERSPEC> BuildFilterSpecs(const std::vector<FileDialogFilter>& filters,
                                                      std::vector<std::wstring>& storage) {
        storage.clear();
        storage.reserve(filters.size() * 2);
        for (const auto& filter : filters) {
            storage.push_back(Utf8ToWide(filter.name));
            storage.push_back(Utf8ToWide(filter.pattern));
        }

        std::vector<COMDLG_FILTERSPEC> specs;
        specs.reserve(filters.size());
        for (size_t i = 0; i < filters.size(); ++i) {
            COMDLG_FILTERSPEC spec = { storage[i * 2].c_str(), storage[i * 2 + 1].c_str() };
            specs.push_back(spec);
        }
        return specs;
    }

    // filterStorage/filterSpecs are out-params purely so their backing
    // wide strings stay alive in the caller's frame through Show() - see
    // BuildFilterSpecs' comment.
    void ApplyCommonOptions(IFileDialog* dialog, const FileDialogOptions& options,
                             std::vector<std::wstring>& filterStorage,
                             std::vector<COMDLG_FILTERSPEC>& filterSpecs) {
        if (!options.title.empty()) {
            dialog->SetTitle(Utf8ToWide(options.title).c_str());
        }
        if (!options.defaultFileName.empty()) {
            dialog->SetFileName(Utf8ToWide(options.defaultFileName).c_str());
        }
        if (!options.defaultExtension.empty()) {
            dialog->SetDefaultExtension(Utf8ToWide(options.defaultExtension).c_str());
        }
        if (!options.initialDirectory.empty()) {
            IShellItem* folder = nullptr;
            std::wstring wideDir = Utf8ToWide(options.initialDirectory);
            if (SUCCEEDED(::SHCreateItemFromParsingName(wideDir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (!options.filters.empty()) {
            filterSpecs = BuildFilterSpecs(options.filters, filterStorage);
            dialog->SetFileTypes(static_cast<UINT>(filterSpecs.size()), filterSpecs.data());
        }
    }

    bool ExtractItemPath(IShellItem* item, std::string& outPath) {
        PWSTR path = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) {
            return false;
        }
        outPath = WideToUtf8(path);
        ::CoTaskMemFree(path);
        return true;
    }

    bool ExtractResultPath(IFileDialog* dialog, std::string& outPath) {
        IShellItem* item = nullptr;
        if (FAILED(dialog->GetResult(&item)) || item == nullptr) {
            return false;
        }
        bool ok = ExtractItemPath(item, outPath);
        item->Release();
        return ok;
    }

}  // namespace

DialogResult Dialog::ShowMessageBox(HWND owner, const std::string& text, const std::string& title,
                                     MessageBoxButtons buttons, MessageBoxIcon icon) {
    UINT flags = 0;
    switch (buttons) {
        case MessageBoxButtons::Ok:              flags |= MB_OK; break;
        case MessageBoxButtons::OkCancel:         flags |= MB_OKCANCEL; break;
        case MessageBoxButtons::YesNo:            flags |= MB_YESNO; break;
        case MessageBoxButtons::YesNoCancel:      flags |= MB_YESNOCANCEL; break;
        case MessageBoxButtons::RetryCancel:      flags |= MB_RETRYCANCEL; break;
        case MessageBoxButtons::AbortRetryIgnore: flags |= MB_ABORTRETRYIGNORE; break;
    }

    switch (icon) {
        case MessageBoxIcon::None:        break;
        case MessageBoxIcon::Information: flags |= MB_ICONINFORMATION; break;
        case MessageBoxIcon::Warning:     flags |= MB_ICONWARNING; break;
        case MessageBoxIcon::Error:       flags |= MB_ICONERROR; break;
        case MessageBoxIcon::Question:    flags |= MB_ICONQUESTION; break;
    }

    int id = ::MessageBoxA(owner, text.c_str(), title.c_str(), flags);
    switch (id) {
        case IDOK:     return DialogResult::Ok;
        case IDCANCEL: return DialogResult::Cancel;
        case IDYES:    return DialogResult::Yes;
        case IDNO:     return DialogResult::No;
        case IDABORT:  return DialogResult::Abort;
        case IDRETRY:  return DialogResult::Retry;
        case IDIGNORE: return DialogResult::Ignore;
        default:       return DialogResult::None;
    }
}

bool Dialog::ShowOpenFile(HWND owner, const FileDialogOptions& options, std::string& outPath) {
    ComScope com;
    if (!com.valid()) {
        return false;
    }

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))
        || dialog == nullptr) {
        return false;
    }

    std::vector<std::wstring> filterStorage;
    std::vector<COMDLG_FILTERSPEC> filterSpecs;
    ApplyCommonOptions(dialog, options, filterStorage, filterSpecs);

    bool result = SUCCEEDED(dialog->Show(owner)) && ExtractResultPath(dialog, outPath);
    dialog->Release();
    return result;
}

bool Dialog::ShowOpenFileMulti(HWND owner, const FileDialogOptions& options, std::vector<std::string>& outPaths) {
    ComScope com;
    if (!com.valid()) {
        return false;
    }

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))
        || dialog == nullptr) {
        return false;
    }

    DWORD existingOptions = 0;
    dialog->GetOptions(&existingOptions);
    dialog->SetOptions(existingOptions | FOS_ALLOWMULTISELECT);

    std::vector<std::wstring> filterStorage;
    std::vector<COMDLG_FILTERSPEC> filterSpecs;
    ApplyCommonOptions(dialog, options, filterStorage, filterSpecs);

    bool result = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items != nullptr) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item)) && item != nullptr) {
                    std::string path;
                    if (ExtractItemPath(item, path)) {
                        outPaths.push_back(std::move(path));
                    }
                    item->Release();
                }
            }
            items->Release();
            result = !outPaths.empty();
        }
    }

    dialog->Release();
    return result;
}

bool Dialog::ShowSaveFile(HWND owner, const FileDialogOptions& options, std::string& outPath) {
    ComScope com;
    if (!com.valid()) {
        return false;
    }

    IFileSaveDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))
        || dialog == nullptr) {
        return false;
    }

    std::vector<std::wstring> filterStorage;
    std::vector<COMDLG_FILTERSPEC> filterSpecs;
    ApplyCommonOptions(dialog, options, filterStorage, filterSpecs);

    bool result = SUCCEEDED(dialog->Show(owner)) && ExtractResultPath(dialog, outPath);
    dialog->Release();
    return result;
}

bool Dialog::ShowBrowseForFolder(HWND owner, const FileDialogOptions& options, std::string& outPath) {
    ComScope com;
    if (!com.valid()) {
        return false;
    }

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))
        || dialog == nullptr) {
        return false;
    }

    DWORD existingOptions = 0;
    dialog->GetOptions(&existingOptions);
    dialog->SetOptions(existingOptions | FOS_PICKFOLDERS);

    // Folder picking has no file-type filters to apply - only
    // title/default name/initial directory are meaningful here.
    FileDialogOptions folderOptions = options;
    folderOptions.filters.clear();
    std::vector<std::wstring> filterStorage;
    std::vector<COMDLG_FILTERSPEC> filterSpecs;
    ApplyCommonOptions(dialog, folderOptions, filterStorage, filterSpecs);

    bool result = SUCCEEDED(dialog->Show(owner)) && ExtractResultPath(dialog, outPath);
    dialog->Release();
    return result;
}

Dialog::Dialog() {
    frame_.onClosed.add(this, &Dialog::handleFrameClosed);
}

Dialog::~Dialog() {
    // Frame::~Frame() throws if a live window still exists with its
    // rootView_ not yet torn down (see frame.cpp) - close()/the user
    // closing the window normally already does this via WM_CLOSE ->
    // WM_DESTROY, but if this Dialog is destroyed while still open
    // (show()'d and never closed), force the window down synchronously
    // here first so frame_'s own destructor - which runs right after this
    // one finishes, as a member - never sees a live rootView_ next to a
    // live frameHandle_. A Dialog that was never shown at all needs no
    // such help: frameHandle_ is still null, and Frame::~Frame() itself
    // already knows a never-created window has nothing to tear down.
    if (frame_.frameHandle() != nullptr) {
        ::DestroyWindow(frame_.frameHandle());
    }
}

bool Dialog::ensureInitialized() {
    if (closed_) {
        return false;  // single-show - see class comment in dialogs.h
    }
    if (frame_.frameHandle() != nullptr) {
        return true;
    }
    return frame_.initialize();
}

SyncReturn Dialog::handleFrameClosed(Frame&) {
    if (result_ == DialogResult::None) {
        // The window closed some other way than Dialog::close() (native X
        // button / Alt+F4/System menu Close) - treat that the same as an
        // explicit Cancel.
        result_ = DialogResult::Cancel;
    }
    closed_ = true;
    onClosed(*this);
    return SyncReturn::Handled;
}

bool Dialog::show() {
    return ensureInitialized();
}

DialogResult Dialog::showModal(Frame* owner) {
    if (!ensureInitialized()) {
        return DialogResult::Cancel;
    }

    HWND ownerHandle = (owner != nullptr) ? owner->frameHandle() : nullptr;

    RunLoop* loop = RunLoop::current();
    if (nullptr == loop) {
        throw std::runtime_error("newui::Dialog::showModal: no RunLoop is running on this thread");
    }

    bool completedNormally = loop->runModal(
        frame_.frameHandle(), ownerHandle, [this]() { return closed_; });
    if (!completedNormally) {
        // runModal() gave up because WM_QUIT reached this thread (the app
        // itself is shutting down), not because closed_ actually became
        // true - nothing else is going to close this dialog now, so
        // finalize it here instead of leaving isClosed()/result() stuck
        // mid-flight.
        if (result_ == DialogResult::None) {
            result_ = DialogResult::Cancel;
        }
        closed_ = true;
    }

    return result_;
}

void Dialog::close(DialogResult result) {
    if (result_ != DialogResult::None) {
        return;  // first close() (or the user closing the window) already won
    }
    result_ = result;
    if (frame_.frameHandle() != nullptr) {
        // Async on purpose - lets this run from inside a handler that's
        // itself on the frame's message-dispatch call stack (e.g. a
        // button's onClicked) without reentering DestroyWindow from
        // there. Funnels through the exact same WM_CLOSE path a native X
        // click takes, so handleFrameClosed() above is the single place
        // that actually finalizes things.
        ::PostMessage(frame_.frameHandle(), WM_CLOSE, 0, 0);
    } else {
        closed_ = true;
        onClosed(*this);
    }
}

}
