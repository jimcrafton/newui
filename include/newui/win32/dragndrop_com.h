#pragma once

// Windows-only OLE/COM drag-and-drop plumbing behind the platform-neutral DropSource/DropTarget in
// newui/dragndrop.h: IDataObject extraction, the WRL data objects, IDropSource and the per-window IDropTarget.

#include "newui/platform.h"

#if !NEWUI_PLATFORM_WIN32
#   error "newui/win32/dragndrop_com.h is Windows-only"
#endif

#include <newui/newui.h>
#include <newui/dragndrop.h>
#include <newui/geometry.h>

#include <blend2d/blend2d.h>

#include <shlobj.h>

#include <wrl/client.h>
#include <wrl/implements.h>

#include <string>
#include <vector>

namespace newui {

class RootView;
class View;

// Converts to/from the raw DWORD DROPEFFECT_* values (src/dragndrop.cpp).
DropEffect toDropEffect(DWORD rawEffect);
DWORD toDropEffectMask(DropEffect effect);

// Extraction utilities (src/dragndrop.cpp) - pull a single payload kind out
// of an IDataObject (a real incoming drop, or a test's mock) via WIL's
// unique_stg_medium/unique_hglobal_locked, so callers never manage
// ReleaseStgMedium/GlobalUnlock by hand. Each returns false (leaving the
// out-param untouched) if dataObject is null or doesn't offer that format -
// not an error, just "this drop doesn't carry this payload kind".

// CF_HDROP -> a real, absolute file path per dropped file.
bool extractFilePaths(IDataObject* dataObject, std::vector<std::wstring>& outPaths);

// CF_UNICODETEXT -> its UTF-16 text.
bool extractUnicodeText(IDataObject* dataObject, std::u16string& outText);

// CF_DIB -> decoded via dibimage.h's dibBytesToImage().
bool extractDibImage(IDataObject* dataObject, BLImage& outImage);

// TextDataObject - a WRL-backed IDataObject exposing a single in-memory
// std::wstring as CF_UNICODETEXT/TYMED_HGLOBAL, for outgoing text drags
// started via StartDragOperation() below. Only the CF_UNICODETEXT/
// DVASPECT_CONTENT/TYMED_HGLOBAL shape is supported - everything else
// (GetDataHere, SetData, D(Un)Advise) is refused per the IDataObject
// contract's own documented "not supported" returns.
// @reflect ignore=true
class TextDataObject final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDataObject> {
public:
    explicit TextDataObject(std::u16string text);

    STDMETHODIMP GetData(FORMATETC* formatEtcIn, STGMEDIUM* medium) override;
    STDMETHODIMP GetDataHere(FORMATETC* formatEtc, STGMEDIUM* medium) override;
    STDMETHODIMP QueryGetData(FORMATETC* formatEtc) override;
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* formatEtcIn, FORMATETC* formatEtcOut) override;
    STDMETHODIMP SetData(FORMATETC* formatEtc, STGMEDIUM* medium, BOOL shouldRelease) override;
    STDMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumFormatEtc) override;
    STDMETHODIMP DAdvise(FORMATETC* formatEtc, DWORD advf, IAdviseSink* adviseSink, DWORD* connection) override;
    STDMETHODIMP DUnadvise(DWORD connection) override;
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** enumAdvise) override;

private:
    std::u16string text_;
};

// VirtualFileDataObject - a WRL-backed IDataObject exposing a set of
// in-memory VirtualFiles as CFSTR_FILEDESCRIPTORW (one FILEGROUPDESCRIPTORW
// listing every file's name/size) + CFSTR_FILECONTENTS (one IStream per
// file, keyed by FORMATETC::lindex) - the shell's own protocol for
// "materialize these as real physical files on drop," used by
// StartVirtualFileDrag() below. Both formats are registered at runtime via
// RegisterClipboardFormatW() (their numeric values aren't stable across
// sessions, so they can't be hardcoded) - fileDescriptorFormat()/
// fileContentsFormat() expose them for tests. TYMED_ISTREAM (not
// TYMED_HGLOBAL) for contents - Explorer expects streaming delivery for
// virtual files, especially medium/large ones. Everything else
// (GetDataHere, SetData, D(Un)Advise) is refused per the IDataObject
// contract's own documented "not supported" returns, same as
// TextDataObject above.
// @reflect ignore=true
class VirtualFileDataObject final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDataObject> {
public:
    explicit VirtualFileDataObject(std::vector<VirtualFile> files);

    CLIPFORMAT fileDescriptorFormat() const { return fileDescriptorFormat_; }
    CLIPFORMAT fileContentsFormat() const { return fileContentsFormat_; }

    STDMETHODIMP GetData(FORMATETC* formatEtcIn, STGMEDIUM* medium) override;
    STDMETHODIMP GetDataHere(FORMATETC* formatEtc, STGMEDIUM* medium) override;
    STDMETHODIMP QueryGetData(FORMATETC* formatEtc) override;
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* formatEtcIn, FORMATETC* formatEtcOut) override;
    STDMETHODIMP SetData(FORMATETC* formatEtc, STGMEDIUM* medium, BOOL shouldRelease) override;
    STDMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumFormatEtc) override;
    STDMETHODIMP DAdvise(FORMATETC* formatEtc, DWORD advf, IAdviseSink* adviseSink, DWORD* connection) override;
    STDMETHODIMP DUnadvise(DWORD connection) override;
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** enumAdvise) override;

private:
    std::vector<VirtualFile> files_;
    CLIPFORMAT fileDescriptorFormat_;
    CLIPFORMAT fileContentsFormat_;
};

