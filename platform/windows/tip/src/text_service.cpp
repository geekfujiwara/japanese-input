#include "text_service.h"

#include "module.h"

#include <new>

namespace astelio::tip {

HRESULT TextService::Create(REFIID riid, void** object)
{
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    auto* service = new (std::nothrow) TextService();
    if (service == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = service->QueryInterface(riid, object);
    service->Release();
    return hr;
}

TextService::TextService()
{
    AddModuleRef();
}

TextService::~TextService()
{
    ReleaseModuleRef();
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** object)
{
    if (object == nullptr) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx) {
        *object = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *object = static_cast<ITfKeyEventSink*>(this);
    } else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

STDMETHODIMP_(ULONG) TextService::Release()
{
    const LONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) {
        delete this;
    }
    return static_cast<ULONG>(count);
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* thread_mgr, TfClientId client_id)
{
    return ActivateEx(thread_mgr, client_id, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id, DWORD /*flags*/)
{
    if (thread_mgr == nullptr) {
        return E_INVALIDARG;
    }
    thread_mgr_ = thread_mgr;
    client_id_ = client_id;

    Microsoft::WRL::ComPtr<ITfKeystrokeMgr> keystroke_mgr;
    HRESULT hr = thread_mgr_.As(&keystroke_mgr);
    if (SUCCEEDED(hr)) {
        hr = keystroke_mgr->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE);
    }
    if (FAILED(hr)) {
        Deactivate();
        return hr;
    }
    key_sink_advised_ = true;
    return S_OK;
}

STDMETHODIMP TextService::Deactivate()
{
    if (key_sink_advised_ && thread_mgr_) {
        Microsoft::WRL::ComPtr<ITfKeystrokeMgr> keystroke_mgr;
        if (SUCCEEDED(thread_mgr_.As(&keystroke_mgr))) {
            keystroke_mgr->UnadviseKeyEventSink(client_id_);
        }
    }
    key_sink_advised_ = false;
    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(BOOL /*foreground*/)
{
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* /*context*/, WPARAM /*wparam*/, LPARAM /*lparam*/, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* /*context*/, WPARAM /*wparam*/, LPARAM /*lparam*/, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* /*context*/, WPARAM /*wparam*/, LPARAM /*lparam*/, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* /*context*/, WPARAM /*wparam*/, LPARAM /*lparam*/, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* /*context*/, REFGUID /*guid*/, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    return S_OK;
}

} // namespace astelio::tip
