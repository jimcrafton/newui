#include "newui/enum_format_etc.h"
#include "newui/dragndrop.h"
#include "newui/dibimage.h"
#include "newui/rootview.h"
#include "newui/subview.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <shlobj.h>

namespace {

std::vector<FORMATETC> makeMockFormats() {
    FORMATETC text = { CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    FORMATETC dib = { CF_DIB, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    FORMATETC hdrop = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return { text, dib, hdrop };
}

// A minimal IDataObject test double - hands back whichever payload
// (CF_HDROP/CF_UNICODETEXT/CF_DIB) it was told to carry via a fresh
// GHND-allocated HGLOBAL each GetData() call, exactly like a real incoming
// drop source would, so extractFilePaths/extractUnicodeText/extractDibImage
// (include/newui/dragndrop.h) get exercised against the real IDataObject
// contract rather than against their own internals directly.
class MockDataObject final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDataObject> {
public:
    void setFilePaths(const std::vector<std::wstring>& paths) {
        std::size_t pathBytes = sizeof(wchar_t);
        for (const auto& path : paths) {
            pathBytes += (path.size() + 1) * sizeof(wchar_t);
        }

        hdropBytes_.assign(sizeof(DROPFILES) + pathBytes, 0);
        auto* dropFiles = reinterpret_cast<DROPFILES*>(hdropBytes_.data());
        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;

        auto* dest = reinterpret_cast<wchar_t*>(hdropBytes_.data() + sizeof(DROPFILES));
        for (const auto& path : paths) {
            std::memcpy(dest, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
            dest += path.size() + 1;
        }
        hasFiles_ = true;
    }

    void setText(const std::wstring& text) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(text.c_str());
        textBytes_.assign(begin, begin + (text.size() + 1) * sizeof(wchar_t));
        hasText_ = true;
    }

    void setDibBytes(std::vector<std::uint8_t> bytes) {
        dibBytes_ = std::move(bytes);
        hasDib_ = true;
    }

    STDMETHODIMP GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override {
        if (pformatetcIn == nullptr || pmedium == nullptr) {
            return E_INVALIDARG;
        }

        const std::vector<std::uint8_t>* bytes = nullptr;
        switch (pformatetcIn->cfFormat) {
        case CF_HDROP: if (hasFiles_) { bytes = &hdropBytes_; } break;
        case CF_UNICODETEXT: if (hasText_) { bytes = &textBytes_; } break;
        case CF_DIB: if (hasDib_) { bytes = &dibBytes_; } break;
        default: break;
        }
        if (bytes == nullptr) {
            return DV_E_FORMATETC;
        }

        HGLOBAL handle = ::GlobalAlloc(GHND, bytes->size());
        if (handle == nullptr) {
            return E_OUTOFMEMORY;
        }
        void* dest = ::GlobalLock(handle);
        std::memcpy(dest, bytes->data(), bytes->size());
        ::GlobalUnlock(handle);

        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = handle;
        pmedium->pUnkForRelease = nullptr;
        return S_OK;
    }

    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }

    STDMETHODIMP QueryGetData(FORMATETC* pformatetc) override {
        if (pformatetc == nullptr) {
            return E_INVALIDARG;
        }
        switch (pformatetc->cfFormat) {
        case CF_HDROP: return hasFiles_ ? S_OK : DV_E_FORMATETC;
        case CF_UNICODETEXT: return hasText_ ? S_OK : DV_E_FORMATETC;
        case CF_DIB: return hasDib_ ? S_OK : DV_E_FORMATETC;
        default: return DV_E_FORMATETC;
        }
    }

    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP DUnadvise(DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }

private:
    std::vector<std::uint8_t> hdropBytes_;
    bool hasFiles_ = false;
    std::vector<std::uint8_t> textBytes_;
    bool hasText_ = false;
    std::vector<std::uint8_t> dibBytes_;
    bool hasDib_ = false;
};

}

TEST(EnumFormatEtc, NextFetchesOneElementAtATimeInOrder) {
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(makeMockFormats());

    FORMATETC fetched = {};
    ULONG count = 0;
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(fetched.cfFormat, CF_UNICODETEXT);

    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(fetched.cfFormat, CF_DIB);
}

TEST(EnumFormatEtc, NextFetchesMultipleElementsAndReportsExhaustion) {
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(makeMockFormats());

    FORMATETC fetched[4] = {};
    ULONG count = 0;
    EXPECT_EQ(enumerator->Next(4, fetched, &count), S_FALSE);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(fetched[0].cfFormat, CF_UNICODETEXT);
    EXPECT_EQ(fetched[1].cfFormat, CF_DIB);
    EXPECT_EQ(fetched[2].cfFormat, CF_HDROP);

    EXPECT_EQ(enumerator->Next(1, fetched, &count), S_FALSE);
    EXPECT_EQ(count, 0u);
}

TEST(EnumFormatEtc, NextRejectsNullOutputPointers) {
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(makeMockFormats());

    FORMATETC fetched = {};
    EXPECT_EQ(enumerator->Next(1, nullptr, nullptr), E_INVALIDARG);
    EXPECT_EQ(enumerator->Next(2, &fetched, nullptr), E_INVALIDARG);
}

TEST(EnumFormatEtc, SkipAdvancesAndReportsWhenPastTheEnd) {
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(makeMockFormats());

    EXPECT_EQ(enumerator->Skip(2), S_OK);

    FORMATETC fetched = {};
    ULONG count = 0;
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(fetched.cfFormat, CF_HDROP);

    enumerator->Reset();
    EXPECT_EQ(enumerator->Skip(10), S_FALSE);
    EXPECT_EQ(enumerator->Next(1, &fetched, &count), S_FALSE);
}

TEST(EnumFormatEtc, ResetReturnsToTheFirstElement) {
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(makeMockFormats());

    FORMATETC fetched = {};
    ULONG count = 0;
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);

