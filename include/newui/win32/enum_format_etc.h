#pragma once

#include <wrl/client.h>
#include <wrl/implements.h>

#include <objidl.h>

#include <cstddef>
#include <vector>

namespace newui {

// Standard OLE FORMATETC enumerator - IDataObject::EnumFormatEtc()
// implementations (TextDataObject, VirtualFileDataObject - see later
// phases) each hand back a fresh instance of this over whatever FORMATETC
// list they support, rather than reimplementing IEnumFORMATETC's Next/
// Skip/Reset/Clone contract themselves.
// @reflect ignore=true
class EnumFormatEtc final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IEnumFORMATETC> {
public:
    explicit EnumFormatEtc(std::vector<FORMATETC> formats, std::size_t index = 0);

    STDMETHODIMP Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override;
    STDMETHODIMP Skip(ULONG celt) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Clone(IEnumFORMATETC** ppenum) override;

private:
    std::vector<FORMATETC> formats_;
    std::size_t index_ = 0;
};

}
