#include "display_attributes.h"

#include "astelio/tip/guids.h"
#include "module.h"

#include <oleauto.h>

#include <new>

namespace astelio::tip {
namespace {

struct AttributeStyle {
    const GUID* guid;
    const wchar_t* description;
    TF_DISPLAYATTRIBUTE style;
};

TF_DISPLAYATTRIBUTE Style(TF_DA_LINESTYLE line, BOOL bold, TF_DA_ATTR_INFO info)
{
    TF_DISPLAYATTRIBUTE style{};
    style.crText.type = TF_CT_NONE;
    style.crBk.type = TF_CT_NONE;
    style.lsStyle = line;
    style.fBoldLine = bold;
    style.crLine.type = TF_CT_NONE;
    style.bAttr = info;
    return style;
}

const AttributeStyle& StyleOf(int index)
{
    static const AttributeStyle kStyles[kDisplayAttributeCount] = {
        {&kInputAttributeGuid, L"Astelio input", Style(TF_LS_DOT, FALSE, TF_ATTR_INPUT)},
        {&kConvertedAttributeGuid, L"Astelio converted", Style(TF_LS_SOLID, FALSE, TF_ATTR_CONVERTED)},
        {&kFocusedAttributeGuid, L"Astelio focused", Style(TF_LS_SOLID, TRUE, TF_ATTR_TARGET_CONVERTED)},
    };
    return kStyles[index];
}

class AttributeInfo final : public ITfDisplayAttributeInfo {
public:
    explicit AttributeInfo(int index) : index_(index) { AddModuleRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
            *object = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_count_)); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    STDMETHODIMP GetGUID(GUID* guid) override
    {
        if (guid == nullptr) {
            return E_INVALIDARG;
        }
        *guid = *StyleOf(index_).guid;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR* description) override
    {
        if (description == nullptr) {
            return E_INVALIDARG;
        }
        *description = SysAllocString(StyleOf(index_).description);
        return *description != nullptr ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attribute) override
    {
        if (attribute == nullptr) {
            return E_INVALIDARG;
        }
        *attribute = StyleOf(index_).style;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* /*attribute*/) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return S_OK; }

private:
    ~AttributeInfo() { ReleaseModuleRef(); }

    LONG ref_count_ = 1;
    int index_;
};

class AttributeEnum final : public IEnumTfDisplayAttributeInfo {
public:
    explicit AttributeEnum(int position = 0) : position_(position) { AddModuleRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
            *object = static_cast<IEnumTfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_count_)); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** clone) override
    {
        if (clone == nullptr) {
            return E_INVALIDARG;
        }
        *clone = new (std::nothrow) AttributeEnum(position_);
        return *clone != nullptr ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** attributes, ULONG* fetched) override
    {
        if (attributes == nullptr || (count > 1 && fetched == nullptr)) {
            return E_INVALIDARG;
        }
        ULONG done = 0;
        while (done < count && position_ < kDisplayAttributeCount) {
            attributes[done] = new (std::nothrow) AttributeInfo(position_);
            if (attributes[done] == nullptr) {
                break;
            }
            ++done;
            ++position_;
        }
        if (fetched != nullptr) {
            *fetched = done;
        }
        return done == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override
    {
        position_ = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG count) override
    {
        const ULONG left = static_cast<ULONG>(kDisplayAttributeCount - position_);
        position_ += static_cast<int>(count < left ? count : left);
        return count <= left ? S_OK : S_FALSE;
    }

private:
    ~AttributeEnum() { ReleaseModuleRef(); }

    LONG ref_count_ = 1;
    int position_;
};

} // namespace

const GUID& DisplayAttributeGuid(DisplayAttribute attribute)
{
    return *StyleOf(static_cast<int>(attribute)).guid;
}

HRESULT EnumDisplayAttributes(IEnumTfDisplayAttributeInfo** attributes)
{
    if (attributes == nullptr) {
        return E_INVALIDARG;
    }
    *attributes = new (std::nothrow) AttributeEnum();
    return *attributes != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT GetDisplayAttribute(REFGUID guid, ITfDisplayAttributeInfo** attribute)
{
    if (attribute == nullptr) {
        return E_INVALIDARG;
    }
    *attribute = nullptr;
    for (int index = 0; index < kDisplayAttributeCount; ++index) {
        if (IsEqualGUID(guid, *StyleOf(index).guid)) {
            *attribute = new (std::nothrow) AttributeInfo(index);
            return *attribute != nullptr ? S_OK : E_OUTOFMEMORY;
        }
    }
    return E_INVALIDARG;
}

} // namespace astelio::tip
