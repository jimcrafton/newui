#include "newui/dragndrop.h"

#include "newui/dibimage.h"
#include "newui/enum_format_etc.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/view.h"

#include <ole2.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wil/com.h>
#include <wil/resource.h>

#include <shellapi.h>

#include <cstring>

namespace newui {

DropEffect toDropEffect(DWORD rawEffect) {
    if ((rawEffect & DROPEFFECT_COPY) != 0) {
        return DropEffect::Copy;
    }
    if ((rawEffect & DROPEFFECT_MOVE) != 0) {
        return DropEffect::Move;
    }
    if ((rawEffect & DROPEFFECT_LINK) != 0) {
        return DropEffect::Link;
    }
    return DropEffect::None;
}

DWORD toDropEffectMask(DropEffect effect) {
    switch (effect) {
    case DropEffect::Copy: return DROPEFFECT_COPY;
    case DropEffect::Move: return DROPEFFECT_MOVE;
    case DropEffect::Link: return DROPEFFECT_LINK;
    default: return DROPEFFECT_NONE;
    }
}

namespace {

    // Asks dataObject for format as a global-memory medium; returns false
    // (medium left default/empty) if dataObject is null, doesn't offer the
    // format, or hands back an empty handle.
    bool getGlobalMedium(IDataObject* dataObject, CLIPFORMAT format, wil::unique_stg_medium& outMedium) {
        if (dataObject == nullptr) {
            return false;
        }

        FORMATETC formatEtc{ format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (FAILED(dataObject->GetData(&formatEtc, &outMedium))) {
            return false;
        }

        return outMedium.hGlobal != nullptr;
    }

}

bool extractFilePaths(IDataObject* dataObject, std::vector<std::wstring>& outPaths) {
    wil::unique_stg_medium medium;
    if (!getGlobalMedium(dataObject, CF_HDROP, medium)) {
        return false;
    }

    HDROP hDrop = static_cast<HDROP>(medium.hGlobal);
    UINT fileCount = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

    std::vector<std::wstring> paths;
    paths.reserve(fileCount);
    for (UINT i = 0; i < fileCount; ++i) {
        UINT length = ::DragQueryFileW(hDrop, i, nullptr, 0);
        std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
        ::DragQueryFileW(hDrop, i, buffer.data(), length + 1);
        paths.emplace_back(buffer.data(), length);
    }

    outPaths = std::move(paths);
    return true;
}

bool extractUnicodeText(IDataObject* dataObject, std::wstring& outText) {
    wil::unique_stg_medium medium;
    if (!getGlobalMedium(dataObject, CF_UNICODETEXT, medium)) {
        return false;
    }

    wil::unique_hglobal_locked lock(medium);
    if (!lock) {
        return false;
    }

    outText = static_cast<const wchar_t*>(lock.get());
    return true;
}

bool extractDibImage(IDataObject* dataObject, BLImage& outImage) {
    wil::unique_stg_medium medium;
    if (!getGlobalMedium(dataObject, CF_DIB, medium)) {
        return false;
    }

    wil::unique_hglobal_locked lock(medium);
    if (!lock) {
        return false;
    }

    std::size_t size = ::GlobalSize(medium.hGlobal);
    const auto* bytes = static_cast<const std::uint8_t*>(lock.get());
    std::vector<std::uint8_t> dibBytes(bytes, bytes + size);

    return dibBytesToImage(dibBytes, outImage);
}

namespace {