    ASSERT_EQ(enumerator->Reset(), S_OK);
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(fetched.cfFormat, CF_UNICODETEXT);
}

TEST(EnumFormatEtc, CloneCopiesTheCurrentPositionIndependently) {
    auto enumerator = Microsoft::WRL::Make<newui::EnumFormatEtc>(makeMockFormats());

    FORMATETC fetched = {};
    ULONG count = 0;
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);

    Microsoft::WRL::ComPtr<IEnumFORMATETC> clone;
    ASSERT_EQ(enumerator->Clone(&clone), S_OK);

    // The clone starts wherever the original was positioned (CF_DIB next),
    // and advancing it must not affect the original's own position.
    ASSERT_EQ(clone->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(fetched.cfFormat, CF_DIB);

    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(fetched.cfFormat, CF_DIB);
}

TEST(DragDropExtraction, ExtractFilePathsReturnsAllDroppedPaths) {
    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setFilePaths({ L"C:\\fake\\a.txt", L"C:\\fake\\b.txt" });

    std::vector<std::wstring> paths;
    ASSERT_TRUE(newui::extractFilePaths(dataObject.Get(), paths));
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], L"C:\\fake\\a.txt");
    EXPECT_EQ(paths[1], L"C:\\fake\\b.txt");
}

TEST(DragDropExtraction, ExtractFilePathsFailsWhenFormatNotOffered) {
    auto dataObject = Microsoft::WRL::Make<MockDataObject>();

    std::vector<std::wstring> paths;
    EXPECT_FALSE(newui::extractFilePaths(dataObject.Get(), paths));
}

TEST(DragDropExtraction, ExtractFilePathsFailsOnNullDataObject) {
    std::vector<std::wstring> paths;
    EXPECT_FALSE(newui::extractFilePaths(nullptr, paths));
}

TEST(DragDropExtraction, ExtractUnicodeTextReturnsTheDroppedString) {
    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"Hello, drop!");

    std::wstring text;
    ASSERT_TRUE(newui::extractUnicodeText(dataObject.Get(), text));
    EXPECT_EQ(text, L"Hello, drop!");
}

