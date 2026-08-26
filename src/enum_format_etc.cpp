#include "newui/enum_format_etc.h"

namespace newui {

EnumFormatEtc::EnumFormatEtc(std::vector<FORMATETC> formats, std::size_t index)
    : formats_(std::move(formats))
    , index_(index) {
}

STDMETHODIMP EnumFormatEtc::Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) {
    // Per IEnumFORMATETC's documented contract, pceltFetched may only be
    // null when celt is exactly 1.
    if (rgelt == nullptr || (celt != 1 && pceltFetched == nullptr)) {
        return E_INVALIDARG;
    }

    ULONG fetched = 0;
    while (fetched < celt && index_ < formats_.size()) {
        rgelt[fetched] = formats_[index_];
        ++fetched;
        ++index_;
    }

    if (pceltFetched != nullptr) {
        *pceltFetched = fetched;
    }

    return fetched == celt ? S_OK : S_FALSE;
}

STDMETHODIMP EnumFormatEtc::Skip(ULONG celt) {
    std::size_t remaining = formats_.size() - index_;
    if (celt > remaining) {
        index_ = formats_.size();
        return S_FALSE;
    }

    index_ += celt;
    return S_OK;
}

STDMETHODIMP EnumFormatEtc::Reset() {
    index_ = 0;
    return S_OK;
}

STDMETHODIMP EnumFormatEtc::Clone(IEnumFORMATETC** ppenum) {
    if (ppenum == nullptr) {
        return E_POINTER;
    }

    auto clone = Microsoft::WRL::Make<EnumFormatEtc>(formats_, index_);
    if (!clone) {
        return E_OUTOFMEMORY;
    }

    return clone.CopyTo(ppenum);
}

}
