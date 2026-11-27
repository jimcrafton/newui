// TODO(macport): placeholder until NSCursor support. No native cursors: loading fails and handle() is null, so a
// Cursor keeps its system kind. These don't abort because every View destroys a Cursor.
#include "newui/cursor.h"

namespace newui {

    bool Cursor::loadPath(const std::string&, int, int, int) { return false; }
    bool Cursor::setImage(const BLImage&, int, int, int) { return false; }
    NativeCursorHandle Cursor::handle() const { return nullptr; }
    void Cursor::releaseOwnedHandle() { handle_ = nullptr; }

}