TEST(DragDropExtraction, ExtractUnicodeTextFailsWhenFormatNotOffered) {
    auto dataObject = Microsoft::WRL::Make<MockDataObject>();

    std::wstring text;
    EXPECT_FALSE(newui::extractUnicodeText(dataObject.Get(), text));
}

TEST(DragDropExtraction, ExtractDibImageDecodesARealBLImage) {
    BLImage source(2, 2, BL_FORMAT_PRGB32);
    {
        BLContext ctx(source);
        ctx.set_fill_style(BLRgba32(0xFF3366CC));
        ctx.fill_all();
        ctx.end();
    }

    std::vector<std::uint8_t> dibBytes;
    ASSERT_TRUE(newui::imageToDibBytes(source, dibBytes));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setDibBytes(dibBytes);

    BLImage decoded;
    ASSERT_TRUE(newui::extractDibImage(dataObject.Get(), decoded));
    EXPECT_EQ(decoded.size().w, 2);
    EXPECT_EQ(decoded.size().h, 2);
}

TEST(DragDropExtraction, ExtractDibImageFailsWhenFormatNotOffered) {
    auto dataObject = Microsoft::WRL::Make<MockDataObject>();

    BLImage decoded;
    EXPECT_FALSE(newui::extractDibImage(dataObject.Get(), decoded));
}