    bool isSupportedTextFormat(const FORMATETC& formatEtc) {
        return formatEtc.cfFormat == CF_UNICODETEXT
            && (formatEtc.tymed & TYMED_HGLOBAL) != 0
            && formatEtc.dwAspect == DVASPECT_CONTENT;
    }

}

TextDataObject::TextDataObject(std::wstring text) : text_(std::move(text)) {
}

STDMETHODIMP TextDataObject::GetData(FORMATETC* formatEtcIn, STGMEDIUM* medium) {
    if (formatEtcIn == nullptr || medium == nullptr) {
        return E_INVALIDARG;
    }
    if (!isSupportedTextFormat(*formatEtcIn)) {
        return DV_E_FORMATETC;
    }

    std::size_t byteCount = (text_.size() + 1) * sizeof(wchar_t);
    wil::unique_hglobal handle(::GlobalAlloc(GMEM_MOVEABLE, byteCount));
    if (!handle) {
        return E_OUTOFMEMORY;
    }

    {
        wil::unique_hglobal_locked lock(handle.get());
        if (!lock) {
            return E_OUTOFMEMORY;
        }
        std::memcpy(lock.get(), text_.c_str(), byteCount);
    }

    ::ZeroMemory(medium, sizeof(*medium));
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = handle.release();
    medium->pUnkForRelease = nullptr;
    return S_OK;
}

STDMETHODIMP TextDataObject::GetDataHere(FORMATETC*, STGMEDIUM*) {
    return E_NOTIMPL;
}

STDMETHODIMP TextDataObject::QueryGetData(FORMATETC* formatEtc) {
    if (formatEtc == nullptr) {
        return E_INVALIDARG;
    }
    return isSupportedTextFormat(*formatEtc) ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP TextDataObject::GetCanonicalFormatEtc(FORMATETC*, FORMATETC* formatEtcOut) {
    if (formatEtcOut != nullptr) {
        formatEtcOut->ptd = nullptr;
    }
    return E_NOTIMPL;
}

STDMETHODIMP TextDataObject::SetData(FORMATETC*, STGMEDIUM*, BOOL) {
    return E_NOTIMPL;
}

STDMETHODIMP TextDataObject::EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumFormatEtc) {
    if (enumFormatEtc == nullptr) {
        return E_POINTER;
    }
    if (direction != DATADIR_GET) {
        return E_NOTIMPL;
    }

    FORMATETC formatEtc{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(std::vector<FORMATETC>{ formatEtc });
    if (!enumerator) {
        return E_OUTOFMEMORY;
    }
    return enumerator.CopyTo(enumFormatEtc);
}

STDMETHODIMP TextDataObject::DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) {
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP TextDataObject::DUnadvise(DWORD) {
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP TextDataObject::EnumDAdvise(IEnumSTATDATA**) {
    return OLE_E_ADVISENOTSUPPORTED;
}

VirtualFileDataObject::VirtualFileDataObject(std::vector<VirtualFile> files)
    : files_(std::move(files))
    , fileDescriptorFormat_(static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)))
    , fileContentsFormat_(static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(CFSTR_FILECONTENTS))) {
}

namespace {

    bool isDescriptorFormat(const VirtualFileDataObject& obj, const FORMATETC& formatEtc) {
        return formatEtc.cfFormat == obj.fileDescriptorFormat()
            && (formatEtc.tymed & TYMED_HGLOBAL) != 0
            && formatEtc.dwAspect == DVASPECT_CONTENT;
    }

    bool isContentsFormat(const VirtualFileDataObject& obj, const FORMATETC& formatEtc) {
        return formatEtc.cfFormat == obj.fileContentsFormat()
            && (formatEtc.tymed & TYMED_ISTREAM) != 0
            && formatEtc.dwAspect == DVASPECT_CONTENT;
    }

