#pragma once

#include <newui/newui.h>

#include <blend2d/blend2d.h>

#include <newui/delegate.h>
#include <newui/geometry.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace newui {

class RootView;
class View;

// Newui's own effect enum, independent of the raw DROPEFFECT_* bits -
// keeps the friendly DropSource/DropTarget classes below (and anything a
// View wires up against them) free of raw OLE types, same spirit as the
// rest of this header keeping COM specifics out of application-facing
// surfaces where it can. toDropEffect()/toDropEffectMask() (src/
// dragndrop.cpp) convert to/from the real DWORD DROPEFFECT_* values at
// the boundary where COMDropSource/COMDropTarget actually talk OLE.
enum class DropEffect { None, Copy, Move, Link };

// Conversion to/from the raw OLE DROPEFFECT_* bits lives in newui/win32/dragndrop_com.h.

// One in-memory virtual file - a name and its full contents, materialized
// as a real physical file by whatever it's dropped onto (Explorer, most
// commonly) without this app ever writing a temp file itself. See
// VirtualFileDataObject/StartVirtualFileDrag below.
struct VirtualFile {
    std::wstring fileName;
    std::vector<std::uint8_t> contents;
};

// A per-View association for *originating* an outgoing OLE drag - not a
// COM object itself (see COMDropSource/StartDragOperation/
// StartVirtualFileDrag below for the real IDropSource/DoDragDrop plumbing
// this drives). A View that wants to be draggable adds a handler to
// onProvideText and/or onProvideFiles that fills in the payload and
// returns SyncReturn::Handled; declining (returning anything else - e.g.
// "nothing selected right now") means no drag starts. Nothing here starts
// a drag by itself: RootView's own mouse dispatch (rootview.cpp) is what
// detects a press-and-move-past-threshold gesture on a View that has one
// of these (View::dragSource()) and, only once one of these is actually
// handled, calls StartDragOperation()/StartVirtualFileDrag() itself -
// checked in that priority order (files before text, matching
// DropTarget's own CF_HDROP-before-CF_UNICODETEXT priority below), so a
// View with both populated only ever starts one kind of drag per gesture.
// onDragComplete fires afterward either way, with whichever DropEffect
// was actually achieved (DropEffect::None if the drag was cancelled) - a
// notification, nothing needs to handle it.
// @reflect ignore=true
class DropSource {
public:
    Delegate<DropSource, std::vector<VirtualFile>&> onProvideFiles;
    Delegate<DropSource, std::u16string&> onProvideText;
    Delegate<DropSource, DropEffect> onDragComplete;
};

// A per-View association for *accepting* an incoming OLE drop - not a COM
// object itself (see COMDropTarget below for the real IDropTarget
// implementation this drives). A View that wants to accept drops adds
// handlers to whichever of these matches the payload kind(s) it cares
// about, same idiom as View::onMouseDown etc. (view.h) -
// view->dropTarget()->onTextDropped.add(...) and so on. "Does this View
// accept a given drag" is answered generically by COMDropTarget's own
// per-View hit-testing: at least one of these has a listener *and* the
// drag actually offers a matching format (CF_HDROP/CF_DIB/CF_UNICODETEXT
// respectively) - checked in that priority order, so a View with more
// than one populated only ever gets the first one that both has a
// listener and matches what was actually dropped.
// @reflect ignore=true
class DropTarget {
public:
    Delegate<DropTarget, const std::vector<std::wstring>&> onFilesDropped;
    Delegate<DropTarget, const BLImage&> onImageDropped;
    Delegate<DropTarget, const std::u16string&> onTextDropped;

    // Text drops with a position, and live hover feedback while one is in flight - what a
    // designer-style surface needs to show "it would land here" cues. All points are in this
    // target's own View's local space. A View accepts a text drag if either onTextDropped or
    // onTextDroppedAt has a listener (the hover delegates alone never make a View accept).
    //
    // onTextDropped's positioned twin - both fire on a drop when both have listeners.
    Delegate<DropTarget, const std::u16string&, const Point&> onTextDroppedAt;

    // Fired for the drag's first move over this target and every move after (the first call after
    // onDragLeave is the "enter"), with the dragged text and the current point. A handler can set
    // effect to DropEffect::None to refuse a drop at this point (the cursor shows "not allowed",
    // and the drop itself is refused there too); it starts as Copy.
    Delegate<DropTarget, const std::u16string&, const Point&, DropEffect&> onTextDragOver;

    // The hover over this target ended: the drag left it, was cancelled, or is about to be dropped
    // on it (fired right before onTextDropped/onTextDroppedAt) - so a subscriber only needs this
    // one notification to clear whatever hover feedback onTextDragOver showed.
    Delegate<DropTarget> onDragLeave;
};

}