TEST(TextDataObject, QueryGetDataAcceptsUnicodeTextHglobal) {
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(std::wstring(L"Hello, drag!"));

    FORMATETC formatEtc{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    EXPECT_EQ(dataObject->QueryGetData(&formatEtc), S_OK);
}

TEST(TextDataObject, QueryGetDataRejectsOtherFormats) {
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(std::wstring(L"Hello, drag!"));

    FORMATETC formatEtc{ CF_DIB, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    EXPECT_EQ(dataObject->QueryGetData(&formatEtc), DV_E_FORMATETC);
}

TEST(TextDataObject, GetDataHandsBackANullTerminatedCopyOfTheText) {
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(std::wstring(L"Hello, drag!"));

    FORMATETC formatEtc{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    ASSERT_EQ(dataObject->GetData(&formatEtc, &medium), S_OK);
    EXPECT_EQ(medium.tymed, static_cast<DWORD>(TYMED_HGLOBAL));

    void* locked = ::GlobalLock(medium.hGlobal);
    ASSERT_NE(locked, nullptr);
    EXPECT_STREQ(static_cast<const wchar_t*>(locked), L"Hello, drag!");
    ::GlobalUnlock(medium.hGlobal);
    ::ReleaseStgMedium(&medium);
}

TEST(TextDataObject, GetDataRejectsAnUnsupportedFormat) {
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(std::wstring(L"Hello, drag!"));

    FORMATETC formatEtc{ CF_DIB, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    EXPECT_EQ(dataObject->GetData(&formatEtc, &medium), DV_E_FORMATETC);
}

TEST(TextDataObject, EnumFormatEtcListsOnlyUnicodeTextForGet) {
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(std::wstring(L"Hello, drag!"));

    Microsoft::WRL::ComPtr<IEnumFORMATETC> enumerator;
    ASSERT_EQ(dataObject->EnumFormatEtc(DATADIR_GET, &enumerator), S_OK);

    FORMATETC fetched = {};
    ULONG count = 0;
    ASSERT_EQ(enumerator->Next(1, &fetched, &count), S_OK);
    EXPECT_EQ(fetched.cfFormat, CF_UNICODETEXT);
    EXPECT_EQ(enumerator->Next(1, &fetched, &count), S_FALSE);
}

TEST(TextDataObject, EnumFormatEtcRefusesSetDirection) {
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(std::wstring(L"Hello, drag!"));

    Microsoft::WRL::ComPtr<IEnumFORMATETC> enumerator;
    EXPECT_EQ(dataObject->EnumFormatEtc(DATADIR_SET, &enumerator), E_NOTIMPL);
}

TEST(VirtualFileDataObject, QueryGetDataAcceptsFileDescriptorHglobal) {
    std::vector<newui::VirtualFile> files{ { L"a.txt", { 'h', 'i' } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(std::move(files));

    FORMATETC formatEtc{ dataObject->fileDescriptorFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    EXPECT_EQ(dataObject->QueryGetData(&formatEtc), S_OK);
}

TEST(VirtualFileDataObject, QueryGetDataAcceptsFileContentsWithAValidLindex) {
    std::vector<newui::VirtualFile> files{ { L"a.txt", { 'h', 'i' } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(std::move(files));

    FORMATETC formatEtc{ dataObject->fileContentsFormat(), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM };
    EXPECT_EQ(dataObject->QueryGetData(&formatEtc), S_OK);
}

TEST(VirtualFileDataObject, QueryGetDataRejectsFileContentsWithAnOutOfRangeLindex) {
    std::vector<newui::VirtualFile> files{ { L"a.txt", { 'h', 'i' } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(std::move(files));

    FORMATETC formatEtc{ dataObject->fileContentsFormat(), nullptr, DVASPECT_CONTENT, 5, TYMED_ISTREAM };
    EXPECT_EQ(dataObject->QueryGetData(&formatEtc), DV_E_LINDEX);
}

TEST(VirtualFileDataObject, QueryGetDataRejectsAnUnrelatedFormat) {
    std::vector<newui::VirtualFile> files{ { L"a.txt", { 'h', 'i' } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(std::move(files));

    FORMATETC formatEtc{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    EXPECT_EQ(dataObject->QueryGetData(&formatEtc), DV_E_FORMATETC);
}

TEST(VirtualFileDataObject, GetDataDescriptorListsEveryFilesNameAndSize) {
    std::vector<newui::VirtualFile> files{
        { L"first.txt", { 'a', 'b', 'c' } },
        { L"second.bin", { 1, 2, 3, 4, 5 } },
    };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(files);

    FORMATETC formatEtc{ dataObject->fileDescriptorFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    ASSERT_EQ(dataObject->GetData(&formatEtc, &medium), S_OK);

    auto* descriptor = static_cast<FILEGROUPDESCRIPTORW*>(::GlobalLock(medium.hGlobal));
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->cItems, 2u);
    EXPECT_STREQ(descriptor->fgd[0].cFileName, L"first.txt");
    EXPECT_EQ(descriptor->fgd[0].nFileSizeLow, 3u);
    EXPECT_TRUE((descriptor->fgd[0].dwFlags & FD_FILESIZE) != 0);
    EXPECT_TRUE((descriptor->fgd[0].dwFlags & FD_PROGRESSUI) != 0);
    EXPECT_STREQ(descriptor->fgd[1].cFileName, L"second.bin");
    EXPECT_EQ(descriptor->fgd[1].nFileSizeLow, 5u);
    ::GlobalUnlock(medium.hGlobal);
    ::ReleaseStgMedium(&medium);
}

TEST(VirtualFileDataObject, GetDataContentsHandsBackTheExactBytesAsAStream) {
    std::vector<newui::VirtualFile> files{ { L"a.bin", { 10, 20, 30, 40 } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(files);

    FORMATETC formatEtc{ dataObject->fileContentsFormat(), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM };
    STGMEDIUM medium{};
    ASSERT_EQ(dataObject->GetData(&formatEtc, &medium), S_OK);
    ASSERT_EQ(medium.tymed, static_cast<DWORD>(TYMED_ISTREAM));
    ASSERT_NE(medium.pstm, nullptr);

    std::uint8_t buffer[4] = {};
    ULONG bytesRead = 0;
    ASSERT_EQ(medium.pstm->Read(buffer, sizeof(buffer), &bytesRead), S_OK);
    EXPECT_EQ(bytesRead, 4u);
    EXPECT_EQ(std::vector<std::uint8_t>(buffer, buffer + 4), files[0].contents);

    ::ReleaseStgMedium(&medium);
}

TEST(VirtualFileDataObject, GetDataRejectsAnUnrelatedFormat) {
    std::vector<newui::VirtualFile> files{ { L"a.txt", { 'h', 'i' } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(std::move(files));

    FORMATETC formatEtc{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    EXPECT_EQ(dataObject->GetData(&formatEtc, &medium), DV_E_FORMATETC);
}

TEST(VirtualFileDataObject, EnumFormatEtcListsBothFormats) {
    std::vector<newui::VirtualFile> files{ { L"a.txt", { 'h', 'i' } } };
    auto dataObject = Microsoft::WRL::Make<newui::VirtualFileDataObject>(std::move(files));

    Microsoft::WRL::ComPtr<IEnumFORMATETC> enumerator;
    ASSERT_EQ(dataObject->EnumFormatEtc(DATADIR_GET, &enumerator), S_OK);

    FORMATETC fetched[2] = {};
    ULONG count = 0;
    ASSERT_EQ(enumerator->Next(2, fetched, &count), S_OK);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(fetched[0].cfFormat, dataObject->fileDescriptorFormat());
    EXPECT_EQ(fetched[1].cfFormat, dataObject->fileContentsFormat());
}

TEST(COMDropSource, QueryContinueDragCancelsOnEscape) {
    auto dropSource = Microsoft::WRL::Make<newui::COMDropSource>();
    EXPECT_EQ(dropSource->QueryContinueDrag(TRUE, MK_LBUTTON), DRAGDROP_S_CANCEL);
}

TEST(COMDropSource, QueryContinueDragDropsWhenTheLeftButtonIsReleased) {
    auto dropSource = Microsoft::WRL::Make<newui::COMDropSource>();
    EXPECT_EQ(dropSource->QueryContinueDrag(FALSE, 0), DRAGDROP_S_DROP);
}

TEST(COMDropSource, QueryContinueDragContinuesWhileTheLeftButtonIsHeld) {
    auto dropSource = Microsoft::WRL::Make<newui::COMDropSource>();
    EXPECT_EQ(dropSource->QueryContinueDrag(FALSE, MK_LBUTTON), S_OK);
}

TEST(COMDropSource, GiveFeedbackAlwaysRequestsDefaultCursors) {
    auto dropSource = Microsoft::WRL::Make<newui::COMDropSource>();
    EXPECT_EQ(dropSource->GiveFeedback(DROPEFFECT_COPY), DRAGDROP_S_USEDEFAULTCURSORS);
}

namespace {

// Headless RootView + one child SubView at a known bounds, matching
// TestableRootView's own established pattern (test_rootview.cpp) - none
// of COMDropTarget's *At() methods touch viewHwnd_ (only the raw COM
// DragEnter/DragOver/Drop entry points do, for ScreenToClient()), so a
// RootView constructed with a null Frame* and never initialize()'d is
// enough to exercise the real per-View hit-testing/negotiation logic.
struct DropTargetFixture {
    newui::RootView* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    newui::SubView* child = new newui::SubView();

    DropTargetFixture() {
        child->setVisible(true);
        child->setBounds(newui::Rect(10, 10, 100, 100));
        root->addChild(child);
    }

    ~DropTargetFixture() {
        root->destroy();
        delete root;
    }
};

}

TEST(COMDropTarget, DragEnterAtNegotiatesCopyWhenTheHitViewAccepts) {
    DropTargetFixture fixture;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onTextDropped.add([](newui::DropTarget&, const std::wstring&) { return newui::SyncReturn::Handled; });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    EXPECT_EQ(comDropTarget->dragEnterAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_COPY));
}

TEST(COMDropTarget, DragEnterAtRefusesWhenNothingInTheChainAccepts) {
    DropTargetFixture fixture;

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dragEnterAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_NONE));
}

TEST(COMDropTarget, DragEnterAtRefusesWhenTheSourceDidNotAllowCopy) {
    DropTargetFixture fixture;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onTextDropped.add([](newui::DropTarget&, const std::wstring&) { return newui::SyncReturn::Handled; });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_MOVE;  // source never offered COPY
    EXPECT_EQ(comDropTarget->dragEnterAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_NONE));
}

TEST(COMDropTarget, DragEnterAtWalksUpToAnAncestorThatAccepts) {
    DropTargetFixture fixture;
    // child itself has no DropTarget - only fixture.root does, so a hit
    // over child has to walk up through parent() to find it.
    auto rootDropTarget = std::make_unique<newui::DropTarget>();
    rootDropTarget->onTextDropped.add([](newui::DropTarget&, const std::wstring&) { return newui::SyncReturn::Handled; });
    fixture.root->setDropTarget(std::move(rootDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dragEnterAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_COPY));
}

TEST(COMDropTarget, DragOverAtReusesTheDataObjectRememberedFromDragEnterAt) {
    DropTargetFixture fixture;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onTextDropped.add([](newui::DropTarget&, const std::wstring&) { return newui::SyncReturn::Handled; });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    ASSERT_EQ(comDropTarget->dragEnterAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);

    effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dragOverAt(newui::Point(55, 55), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_COPY));
}

TEST(COMDropTarget, DragLeaveClearsTheRememberedDataObject) {
    DropTargetFixture fixture;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onTextDropped.add([](newui::DropTarget&, const std::wstring&) { return newui::SyncReturn::Handled; });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    ASSERT_EQ(comDropTarget->dragEnterAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    ASSERT_EQ(comDropTarget->DragLeave(), S_OK);

    effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dragOverAt(newui::Point(55, 55), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_NONE));
}

TEST(COMDropTarget, DropAtDispatchesFilesWhenCfHdropIsOffered) {
    DropTargetFixture fixture;
    std::vector<std::wstring> received;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onFilesDropped.add([&received](newui::DropTarget&, const std::vector<std::wstring>& paths) {
        received = paths;
        return newui::SyncReturn::Handled;
    });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setFilePaths({ L"C:\\fake\\a.txt" });

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dropAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], L"C:\\fake\\a.txt");
}

TEST(COMDropTarget, DropAtPrefersFilesOverTextWhenBothAreOffered) {
    DropTargetFixture fixture;
    bool filesCalled = false;
    bool textCalled = false;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onFilesDropped.add([&filesCalled](newui::DropTarget&, const std::vector<std::wstring>&) {
        filesCalled = true;
        return newui::SyncReturn::Handled;
    });
    childDropTarget->onTextDropped.add([&textCalled](newui::DropTarget&, const std::wstring&) {
        textCalled = true;
        return newui::SyncReturn::Handled;
    });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setFilePaths({ L"C:\\fake\\a.txt" });
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dropAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_TRUE(filesCalled);
    EXPECT_FALSE(textCalled);
}

TEST(COMDropTarget, DropAtFallsThroughToTextWhenFilesAreNotRegistered) {
    DropTargetFixture fixture;
    std::wstring received;
    auto childDropTarget = std::make_unique<newui::DropTarget>();
    childDropTarget->onTextDropped.add([&received](newui::DropTarget&, const std::wstring& text) {
        received = text;
        return newui::SyncReturn::Handled;
    });
    fixture.child->setDropTarget(std::move(childDropTarget));

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dropAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_EQ(received, L"hello");
}

TEST(COMDropTarget, DropAtDispatchesNothingWhenNothingInTheChainAccepts) {
    DropTargetFixture fixture;

    auto dataObject = Microsoft::WRL::Make<MockDataObject>();
    dataObject->setText(L"hello");

    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*fixture.root);

    DWORD effect = DROPEFFECT_COPY;
    EXPECT_EQ(comDropTarget->dropAt(dataObject.Get(), newui::Point(50, 50), &effect), S_OK);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_NONE));
}