    bool lindexInRange(const FORMATETC& formatEtc, std::size_t count) {
        return formatEtc.lindex >= 0 && static_cast<std::size_t>(formatEtc.lindex) < count;
    }

}

STDMETHODIMP VirtualFileDataObject::QueryGetData(FORMATETC* formatEtc) {
    if (formatEtc == nullptr) {
        return E_INVALIDARG;
    }
    if (files_.empty()) {
        return DV_E_FORMATETC;
    }
    if (isDescriptorFormat(*this, *formatEtc)) {
        return S_OK;
    }
    if (isContentsFormat(*this, *formatEtc)) {
        return lindexInRange(*formatEtc, files_.size()) ? S_OK : DV_E_LINDEX;
    }
    return DV_E_FORMATETC;
}

STDMETHODIMP VirtualFileDataObject::GetData(FORMATETC* formatEtcIn, STGMEDIUM* medium) {
    if (formatEtcIn == nullptr || medium == nullptr) {
        return E_INVALIDARG;
    }
    if (files_.empty()) {
        return DV_E_FORMATETC;
    }

    if (isDescriptorFormat(*this, *formatEtcIn)) {
        std::size_t count = files_.size();
        std::size_t byteCount = sizeof(FILEGROUPDESCRIPTORW) + (count - 1) * sizeof(FILEDESCRIPTORW);
        wil::unique_hglobal handle(::GlobalAlloc(GMEM_MOVEABLE, byteCount));
        if (!handle) {
            return E_OUTOFMEMORY;
        }

        {
            wil::unique_hglobal_locked lock(handle.get());
            if (!lock) {
                return E_OUTOFMEMORY;
            }
            auto* descriptor = static_cast<FILEGROUPDESCRIPTORW*>(lock.get());
            ::ZeroMemory(descriptor, byteCount);
            descriptor->cItems = static_cast<UINT>(count);
            for (std::size_t i = 0; i < count; ++i) {
                FILEDESCRIPTORW& entry = descriptor->fgd[i];
                entry.dwFlags = FD_FILESIZE | FD_PROGRESSUI;

                const std::wstring& name = files_[i].fileName;
                std::size_t nameLength = (name.size() < MAX_PATH) ? name.size() : (MAX_PATH - 1);
                std::memcpy(entry.cFileName, name.c_str(), nameLength * sizeof(wchar_t));
                entry.cFileName[nameLength] = L'\0';

                ULARGE_INTEGER size;
                size.QuadPart = files_[i].contents.size();
                entry.nFileSizeLow = size.LowPart;
                entry.nFileSizeHigh = size.HighPart;
            }
        }

        ::ZeroMemory(medium, sizeof(*medium));
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = handle.release();
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    if (isContentsFormat(*this, *formatEtcIn)) {
        if (!lindexInRange(*formatEtcIn, files_.size())) {
            return DV_E_LINDEX;
        }

        const std::vector<std::uint8_t>& contents = files_[static_cast<std::size_t>(formatEtcIn->lindex)].contents;
        Microsoft::WRL::ComPtr<IStream> stream;
        stream.Attach(::SHCreateMemStream(contents.data(), static_cast<UINT>(contents.size())));
        if (!stream) {
            return E_OUTOFMEMORY;
        }

        ::ZeroMemory(medium, sizeof(*medium));
        medium->tymed = TYMED_ISTREAM;
        medium->pstm = stream.Detach();
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    return DV_E_FORMATETC;
}

STDMETHODIMP VirtualFileDataObject::GetDataHere(FORMATETC*, STGMEDIUM*) {
    return E_NOTIMPL;
}

STDMETHODIMP VirtualFileDataObject::GetCanonicalFormatEtc(FORMATETC*, FORMATETC* formatEtcOut) {
    if (formatEtcOut != nullptr) {
        formatEtcOut->ptd = nullptr;
    }
    return E_NOTIMPL;
}

STDMETHODIMP VirtualFileDataObject::SetData(FORMATETC*, STGMEDIUM*, BOOL) {
    return E_NOTIMPL;
}

STDMETHODIMP VirtualFileDataObject::EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumFormatEtc) {
    if (enumFormatEtc == nullptr) {
        return E_POINTER;
    }
    if (direction != DATADIR_GET) {
        return E_NOTIMPL;
    }

    FORMATETC descriptorFormatEtc{ fileDescriptorFormat_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    FORMATETC contentsFormatEtc{ fileContentsFormat_, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM };
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(
        std::vector<FORMATETC>{ descriptorFormatEtc, contentsFormatEtc });
    if (!enumerator) {
        return E_OUTOFMEMORY;
    }
    return enumerator.CopyTo(enumFormatEtc);
}

STDMETHODIMP VirtualFileDataObject::DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) {
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP VirtualFileDataObject::DUnadvise(DWORD) {
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP VirtualFileDataObject::EnumDAdvise(IEnumSTATDATA**) {
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP COMDropSource::QueryContinueDrag(BOOL escapePressed, DWORD keyState) {
    if (escapePressed) {
        return DRAGDROP_S_CANCEL;
    }
    if ((keyState & MK_LBUTTON) == 0) {
        return DRAGDROP_S_DROP;
    }
    return S_OK;
}

STDMETHODIMP COMDropSource::GiveFeedback(DWORD) {
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

HRESULT StartDragOperation(HWND hwndSource, const std::wstring& text, POINT ptCursorClient,
        DWORD allowedEffects, DWORD* outEffect) {
    auto dataObject = Microsoft::WRL::Make<TextDataObject>(text);
    auto dropSource = Microsoft::WRL::Make<COMDropSource>();

    wil::com_ptr_nothrow<IDragSourceHelper> dragSourceHelper;
    if (SUCCEEDED(::CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dragSourceHelper))) && dragSourceHelper) {
        dragSourceHelper->InitializeFromWindow(hwndSource, &ptCursorClient, dataObject.Get());
    }

    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr = ::DoDragDrop(dataObject.Get(), dropSource.Get(), allowedEffects, &effect);
    if (outEffect != nullptr) {
        *outEffect = effect;
    }
    return hr;
}

HRESULT StartVirtualFileDrag(HWND hwndSource, std::vector<VirtualFile> files, POINT ptCursorClient,
        DWORD allowedEffects, DWORD* outEffect) {
    auto dataObject = Microsoft::WRL::Make<VirtualFileDataObject>(std::move(files));
    auto dropSource = Microsoft::WRL::Make<COMDropSource>();

    wil::com_ptr_nothrow<IDragSourceHelper> dragSourceHelper;
    if (SUCCEEDED(::CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dragSourceHelper))) && dragSourceHelper) {
        dragSourceHelper->InitializeFromWindow(hwndSource, &ptCursorClient, dataObject.Get());
    }

    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr = ::DoDragDrop(dataObject.Get(), dropSource.Get(), allowedEffects, &effect);
    if (outEffect != nullptr) {
        *outEffect = effect;
    }
    return hr;
}

namespace {

    bool offersFormat(IDataObject* dataObject, CLIPFORMAT format) {
        if (dataObject == nullptr) {
            return false;
        }
        FORMATETC formatEtc{ format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return dataObject->QueryGetData(&formatEtc) == S_OK;
    }

    // True if view has its own DropTarget (View::dropTarget(), never
    // allocated just by asking) with a listener that also matches a
    // format dataObject actually offers - checked in CF_HDROP -> CF_DIB ->
    // CF_UNICODETEXT priority order, matching DropTarget's own class
    // comment (dragndrop.h).
    bool viewAcceptsOffered(View* view, IDataObject* dataObject) {
        DropTarget* dropTarget = view->dropTarget();
        if (dropTarget == nullptr) {
            return false;
        }
        if (!dropTarget->onFilesDropped.empty() && offersFormat(dataObject, CF_HDROP)) {
            return true;
        }
        if (!dropTarget->onImageDropped.empty() && offersFormat(dataObject, CF_DIB)) {
            return true;
        }
        if (!dropTarget->onTextDropped.empty() && offersFormat(dataObject, CF_UNICODETEXT)) {
            return true;
        }
        return false;
    }

    // Hit-tests rootView's tree at clientPt, then walks from the hit View
    // (or rootView itself, if the point isn't over any child) up through
    // parent() looking for the first View that actually accepts
    // dataObject - mirrors RootView::canPerformCommand()'s own chain-walk
    // (command.h) for the same "let an ancestor answer on behalf of an
    // uninterested descendant" reasoning.
    View* findDropTargetView(RootView& rootView, const Point& clientPt, IDataObject* dataObject) {
        Point localPt;
        SubView* hit = rootView.hitTestChildren(clientPt, localPt);
        View* start = (hit != nullptr) ? static_cast<View*>(hit) : static_cast<View*>(&rootView);
        for (View* v = start; v != nullptr; v = v->parent()) {
            if (viewAcceptsOffered(v, dataObject)) {
                return v;
            }
        }
        return nullptr;
    }

    // Only ever proposes DROPEFFECT_COPY - and only if some View in the
    // hit-tested chain accepts, and the source allowed COPY in the first
    // place (the incoming *effect on every IDropTarget call carries the
    // source's own allowed-effects mask).
    void negotiateEffect(bool accepted, DWORD* effect) {
        if (effect == nullptr) {
            return;
        }
        bool canCopy = accepted && (*effect & DROPEFFECT_COPY) != 0;
        *effect = canCopy ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    }

    Point toClientPoint(RootView& rootView, POINTL screenPt) {
        POINT pt{ screenPt.x, screenPt.y };
        ::ScreenToClient(rootView.windowHandle(), &pt);
        return Point(static_cast<float>(pt.x), static_cast<float>(pt.y));
    }

}

COMDropTarget::COMDropTarget(RootView& rootView) : rootView_(rootView) {
    ::CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dropTargetHelper_));
}

HRESULT COMDropTarget::dragEnterAt(IDataObject* dataObject, const Point& clientPt, DWORD* effect) {
    currentDataObject_ = dataObject;
    View* target = findDropTargetView(rootView_, clientPt, dataObject);
    negotiateEffect(target != nullptr, effect);
    return S_OK;
}

HRESULT COMDropTarget::dragOverAt(const Point& clientPt, DWORD* effect) {
    View* target = findDropTargetView(rootView_, clientPt, currentDataObject_.Get());
    negotiateEffect(target != nullptr, effect);
    return S_OK;
}

HRESULT COMDropTarget::dropAt(IDataObject* dataObject, const Point& clientPt, DWORD* effect) {
    View* target = findDropTargetView(rootView_, clientPt, dataObject);
    negotiateEffect(target != nullptr, effect);

    if (target != nullptr && effect != nullptr && *effect != DROPEFFECT_NONE) {
        DropTarget* dropTarget = target->dropTarget();
        std::vector<std::wstring> paths;
        BLImage image;
        std::wstring text;
        if (!dropTarget->onFilesDropped.empty() && extractFilePaths(dataObject, paths)) {
            dropTarget->onFilesDropped(*dropTarget, paths);
        } else if (!dropTarget->onImageDropped.empty() && extractDibImage(dataObject, image)) {
            dropTarget->onImageDropped(*dropTarget, image);
        } else if (!dropTarget->onTextDropped.empty() && extractUnicodeText(dataObject, text)) {
            dropTarget->onTextDropped(*dropTarget, text);
        }
    }

    currentDataObject_.Reset();
    return S_OK;
}

STDMETHODIMP COMDropTarget::DragEnter(IDataObject* dataObject, DWORD, POINTL pt, DWORD* effect) {
    HRESULT hr = dragEnterAt(dataObject, toClientPoint(rootView_, pt), effect);

    if (dropTargetHelper_) {
        POINT screenPt{ pt.x, pt.y };
        dropTargetHelper_->DragEnter(rootView_.windowHandle(), dataObject, &screenPt, effect != nullptr ? *effect : DROPEFFECT_NONE);
    }
    return hr;
}

STDMETHODIMP COMDropTarget::DragOver(DWORD, POINTL pt, DWORD* effect) {
    HRESULT hr = dragOverAt(toClientPoint(rootView_, pt), effect);

    if (dropTargetHelper_) {
        POINT screenPt{ pt.x, pt.y };
        dropTargetHelper_->DragOver(&screenPt, effect != nullptr ? *effect : DROPEFFECT_NONE);
    }
    return hr;
}

STDMETHODIMP COMDropTarget::DragLeave() {
    if (dropTargetHelper_) {
        dropTargetHelper_->DragLeave();
    }
    currentDataObject_.Reset();
    return S_OK;
}

STDMETHODIMP COMDropTarget::Drop(IDataObject* dataObject, DWORD, POINTL pt, DWORD* effect) {
    HRESULT hr = dropAt(dataObject, toClientPoint(rootView_, pt), effect);

    if (dropTargetHelper_) {
        POINT screenPt{ pt.x, pt.y };
        dropTargetHelper_->Drop(dataObject, &screenPt, effect != nullptr ? *effect : DROPEFFECT_NONE);
    }
    return hr;
}

}
