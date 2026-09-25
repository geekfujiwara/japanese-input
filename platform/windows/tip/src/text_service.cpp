#include "text_service.h"

#include "candidate_window.h"
#include "key_translation.h"
#include "dictionary_loader.h"
#include "lang_bar_button.h"
#include "module.h"

#include <oleauto.h>

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

std::optional<ModifierSide> AltSide(WPARAM wparam, LPARAM lparam)
{
    switch (wparam) {
    case VK_LMENU: return ModifierSide::Left;
    case VK_RMENU: return ModifierSide::Right;
    case VK_MENU: return (lparam & (1 << 24)) != 0 ? ModifierSide::Right : ModifierSide::Left;
    default: return std::nullopt;
    }
}

std::uint64_t Now()
{
    return GetTickCount64();
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

// The instance TSF activated on this thread (for the test entry point below).
thread_local TextService* g_active_service = nullptr;

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
    } else if (riid == IID_ITfCompartmentEventSink) {
        *object = static_cast<ITfCompartmentEventSink*>(this);
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
    g_active_service = this;
    session_.SetConverter(SharedConverter());
    try {
        StartModeIndicators();
    } catch (...) {
        // The indicator is optional; typing works without it.
    }
    return S_OK;
}

STDMETHODIMP TextService::Deactivate()
{
    if (g_active_service == this) {
        g_active_service = nullptr;
    }
    StopModeIndicators();
    candidate_window_.reset();
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
    ++Diagnostics().test_key_down;
    try {
        // Alt down always reaches the app so Alt shortcuts keep working; only the tap's release is eaten.
        if (const std::optional<ModifierSide> side = AltSide(wparam, lparam)) {
            alt_taps_.Press(*side, Now());
            return S_OK;
        }
        alt_taps_.MarkChordUsed();
        if (context == nullptr) {
            ++Diagnostics().null_context;
            return S_OK;
        }
        const std::optional<KeyEvent> key = Translate(wparam, lparam);
        *eaten = key && session_.WillHandle(*key) ? TRUE : FALSE;
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* /*context*/, WPARAM wparam, LPARAM lparam, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    try {
        if (const std::optional<ModifierSide> side = AltSide(wparam, lparam)) {
            if (alt_taps_.Release(*side, Now())) {
                pending_alt_tap_ = *side;
                *eaten = TRUE;
            }
        }
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    ++Diagnostics().key_down;
    try {
        if (const std::optional<ModifierSide> side = AltSide(wparam, lparam)) {
            alt_taps_.Press(*side, Now());
            return S_OK;
        }
        alt_taps_.MarkChordUsed();
        if (context == nullptr) {
            ++Diagnostics().null_context;
            return S_OK;
        }
        const std::optional<KeyEvent> key = Translate(wparam, lparam);
        if (!key || !session_.WillHandle(*key)) {
            return S_OK;
        }
        *eaten = TRUE;
        ++Diagnostics().eaten;
        SessionOutput output = session_.Handle(*key);
        if (output.composition_changed || !output.commit.empty()) {
            return RequestEdit(context, std::move(output.commit));
        }
        return S_OK;
    } catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    try {
        const std::optional<ModifierSide> side = AltSide(wparam, lparam);
        if (!side) {
            return S_OK;
        }
        bool tap = pending_alt_tap_ == side;
        pending_alt_tap_.reset();
        if (!tap) {
            tap = alt_taps_.Release(*side, Now());
        }
        if (!tap) {
            return S_OK;
        }
        // R-03: left Alt tap switches to English, right Alt tap to Japanese.
        *eaten = TRUE;
        return SetMode(*side == ModifierSide::Right, context);
    } catch (...) {
        return E_UNEXPECTED;
    }
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
    HideCandidateWindow();
    return S_OK;
}

// Another component (the taskbar, an app, the IME on/off key) changed the keyboard open state.
STDMETHODIMP TextService::OnChange(REFGUID compartment_guid)
{
    if (!IsEqualGUID(compartment_guid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        return S_OK;
    }
    try {
        const ComPtr<ITfCompartment> compartment = OpenCloseCompartment();
        if (!compartment) {
            return S_OK;
        }
        VARIANT value;
        VariantInit(&value);
        if (FAILED(compartment->GetValue(&value)) || value.vt != VT_I4) {
            VariantClear(&value);
            return S_OK;
        }
        const bool open = value.lVal != 0;
        if (open == JapaneseMode()) {
            return S_OK;
        }
        return SetMode(open, FocusedContext().Get());
    } catch (...) {
        return E_UNEXPECTED;
    }
}

HRESULT TextService::ToggleMode()
{
    try {
        return SetMode(!JapaneseMode(), FocusedContext().Get());
    } catch (...) {
        return E_UNEXPECTED;
    }
}

HRESULT TextService::SetMode(bool japanese, ITfContext* context)
{
    SessionOutput output = session_.SetJapaneseMode(japanese);
    HRESULT hr = S_OK;
    if (context != nullptr && (output.composition_changed || !output.commit.empty())) {
        hr = RequestEdit(context, std::move(output.commit));
    }
    if (!session_.CandidateListVisible()) {
        HideCandidateWindow();
    }
    PublishMode();
    return hr;
}

ComPtr<ITfContext> TextService::FocusedContext() const
{
    ComPtr<ITfContext> context;
    if (composition_) {
        ComPtr<ITfRange> range;
        if (SUCCEEDED(composition_->GetRange(&range)) && SUCCEEDED(range->GetContext(&context))) {
            return context;
        }
    }
    ComPtr<ITfDocumentMgr> document;
    if (thread_mgr_ && SUCCEEDED(thread_mgr_->GetFocus(&document)) && document) {
        document->GetTop(&context);
    }
    return context;
}

ComPtr<ITfCompartment> TextService::OpenCloseCompartment() const
{
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> compartment;
    if (thread_mgr_ && SUCCEEDED(thread_mgr_.As(&compartments))) {
        compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment);
    }
    return compartment;
}

void TextService::StartModeIndicators()
{
    if (const ComPtr<ITfCompartment> compartment = OpenCloseCompartment()) {
        VARIANT value;
        VariantInit(&value);
        // Keep the open state from an earlier activation on this thread.
        if (SUCCEEDED(compartment->GetValue(&value)) && value.vt == VT_I4) {
            static_cast<void>(session_.SetJapaneseMode(value.lVal != 0));
        }
        VariantClear(&value);
        ComPtr<ITfSource> source;
        if (FAILED(compartment.As(&source)) ||
            FAILED(source->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this),
                                      &compartment_cookie_))) {
            compartment_cookie_ = TF_INVALID_COOKIE;
        }
    }
    mode_button_ = LangBarButton::Create(this);
    ComPtr<ITfLangBarItemMgr> items;
    if (mode_button_ != nullptr && SUCCEEDED(thread_mgr_.As(&items))) {
        mode_button_added_ = SUCCEEDED(items->AddItem(mode_button_));
    }
    PublishMode();
}