// COMDropSource - a minimal WRL-backed IDropSource for StartDragOperation()
// below: always defers to the shell's default cursors (the
// IDragSourceHelper-driven drag thumbnail makes custom cursor art
// unnecessary) and ends the drag as soon as the left mouse button is
// released or Escape is pressed. Purely OLE-protocol glue - it has no
// per-drag customization surface, unlike DropSource above (the friendly,
// per-View association a View actually configures).
// @reflect ignore=true
class COMDropSource final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDropSource> {
public:
    STDMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState) override;
    STDMETHODIMP GiveFeedback(DWORD effect) override;
};

// Starts an outgoing text drag from hwndSource - wraps DoDragDrop() with a
// TextDataObject/COMDropSource pair. ptCursorClient is the drag's starting
// cursor position in hwndSource's client coordinates, matching
// IDragSourceHelper::InitializeFromWindow()'s own contract; that shell
// helper is used internally so Windows renders the default drag thumbnail
// automatically - CoCreateInstance failing for it is not fatal, just loses
// the thumbnail. Returns DoDragDrop()'s own HRESULT (S_OK with *outEffect
// set to whichever of allowedEffects was accepted on a completed drop,
// DRAGDROP_S_CANCEL if the user aborted); outEffect may be null if the
// caller doesn't care which effect was chosen. Low-level plumbing - the
// normal way a drag actually starts is a View's own DropSource
// (View::dragSource()) being asked by RootView's generic mouse-gesture
// detection (rootview.cpp), not a direct call to this from application
// code.
HRESULT StartDragOperation(HWND hwndSource, const std::u16string& text, POINT ptCursorClient,
    DWORD allowedEffects = DROPEFFECT_COPY, DWORD* outEffect = nullptr);

// Starts an outgoing virtual-file drag from hwndSource - wraps
// DoDragDrop() with a VirtualFileDataObject/COMDropSource pair, same shape
// as StartDragOperation() above (including reusing the IDragSourceHelper
// wiring for the default drag thumbnail) but for one or more in-memory
// VirtualFiles instead of plain text. Dropping onto Explorer materializes
// each as a real physical file with the given name/contents, with
// Explorer's own native copy-progress dialog for large payloads
// (VirtualFileDataObject's FD_PROGRESSUI). Same return-value contract as
// StartDragOperation().
HRESULT StartVirtualFileDrag(HWND hwndSource, std::vector<VirtualFile> files, POINT ptCursorClient,
    DWORD allowedEffects = DROPEFFECT_COPY, DWORD* outEffect = nullptr);

// COMDropTarget - the one WRL-backed IDropTarget registered per window
// (RegisterDragDrop() is scoped to a single HWND, and this framework backs
// a whole RootView with exactly one real window - see this class's own
// dragEnterAt()/dragOverAt()/dropAt() for how it then fans back out to
// individual Views). DragEnter/DragOver/Drop are thin wrappers converting
// the OS's screen-coordinate POINTL to rootView's own local/client space
// (ScreenToClient()) and forwarding to the shell's own IDropTargetHelper
// (CoCreateInstance(CLSID_DragDropHelper, ...), stored in the constructor -
// guarded by whether that helper actually came up, since CoCreateInstance
// can fail gracefully and isn't fatal here) so Windows renders/animates
// the drag image; *At() below do the real work in client-space Point
// terms, directly callable by tests without needing a real window's
// actual screen position.
//
// Per-View hit-testing: given the client-space cursor point, hit-tests
// rootView's tree (View::hitTestChildren()) and then walks from that View
// up through parent() (mirroring RootView::canPerformCommand()'s own
// chain-walk, command.h) looking for the first View whose DropTarget
// (View::dropTarget()) has a listener that also matches an actually-
// offered format - so a container can accept a drop anywhere inside it
// even over a child that has no DropTarget of its own. Only ever
// negotiates DROPEFFECT_COPY (DROPEFFECT_NONE if no View in the chain
// accepts, or if the source didn't allow COPY in the first place) - this
// project doesn't distinguish copy from move for a drop target yet.
// @reflect ignore=true
class COMDropTarget final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDropTarget> {
public:
    explicit COMDropTarget(RootView& rootView);

    STDMETHODIMP DragEnter(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) override;

    // The real logic behind the four COM entry points above, in
    // rootView's own local/client Point space - split out so it's
    // directly testable without a real window's actual screen position
    // (DragEnter/DragOver/Drop's POINTL is in screen coordinates, which
    // only means something relative to a real, positioned HWND).
    HRESULT dragEnterAt(IDataObject* dataObject, const Point& clientPt, DWORD* effect);
    HRESULT dragOverAt(const Point& clientPt, DWORD* effect);
    HRESULT dropAt(IDataObject* dataObject, const Point& clientPt, DWORD* effect);

private:
    // Runs the accept test at clientPt, fires onDragLeave/onTextDragOver as the hovered View
    // changes, and writes the negotiated effect - shared by dragEnterAt()/dragOverAt().
    void updateHover(const Point& clientPt, DWORD* effect);
    // Fires onDragLeave on the currently hovered View's DropTarget (if any) and forgets it.
    void endHover();

    RootView& rootView_;
    Microsoft::WRL::ComPtr<IDataObject> currentDataObject_;
    std::u16string currentText_;       // the drag's CF_UNICODETEXT payload, cached at drag-enter
    bool hasCurrentText_ = false;
    View* hoverView_ = nullptr;      // non-owning; the View whose DropTarget last got onTextDragOver
    Microsoft::WRL::ComPtr<IDropTargetHelper> dropTargetHelper_;
};

}
