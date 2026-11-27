// Cursor's platform-neutral state; creating and freeing native handles is in src/win32/cursor_win32.cpp.
#include "newui/cursor.h"

namespace newui {

    void Cursor::setPath(const std::string& path) {
        if (path.empty()) {
            // Clearing a file-based custom cursor goes back to the system kind it had before; a
            // system kind or an image-built cursor has no path to clear, so it is left alone.
            if (kind_ == CursorKind::Custom && !path_.empty()) {
                setCursorKind(fallbackKind_);
            }
            return;
        }
        loadPath(path);  // on failure the cursor is unchanged; on success kind() becomes Custom
    }

    std::string Cursor::cursorKindToString(CursorKind kind) {
        switch (kind) {
            case CursorKind::Arrow: return "Arrow";
            case CursorKind::IBeam: return "IBeam";
            case CursorKind::Wait: return "Wait";
            case CursorKind::Cross: return "Cross";
            case CursorKind::Hand: return "Hand";
            case CursorKind::SizeNS: return "SizeNS";
            case CursorKind::SizeWE: return "SizeWE";
            case CursorKind::SizeNWSE: return "SizeNWSE";
            case CursorKind::SizeNESW: return "SizeNESW";
            case CursorKind::SizeAll: return "SizeAll";
            case CursorKind::No: return "No";
            case CursorKind::AppStarting: return "AppStarting";
            case CursorKind::Help: return "Help";
            case CursorKind::Custom: return "Custom";
        }
        return "Arrow";
    }

    CursorKind Cursor::cursorKindFromString(const std::string& s, CursorKind defaultValue) {
        if (s == "Arrow") return CursorKind::Arrow;
        if (s == "IBeam") return CursorKind::IBeam;
        if (s == "Wait") return CursorKind::Wait;
        if (s == "Cross") return CursorKind::Cross;
        if (s == "Hand") return CursorKind::Hand;
        if (s == "SizeNS") return CursorKind::SizeNS;
        if (s == "SizeWE") return CursorKind::SizeWE;
        if (s == "SizeNWSE") return CursorKind::SizeNWSE;
        if (s == "SizeNESW") return CursorKind::SizeNESW;
        if (s == "SizeAll") return CursorKind::SizeAll;
        if (s == "No") return CursorKind::No;
        if (s == "AppStarting") return CursorKind::AppStarting;
        if (s == "Help") return CursorKind::Help;
        if (s == "Custom") return CursorKind::Custom;
        return defaultValue;
    }

}