void TextService::StopModeIndicators()
{
    if (compartment_cookie_ != TF_INVALID_COOKIE) {
        ComPtr<ITfSource> source;
        if (const ComPtr<ITfCompartment> compartment = OpenCloseCompartment();
            compartment && SUCCEEDED(compartment.As(&source))) {
            source->UnadviseSink(compartment_cookie_);
        }
        compartment_cookie_ = TF_INVALID_COOKIE;
    }
    if (mode_button_ != nullptr) {
        ComPtr<ITfLangBarItemMgr> items;
        if (mode_button_added_ && thread_mgr_ && SUCCEEDED(thread_mgr_.As(&items))) {
            items->RemoveItem(mode_button_);
        }
        mode_button_->Detach();
        mode_button_->Release();
        mode_button_ = nullptr;
        mode_button_added_ = false;
    }
}

void TextService::PublishMode()
{
    if (const ComPtr<ITfCompartment> compartment = OpenCloseCompartment()) {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = JapaneseMode() ? 1 : 0;
        compartment->SetValue(client_id_, &value);
    }
    if (mode_button_ != nullptr) {
        mode_button_->NotifyModeChanged();
    }
}

HRESULT TextService::RequestEdit(ITfContext* context, std::u16string commit)
{
    auto* edit = new (std::nothrow) EditSession(this, context, std::move(commit));
    if (edit == nullptr) {
        return E_OUTOFMEMORY;
    }
    HRESULT session_result = S_OK;
    // Synchronous inside key handling; queued if the document cannot be locked right now.
    const HRESULT hr =
        context->RequestEditSession(client_id_, edit, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &session_result);
    edit->Release();
    return FAILED(hr) ? hr : (session_result == TF_S_ASYNC ? S_OK : session_result);
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
    const HRESULT hr = ApplyText(cookie, context, commit);
    UpdateCandidateWindow(cookie, context);
    return hr;
}

