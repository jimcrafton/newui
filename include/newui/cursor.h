#pragma once

#include <newui/newui.h>

#include <blend2d/blend2d.h>

#include <string>
#include <utility>

namespace newui {

    // Standard Win32 system cursor shapes selectable per-View (see
    // View::setCursor()/View::cursorKind(), backed by a Cursor member -
    // see the Cursor class below). Custom means "this Cursor built its
    // own HCURSOR, from a file path (setPath()) or an in-memory image
    // (setImage())" instead of picking a system shape - Cursor never
    // accepts/wraps an externally-supplied HCURSOR, so it's always the
    // one responsible for eventually freeing whatever handle_ it holds.
    enum class CursorKind {
        Arrow,
        IBeam,
        Wait,
        Cross,
        Hand,
        SizeNS,
        SizeWE,
        SizeNWSE,
        SizeNESW,
        SizeAll,
        No,
        AppStarting,
        Help,
        Custom
    };

    // Value type wrapping a single showable cursor - either a system
    // CursorKind, or a "Custom" one Cursor built itself (from a file via
    // setPath(), or an in-memory image via setImage()) and therefore
    // always owns - released via ::DestroyCursor() on replacement or
    // destruction. View holds exactly one Cursor member (view.h) instead
    // of separately tracking a kind/handle/ownership-flag itself.
    //
    // Move-only (like ThemedViewStyle - viewstyle.h - Cursor may own a
    // native Win32 handle, so copying it would either double-free or
    // silently share ownership).
    class Cursor {
    public:

        enum Constants {
            MaxCursorSize = 256
        };

        Cursor() = default;
        explicit Cursor(CursorKind kind) { setCursorKind(kind); }

        explicit Cursor(const BLImage& image, int hotspotX = 0, int hotspotY = 0, int maxSize = 32) {
            setImage(image, hotspotX, hotspotY, maxSize);
        }

        explicit Cursor(const std::string& path, int hotspotX = 0, int hotspotY = 0, int maxSize = 32) {
            setPath(path, hotspotX, hotspotY, maxSize);
        }

        Cursor(const Cursor&) = delete;
        Cursor& operator=(const Cursor&) = delete;

        Cursor(Cursor&& other) noexcept {
            *this = std::move(other);
        }

        Cursor& operator=(Cursor&& other) noexcept {
            if (this != &other) {
                releaseOwnedHandle();
                kind_ = other.kind_;
                path_ = std::move(other.path_);
                handle_ = other.handle_;

                other.kind_ = CursorKind::Arrow;
                other.path_.clear();
                other.handle_ = nullptr;
            }
            return *this;
        }

        ~Cursor() {
            releaseOwnedHandle();
        }

        // Switches to a system shape - frees any Custom handle this
        // Cursor previously built first.
        void setCursorKind(CursorKind kind) {
            releaseOwnedHandle();
            kind_ = kind;
            path_.clear();
        }

        // Loads path via loadCursorFromFile() and adopts the result -
        // this Cursor owns the loaded HCURSOR from here on (released via
        // ::DestroyCursor() on the next setCursorKind()/setPath()/
        // setImage() call, or destruction). kind() becomes Custom and
        // path() becomes path on success. Returns false, leaving this
        // Cursor completely unchanged, if the file can't be loaded
        // (missing/unreadable file, undecodable format, oversized image).
        bool setPath(const std::string& path, int hotspotX = 0, int hotspotY = 0, int maxSize = 32);

        CursorKind kind() const {
            return kind_;
        }

        // The path setPath() last loaded successfully - empty for every
        // other case (a system kind, or a setImage()-built handle), even
        // though kind() is Custom in the setImage() case too.
        const std::string& path() const {
            return path_;
        }

        // Builds a cursor directly from an already-decoded image and
        // adopts the result - like setPath(), this Cursor owns the built
        // HCURSOR from here on (released the same way). kind() becomes
        // Custom and path() stays/becomes empty (there's no file path for
        // an in-memory image) on success. Returns false, leaving this
        // Cursor completely unchanged, if image can't be converted to a
        // cursor (empty image, or either dimension exceeds maxSize).
        bool setImage(const BLImage& image, int hotspotX = 0, int hotspotY = 0, int maxSize = 32);

        // Resolves to a real, showable HCURSOR - a system shape via the
        // enclosing kind(), or whatever handle_ currently holds when
        // kind() is Custom.
        HCURSOR handle() const;

        // True iff this Cursor has neither a loaded path() nor a
        // setImage()-built handle - i.e. it's carrying no Custom payload
        // at all (a system CursorKind, including the untouched default,
        // always reports true here; use kind()/handle() to ask "what
        // would actually be shown", this is specifically about the
        // Custom-cursor payload).
        bool isNull() const {
            return path_.empty() && handle_ == nullptr;
        }

        bool empty() const {
            return isNull();
        }

        // True iff this Cursor is responsible for freeing handle_ itself
        // (::DestroyCursor(), via releaseOwnedHandle()) - Cursor never
        // accepts an externally-supplied HCURSOR (no setHandle()/
        // Cursor(HCURSOR) escape hatch), so this is exactly "is kind()
        // Custom and is there actually a handle to free" - no separate
        // ownership flag needed.
        bool ownsHandle() const {
            return kind_ == CursorKind::Custom && handle_ != nullptr;
        }

        // String <-> CursorKind conversion.
        static std::string cursorKindToString(CursorKind kind);
        static CursorKind cursorKindFromString(const std::string& s, CursorKind defaultValue);

    private:
        // Frees handle_ via ::DestroyCursor() iff ownsHandle(), then
        // always clears handle_ - self-contained, so every
        // setCursorKind()/setPath()/setImage() call site can call this
        // first without also having to remember to clear handle_ itself
        // afterward.
        void releaseOwnedHandle();

        CursorKind kind_ = CursorKind::Arrow;
        std::string path_;
        HCURSOR handle_ = nullptr;
    };

}
