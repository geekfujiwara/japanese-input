#include "text_service.h"

#include "key_translation.h"
#include "module.h"

#include <new>
#include <optional>
#include <utility>

namespace astelio::tip {
namespace {

using Microsoft::WRL::ComPtr;

const WCHAR* Wide(const std::u16string& text)
{
    return reinterpret_cast<const WCHAR*>(text.c_str());
}

LONG Length(const std::u16string& text)
{
    return static_cast<LONG>(text.size());
}

Modifiers CurrentModifiers()
{
    Modifiers modifiers;
    modifiers.shift = GetKeyState(VK_SHIFT) < 0;
    modifiers.control = GetKeyState(VK_CONTROL) < 0;
    modifiers.alt = GetKeyState(VK_MENU) < 0;
    modifiers.windows = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
    modifiers.caps_lock = (GetKeyState(VK_CAPITAL) & 1) != 0;
    return modifiers;
}

std::optional<KeyEvent> Translate(WPARAM wparam, LPARAM lparam)
{
    return TranslateKey(static_cast<std::uint32_t>(wparam), static_cast<std::uint32_t>(lparam), CurrentModifiers());
}

HRESULT SetCaret(TfEditCookie cookie, ITfContext* context, ITfRange* range)
{
    TF_SELECTION selection{};
    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    return context->SetSelection(cookie, 1, &selection);
}

HRESULT CollapseSelectionToEnd(TfEditCookie cookie, ITfContext* context, ITfRange* range)
{
    ComPtr<ITfRange> caret;
    HRESULT hr = range->Clone(&caret);
    if (SUCCEEDED(hr)) {
        hr = caret->Collapse(cookie, TF_ANCHOR_END);
    }
    if (SUCCEEDED(hr)) {
        hr = SetCaret(cookie, context, caret.Get());
    }
    return hr;
}

class EditSession final : public ITfEditSession {
public:
    EditSession(TextService* service, ITfContext* context, std::u16string commit)
        : service_(service), context_(context), commit_(std::move(commit))
    {
        service_->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *object = static_cast<ITfEditSession*>(this);
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

    STDMETHODIMP DoEditSession(TfEditCookie cookie) override
    {
        try {
            return service_->ApplyToDocument(cookie, context_.Get(), commit_);
        } catch (...) {
            return E_UNEXPECTED;
        }
    }

private:
    ~EditSession() { service_->Release(); }

    LONG ref_count_ = 1;
    TextService* service_;
    ComPtr<ITfContext> context_;
    std::u16string commit_;
};

} // namespace

HRESULT TextService::Create(REFIID riid, void** object)
{
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    TextService* service = nullptr;
    try {
        service = new (std::nothrow) TextService();
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    if (service == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = service->QueryInterface(riid, object);
    service->Release();
    return hr;
}

TextService::TextService() : session_(RomajiTable::Default(), CharacterSettings{})
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
    } else if (riid == IID_ITfCompositionSink) {
        *object = static_cast<ITfCompositionSink*>(this);
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
    composition_.Reset();
    session_.AbandonComposition();
    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(BOOL /*foreground*/)
{
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    if (context == nullptr) {
        return S_OK;
    }
    try {
        const std::optional<KeyEvent> key = Translate(wparam, lparam);
        *eaten = key && session_.WillHandle(*key) ? TRUE : FALSE;
    } catch (...) {
        *eaten = FALSE;
    }
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

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    if (context == nullptr) {
        return S_OK;
    }
    try {
        const std::optional<KeyEvent> key = Translate(wparam, lparam);
        if (!key || !session_.WillHandle(*key)) {
            return S_OK;
        }
        *eaten = TRUE;
        SessionOutput output = session_.Handle(*key);
        if (output.composition_changed || !output.commit.empty()) {
            return RequestEdit(context, std::move(output.commit));
        }
        return S_OK;
    } catch (...) {
        return E_UNEXPECTED;
    }
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

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie /*cookie*/, ITfComposition* /*composition*/)
{
    composition_.Reset();
    session_.AbandonComposition();
    return S_OK;
}

HRESULT TextService::RequestEdit(ITfContext* context, std::u16string commit)
{
    auto* edit = new (std::nothrow) EditSession(this, context, std::move(commit));
    if (edit == nullptr) {
        return E_OUTOFMEMORY;
    }
    HRESULT session_result = S_OK;
    const HRESULT hr = context->RequestEditSession(client_id_, edit, TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    edit->Release();
    return FAILED(hr) ? hr : session_result;
}

HRESULT TextService::StartComposition(TfEditCookie cookie, ITfContext* context)
{
    ComPtr<ITfInsertAtSelection> insert;
    HRESULT hr = context->QueryInterface(IID_PPV_ARGS(&insert));
    ComPtr<ITfRange> range;
    if (SUCCEEDED(hr)) {
        hr = insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, nullptr, 0, &range);
    }
    ComPtr<ITfContextComposition> compositions;
    if (SUCCEEDED(hr)) {
        hr = context->QueryInterface(IID_PPV_ARGS(&compositions));
    }
    if (SUCCEEDED(hr)) {
        hr = compositions->StartComposition(cookie, range.Get(), static_cast<ITfCompositionSink*>(this),
                                            composition_.ReleaseAndGetAddressOf());
    }
    return SUCCEEDED(hr) && !composition_ ? E_FAIL : hr;
}

HRESULT TextService::EndComposition(TfEditCookie cookie)
{
    if (!composition_) {
        return S_OK;
    }
    const HRESULT hr = composition_->EndComposition(cookie);
    composition_.Reset();
    return hr;
}

HRESULT TextService::ApplyToDocument(TfEditCookie cookie, ITfContext* context, const std::u16string& commit)
{
    HRESULT hr = S_OK;
    if (!commit.empty()) {
        ComPtr<ITfRange> range;
        if (composition_) {
            hr = composition_->GetRange(&range);
            if (SUCCEEDED(hr)) {
                hr = range->SetText(cookie, 0, Wide(commit), Length(commit));
            }
            if (SUCCEEDED(hr)) {
                hr = CollapseSelectionToEnd(cookie, context, range.Get());
            }
            const HRESULT ended = EndComposition(cookie);
            hr = FAILED(hr) ? hr : ended;
        } else {
            ComPtr<ITfInsertAtSelection> insert;
            hr = context->QueryInterface(IID_PPV_ARGS(&insert));
            if (SUCCEEDED(hr)) {
                hr = insert->InsertTextAtSelection(cookie, 0, Wide(commit), Length(commit), &range);
            }
            if (SUCCEEDED(hr)) {
                hr = CollapseSelectionToEnd(cookie, context, range.Get());
            }
        }
        if (FAILED(hr)) {
            return hr;
        }
    }

    if (!session_.Composing()) {
        if (composition_) {
            ComPtr<ITfRange> range;
            if (SUCCEEDED(composition_->GetRange(&range))) {
                range->SetText(cookie, 0, L"", 0);
            }
            return EndComposition(cookie);
        }
        return S_OK;
    }

    if (!composition_) {
        hr = StartComposition(cookie, context);
        if (FAILED(hr)) {
            return hr;
        }
    }
    const std::u16string text = session_.CompositionText();
    ComPtr<ITfRange> range;
    hr = composition_->GetRange(&range);
    if (SUCCEEDED(hr)) {
        hr = range->SetText(cookie, 0, Wide(text), Length(text));
    }
    ComPtr<ITfRange> caret;
    if (SUCCEEDED(hr)) {
        hr = range->Clone(&caret);
    }
    if (SUCCEEDED(hr)) {
        hr = caret->Collapse(cookie, TF_ANCHOR_START);
    }
    LONG shifted = 0;
    if (SUCCEEDED(hr)) {
        hr = caret->ShiftEnd(cookie, static_cast<LONG>(session_.CompositionCursor()), &shifted, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = caret->Collapse(cookie, TF_ANCHOR_END);
    }
    if (SUCCEEDED(hr)) {
        hr = SetCaret(cookie, context, caret.Get());
    }
    return hr;
}

} // namespace astelio::tip