void TextService::HideCandidateWindow()
{
    if (candidate_window_) {
        candidate_window_->Hide();
    }
}

// Shows the candidates of the focused segment under it (needs the edit cookie to measure the text).
void TextService::UpdateCandidateWindow(TfEditCookie cookie, ITfContext* context)
{
    if (!session_.CandidateListVisible() || !composition_) {
        HideCandidateWindow();
        return;
    }
    const std::vector<ConvertedSegment>& segments = session_.Segments();
    const std::size_t focus = session_.FocusedSegment();
    LONG offset = 0;
    for (std::size_t i = 0; i < focus; ++i) {
        offset += Length(segments[i].candidates[session_.SelectedCandidate(i)]);
    }
    const LONG length = Length(segments[focus].candidates[session_.SelectedCandidate(focus)]);

    RECT anchor{};
    ComPtr<ITfRange> range;
    ComPtr<ITfRange> segment;
    ComPtr<ITfContextView> view;
    LONG shifted = 0;
    BOOL clipped = FALSE;
    const bool measured = SUCCEEDED(composition_->GetRange(&range)) && SUCCEEDED(range->Clone(&segment)) &&
                          SUCCEEDED(segment->Collapse(cookie, TF_ANCHOR_START)) &&
                          SUCCEEDED(segment->ShiftEnd(cookie, offset + length, &shifted, nullptr)) &&
                          SUCCEEDED(segment->ShiftStart(cookie, offset, &shifted, nullptr)) &&
                          SUCCEEDED(context->GetActiveView(&view)) &&
                          SUCCEEDED(view->GetTextExt(cookie, segment.Get(), &anchor, &clipped));
    if (!measured) {
        POINT caret{};
        GetCaretPos(&caret);
        anchor = RECT{caret.x, caret.y, caret.x, caret.y + 20};
    }
    if (!candidate_window_) {
        candidate_window_.reset(new (std::nothrow) CandidateWindow());
    }
    if (candidate_window_) {
        candidate_window_->Show(segments[focus].candidates, session_.SelectedCandidate(focus), anchor);
    }
}

HRESULT TextService::ApplyText(TfEditCookie cookie, ITfContext* context, const std::u16string& commit)
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

HRESULT TextService::TestKey(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL key_up, BOOL* eaten)
{
    if (eaten == nullptr) {
        return E_INVALIDARG;
    }
    *eaten = FALSE;
    TextService* service = g_active_service;
    if (service == nullptr) {
        return E_UNEXPECTED;
    }
    // Same order as TSF: the test call decides whether the real call happens.
    BOOL test_eaten = FALSE;
    const HRESULT hr = key_up ? service->OnTestKeyUp(context, wparam, lparam, &test_eaten)
                              : service->OnTestKeyDown(context, wparam, lparam, &test_eaten);
    if (FAILED(hr) || !test_eaten) {
        return hr;
    }
    return key_up ? service->OnKeyUp(context, wparam, lparam, eaten) : service->OnKeyDown(context, wparam, lparam, eaten);
}

HRESULT TextService::TestUseDictionary(const wchar_t* path)
{
    TextService* service = g_active_service;
    if (service == nullptr) {
        return E_UNEXPECTED;
    }
    const Converter* converter = UseDictionaryFile(path);
    service->session_.SetConverter(converter);
    return converter != nullptr || path == nullptr ? S_OK : E_FAIL;
}

HWND TextService::TestCandidateWindow()
{
    TextService* service = g_active_service;
    return service != nullptr && service->candidate_window_ ? service->candidate_window_->window() : nullptr;
}

} // namespace astelio::tip

// Test entry point: sends a key to the TSF-activated text service without OS keyboard focus.
extern "C" HRESULT WINAPI AstelioTipTestKey(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL key_up,
                                            BOOL* eaten)
{
    return astelio::tip::TextService::TestKey(context, wparam, lparam, key_up, eaten);
}

// Test entry point: converts with the dictionary at `path` instead of the installed one.
extern "C" HRESULT WINAPI AstelioTipTestUseDictionary(const wchar_t* path)
{
    return astelio::tip::TextService::TestUseDictionary(path);
}

// Test entry point: the candidate window of the active text service, or nullptr.
extern "C" HWND WINAPI AstelioTipTestCandidateWindow()
{
    return astelio::tip::TextService::TestCandidateWindow();
}
