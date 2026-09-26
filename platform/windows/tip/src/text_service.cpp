#include "text_service.h"

#include "candidate_window.h"
#include "display_attributes.h"
#include "emoji_history.h"
#include "emoji_window.h"
#include "key_translation.h"
#include "dictionary_loader.h"
#include "lang_bar_button.h"
#include "learning_manager.h"
#include "learning_store.h"
#include "mode_window.h"
#include "module.h"

#include <InputScope.h>
#include <oleauto.h>

#include <functional>
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

// The exe file name of this process in lower case (D-06).
std::wstring CurrentAppName()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::wstring name(path, length);
    name.erase(0, name.find_last_of(L"\\/") + 1);
    CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
    return name;
}

// Keys the app gets that can move the caret or change the text around it (T-B02-5).
bool MovesTheCaret(WPARAM wparam)
{
    switch (wparam) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END: case VK_PRIOR:
    case VK_NEXT: case VK_BACK: case VK_DELETE: case VK_RETURN: case VK_TAB: case VK_ESCAPE:
        return true;
    default:
        // Shortcuts such as Ctrl+V or Ctrl+Z change the text too.
        return GetKeyState(VK_CONTROL) < 0 && wparam != VK_CONTROL && wparam != VK_LCONTROL && wparam != VK_RCONTROL;
    }
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

// B-11, D-06: whether the text at `range` is a password or PIN field (its InputScope says so).
// GUID_PROP_INPUTSCOPE, which no import library defines.
constexpr GUID kInputScopeProperty = {0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};

bool IsPasswordField(TfEditCookie cookie, ITfContext* context, ITfRange* range)
{
    ComPtr<ITfReadOnlyProperty> property;
    if (range == nullptr || FAILED(context->GetAppProperty(kInputScopeProperty, &property)) || !property) {
        return false;
    }
    VARIANT value;
    VariantInit(&value);
    bool password = false;
    if (SUCCEEDED(property->GetValue(cookie, range, &value)) && value.vt == VT_UNKNOWN && value.punkVal != nullptr) {
        ComPtr<ITfInputScope> scope;
        InputScope* scopes = nullptr;
        UINT count = 0;
        if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&scope))) &&
            SUCCEEDED(scope->GetInputScopes(&scopes, &count)) && scopes != nullptr) {
            for (UINT i = 0; i < count; ++i) {
                password = password || scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PASSWORD ||
                           scopes[i] == IS_NUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN;
            }
            CoTaskMemFree(scopes);
        }
    }
    VariantClear(&value);
    return password;
}

// Runs `function` in a synchronous read-only edit session.
class ReadSession final : public ITfEditSession {
public:
    explicit ReadSession(std::function<void(TfEditCookie)> function) : function_(std::move(function)) {}

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
            function_(cookie);
            return S_OK;
        } catch (...) {
            return E_UNEXPECTED;
        }
    }

private:
    ~ReadSession() = default;

    LONG ref_count_ = 1;
    std::function<void(TfEditCookie)> function_;
};

// B-11: whether the caret is in a password field, where keys go to the app as they are.
bool CaretInPasswordField(TfClientId client_id, ITfContext* context)
{
    bool password = false;
    auto* session = new (std::nothrow) ReadSession([&](TfEditCookie cookie) {
        TF_SELECTION selection{};
        ULONG fetched = 0;
        if (SUCCEEDED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) && fetched == 1 &&
            selection.range != nullptr) {
            password = IsPasswordField(cookie, context, selection.range);
            selection.range->Release();
        }
    });
    if (session == nullptr) {
        return false;
    }
    HRESULT session_result = S_OK;
    context->RequestEditSession(client_id, session, TF_ES_SYNC | TF_ES_READ, &session_result);
    session->Release();
    return password;
}

// B-12: the caret in screen coordinates, from the document or else from the system caret.
bool CaretRect(TfClientId client_id, ITfContext* context, RECT* rect)
{
    bool found = false;
    auto* session = context == nullptr ? nullptr : new (std::nothrow) ReadSession([&](TfEditCookie cookie) {
        TF_SELECTION selection{};
        ULONG fetched = 0;
        if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || fetched != 1 ||
            selection.range == nullptr) {
            return;
        }
        ComPtr<ITfRange> range;
        range.Attach(selection.range);
        ComPtr<ITfContextView> view;
        BOOL clipped = FALSE;
        found = SUCCEEDED(context->GetActiveView(&view)) &&
                SUCCEEDED(view->GetTextExt(cookie, range.Get(), rect, &clipped)) && rect->bottom > rect->top;
    });
    if (session != nullptr) {
        HRESULT session_result = S_OK;
        context->RequestEditSession(client_id, session, TF_ES_SYNC | TF_ES_READ, &session_result);
        session->Release();
    }
    if (!found) {
        GUITHREADINFO info{sizeof(info)};
        if (GetGUIThreadInfo(0, &info) && info.hwndCaret != nullptr) {
            *rect = info.rcCaret;
            MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT*>(rect), 2);
            found = true;
        }
    }
    return found;
}

class EditSession final : public ITfEditSession {
public:
    EditSession(TextService* service, ITfContext* context, std::u16string commit, std::u16string undo)
        : service_(service), context_(context), commit_(std::move(commit)), undo_(std::move(undo))
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
            return service_->ApplyToDocument(cookie, context_.Get(), commit_, undo_);
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
    std::u16string undo_;
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
    } else if (riid == IID_ITfDisplayAttributeProvider) {
        *object = static_cast<ITfDisplayAttributeProvider*>(this);
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
    UseConverter(SharedConverter());
    session_.SetRecentEmoji(LoadRecentEmoji());
    app_name_ = CurrentAppName();
    learning_on_ = LearningEnabled();
    RefreshLearning(true);
    session_.SetTypoSuggestions(TypoSuggestionsEnabled());
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) {
        for (int index = 0; index < kDisplayAttributeCount; ++index) {
            categories->RegisterGUID(DisplayAttributeGuid(static_cast<DisplayAttribute>(index)),
                                     &attribute_atoms_[index]);
        }
    }
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
    emoji_window_.reset();
    mode_window_.reset();
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
    session_.ResetContext();
    return S_OK;
}

bool TextService::WillHandle(ITfContext* context, const KeyEvent& key)
{
    // Outside a composition, a key starts one; not in a password field (B-11).
    return session_.WillHandle(key) && (session_.Composing() || !CaretInPasswordField(client_id_, context));
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
        *eaten = key && WillHandle(context, *key) ? TRUE : FALSE;
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
        if (!key || !WillHandle(context, *key)) {
            if (MovesTheCaret(wparam)) {
                session_.ResetContext(); // the next word may not follow the last one
            }
            return S_OK;
        }
        *eaten = TRUE;
        ++Diagnostics().eaten;
        if (!session_.Composing()) {
            RefreshLearning();
        }
        const bool offered = session_.EmojiPaletteOffered();
        SessionOutput output = session_.Handle(*key);
        if (!offered && session_.EmojiPaletteOffered()) {
            session_.SetRecentEmoji(LoadRecentEmoji()); // other apps may have used emoji since
        }
        return Deliver(context, std::move(output));
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
        return SetMode(*side == ModifierSide::Right, context, true);
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
        return SetMode(!JapaneseMode(), FocusedContext().Get(), true);
    } catch (...) {
        return E_UNEXPECTED;
    }
}

HRESULT TextService::SetMode(bool japanese, ITfContext* context, bool show)
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
    RECT caret{};
    if (show && CaretRect(client_id_, context, &caret)) {
        if (!mode_window_) {
            mode_window_ = std::make_unique<ModeWindow>();
        }
        mode_window_->Show(JapaneseMode(), caret);
    }
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

HRESULT TextService::RequestEdit(ITfContext* context, std::u16string commit, std::u16string undo)
{
    auto* edit = new (std::nothrow) EditSession(this, context, std::move(commit), std::move(undo));
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

bool TextService::RemoveBeforeCaret(TfEditCookie cookie, ITfContext* context, const std::u16string& text)
{
    TF_SELECTION selection{};
    ULONG fetched = 0;
    if (composition_ || FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) ||
        fetched != 1 || selection.range == nullptr) {
        return false;
    }
    ComPtr<ITfRange> range;
    range.Attach(selection.range);
    LONG shifted = 0;
    const LONG length = Length(text);
    if (FAILED(range->Collapse(cookie, TF_ANCHOR_START)) ||
        FAILED(range->ShiftStart(cookie, -length, &shifted, nullptr)) || shifted != -length) {
        return false;
    }
    std::u16string before(text.size() + 1, u'\0');
    ULONG read = 0;
    if (FAILED(range->GetText(cookie, 0, reinterpret_cast<WCHAR*>(before.data()), static_cast<ULONG>(before.size()),
                              &read)) ||
        before.substr(0, read) != text) {
        return false;
    }
    return SUCCEEDED(range->SetText(cookie, 0, L"", 0)) && SUCCEEDED(SetCaret(cookie, context, range.Get()));
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
    // Nothing typed in a password field, in secret mode or in an app left out is learned (D-06).
    session_.SetRecording(recording_allowed_ && !IsPasswordField(cookie, context, range.Get()));
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

HRESULT TextService::ApplyToDocument(TfEditCookie cookie, ITfContext* context, const std::u16string& commit,
                                     const std::u16string& undo)
{
    if (!undo.empty() && !RemoveBeforeCaret(cookie, context, undo)) {
        session_.AbandonComposition(); // the text before the caret changed; keep the document as it is
    }
    const HRESULT hr = ApplyText(cookie, context, commit);
    UpdateCandidateWindow(cookie, context);
    return hr;
}

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** attributes)
{
    return EnumDisplayAttributes(attributes);
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** attribute)
{
    return GetDisplayAttribute(guid, attribute);
}

void TextService::ClearDisplayAttributes(TfEditCookie cookie, ITfContext* context, ITfRange* range)
{
    ComPtr<ITfProperty> property;
    if (range != nullptr && SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) {
        property->Clear(cookie, range);
    }
}

void TextService::ApplyDisplayAttributes(TfEditCookie cookie, ITfContext* context, ITfRange* composition)
{
    ComPtr<ITfProperty> property;
    if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) {
        return;
    }
    const auto set = [&](ITfRange* range, DisplayAttribute attribute) {
        const TfGuidAtom atom = attribute_atoms_[static_cast<int>(attribute)];
        if (atom == TF_INVALID_GUIDATOM) {
            return;
        }
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = static_cast<LONG>(atom);
        property->SetValue(cookie, range, &value);
    };
    if (!session_.Converting()) {
        set(composition, DisplayAttribute::Input);
        return;
    }
    const std::vector<ConvertedSegment>& segments = session_.Segments();
    LONG offset = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const LONG length = Length(segments[i].candidates[session_.SelectedCandidate(i)]);
        ComPtr<ITfRange> range;
        LONG shifted = 0;
        if (SUCCEEDED(composition->Clone(&range)) && SUCCEEDED(range->Collapse(cookie, TF_ANCHOR_START)) &&
            SUCCEEDED(range->ShiftEnd(cookie, offset + length, &shifted, nullptr)) &&
            SUCCEEDED(range->ShiftStart(cookie, offset, &shifted, nullptr))) {
            set(range.Get(), i == session_.FocusedSegment() ? DisplayAttribute::Focused : DisplayAttribute::Converted);
        }
        offset += length;
    }
}

void TextService::HideCandidateWindow()
{
    if (candidate_window_) {
        candidate_window_->Hide();
    }
    if (emoji_window_) {
        emoji_window_->Hide();
    }
}

void TextService::UseConverter(const Converter* converter)
{
    session_.SetConverter(converter);
    if (converter != nullptr) {
        session_.SetEmojiCatalog([converter] { return EmojiCatalogFor(converter); });
    } else {
        session_.SetEmojiCatalog(nullptr);
    }
}

void TextService::RefreshLearning(bool force)
{
    recording_allowed_ = !LearningPaused() && !AppLearningExcluded(app_name_);
    session_.SetLearning(learning_on_ ? &learning_ : nullptr);
    if (!learning_on_) {
        return;
    }
    const std::uint64_t stamp = LearningFileStamp();
    if (force || stamp != learning_stamp_) {
        learning_ = LoadLearning();
        learning_stamp_ = stamp;
    }
}

void TextService::OnLearningCommand(LearningCommand command, HWND owner)
{
    switch (command) {
    case LearningCommand::Toggle:
        learning_on_ = !learning_on_;
        SetLearningEnabled(learning_on_);
        RefreshLearning(true);
        break;
    case LearningCommand::Manage:
        ShowLearningManager();
        break;
    case LearningCommand::Pause:
        SetLearningPaused(!LearningPaused());
        RefreshLearning();
        break;
    case LearningCommand::ExcludeApp:
        SetAppLearningExcluded(app_name_, !AppLearningExcluded(app_name_));
        RefreshLearning();
        break;
    case LearningCommand::Clear:
        // 入力履歴をすべて削除しますか？
        if (MessageBoxW(owner, L"\u5165\u529B\u5C65\u6B74\u3092\u3059\u3079\u3066\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F",
                        L"Astelio IME", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND) == IDYES) {
            learning_.Clear();
            SaveLearning(learning_);
            learning_stamp_ = LearningFileStamp();
        }
        break;
    }
}

HRESULT TextService::Deliver(ITfContext* context, SessionOutput output)
{
    if (output.recent_emoji_changed && !session_.RecentEmoji().empty()) {
        // Merge with what other apps saved since this one loaded the file.
        std::vector<std::u16string> recent = LoadRecentEmoji();
        const std::u16string newest = session_.RecentEmoji().front();
        std::erase(recent, newest);
        recent.insert(recent.begin(), newest);
        session_.SetRecentEmoji(std::move(recent));
        SaveRecentEmoji(session_.RecentEmoji());
    }
    if (output.learning_changed && learning_on_) {
        SaveLearning(learning_);
        learning_stamp_ = LearningFileStamp();
    }
    if (context != nullptr && (output.composition_changed || !output.commit.empty())) {
        return RequestEdit(context, std::move(output.commit), std::move(output.undo_commit));
    }
    return S_OK;
}

void TextService::OnEmojiClick(bool category, std::size_t index)
{
    const ComPtr<ITfContext> context = FocusedContext();
    if (!context) {
        return;
    }
    SessionOutput output;
    if (category) {
        output.composition_changed = session_.SelectEmojiCategory(static_cast<EmojiCategory>(index));
    } else {
        output = session_.PickEmoji(index);
    }
    Deliver(context.Get(), std::move(output));
}

// Screen rectangle of [offset, offset + length) in the composition (needs the edit cookie to measure the text).
bool TextService::CompositionRect(TfEditCookie cookie, ITfContext* context, LONG offset, LONG length,
                                  RECT* rect) const
{
    ComPtr<ITfRange> range;
    ComPtr<ITfRange> segment;
    ComPtr<ITfContextView> view;
    LONG shifted = 0;
    BOOL clipped = FALSE;
    const bool measured = composition_ && SUCCEEDED(composition_->GetRange(&range)) &&
                          SUCCEEDED(range->Clone(&segment)) && SUCCEEDED(segment->Collapse(cookie, TF_ANCHOR_START)) &&
                          SUCCEEDED(segment->ShiftEnd(cookie, offset + length, &shifted, nullptr)) &&
                          SUCCEEDED(segment->ShiftStart(cookie, offset, &shifted, nullptr)) &&
                          SUCCEEDED(context->GetActiveView(&view)) &&
                          SUCCEEDED(view->GetTextExt(cookie, segment.Get(), rect, &clipped));
    if (!measured) {
        POINT caret{};
        GetCaretPos(&caret);
        *rect = RECT{caret.x, caret.y, caret.x, caret.y + 20};
    }
    return measured;
}

// Shows the emoji palette, the candidates of the focused segment under it, or the predictions under the text
// being typed.
void TextService::UpdateCandidateWindow(TfEditCookie cookie, ITfContext* context)
{
    if (composition_ && (session_.EmojiPaletteActive() || session_.EmojiPaletteOffered())) {
        if (candidate_window_) {
            candidate_window_->Hide();
        }
        RECT anchor{};
        CompositionRect(cookie, context, 0, Length(session_.CompositionText()), &anchor);
        if (!emoji_window_) {
            emoji_window_.reset(new (std::nothrow) EmojiWindow(
                [this](EmojiWindow::Click click) { OnEmojiClick(click.category, click.index); }));
        }
        if (emoji_window_) {
            emoji_window_->Show(session_.EmojiPalette(), anchor);
        }
        return;
    }
    if (emoji_window_) {
        emoji_window_->Hide();
    }
    const bool predicting = !session_.Converting() && !session_.Predictions().empty();
    if ((!session_.CandidateListVisible() && !predicting) || !composition_) {
        HideCandidateWindow();
        return;
    }
    LONG offset = 0;
    LONG length = Length(session_.CompositionText());
    const std::vector<std::u16string>* candidates = &session_.Predictions();
    std::size_t selected = CandidateWindow::kNoSelection;
    std::vector<CandidateWindow::Mark> marks;
    if (!predicting) {
        const std::vector<ConvertedSegment>& segments = session_.Segments();
        const std::size_t focus = session_.FocusedSegment();
        for (std::size_t i = 0; i < focus; ++i) {
            offset += Length(segments[i].candidates[session_.SelectedCandidate(i)]);
        }
        length = Length(segments[focus].candidates[session_.SelectedCandidate(focus)]);
        candidates = &segments[focus].candidates;
        selected = session_.SelectedCandidate(focus);
        for (std::size_t i = 0; i < candidates->size(); ++i) {
            marks.push_back(session_.IsTypoCandidate(focus, i)      ? CandidateWindow::Mark::Typo
                            : session_.IsLearnedCandidate(focus, i) ? CandidateWindow::Mark::Learned
                                                                    : CandidateWindow::Mark::None);
        }
    }

    RECT anchor{};
    CompositionRect(cookie, context, offset, length, &anchor);
    if (!candidate_window_) {
        candidate_window_.reset(new (std::nothrow) CandidateWindow());
    }
    if (candidate_window_) {
        candidate_window_->Show(*candidates, selected, anchor, marks);
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
                ClearDisplayAttributes(cookie, context, range.Get());
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
    if (SUCCEEDED(hr)) {
        ApplyDisplayAttributes(cookie, context, range.Get());
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
    service->UseConverter(converter);
    return converter != nullptr || path == nullptr ? S_OK : E_FAIL;
}

HWND TextService::TestCandidateWindow()
{
    TextService* service = g_active_service;
    return service != nullptr && service->candidate_window_ ? service->candidate_window_->window() : nullptr;
}

HWND TextService::TestEmojiWindow()
{
    TextService* service = g_active_service;
    return service != nullptr && service->emoji_window_ ? service->emoji_window_->window() : nullptr;
}

HWND TextService::TestModeWindow()
{
    TextService* service = g_active_service;
    return service != nullptr && service->mode_window_ ? service->mode_window_->window() : nullptr;
}

void TextService::TestUseLearningFile(const wchar_t* path)
{
    UseLearningFile(path);
    if (TextService* service = g_active_service) {
        service->RefreshLearning(true);
    }
}

void TextService::TestUseSettingsKey(const wchar_t* key)
{
    UseSettingsKey(key);
    if (TextService* service = g_active_service) {
        service->learning_on_ = LearningEnabled();
        service->session_.SetTypoSuggestions(TypoSuggestionsEnabled());
        service->RefreshLearning(true);
    }
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

// Test entry point: the emoji palette window of the active text service, or nullptr.
extern "C" HWND WINAPI AstelioTipTestEmojiWindow()
{
    return astelio::tip::TextService::TestEmojiWindow();
}

// Test entry point: the mode popup (B-12) of the active text service, or nullptr.
extern "C" HWND WINAPI AstelioTipTestModeWindow()
{
    return astelio::tip::TextService::TestModeWindow();
}

// Test entry point: keeps the emoji history in `path` instead of the user's profile (nullptr restores it).
extern "C" void WINAPI AstelioTipTestUseEmojiHistory(const wchar_t* path)
{
    astelio::tip::UseRecentEmojiFile(path);
}

// Test entry point: keeps the learning history in `path` instead of the user's profile (nullptr restores it).
extern "C" void WINAPI AstelioTipTestUseLearningHistory(const wchar_t* path)
{
    astelio::tip::TextService::TestUseLearningFile(path);
}

// Test entry point: reads the settings from `key` under HKCU instead of the user's (nullptr restores it).
extern "C" void WINAPI AstelioTipTestUseSettingsKey(const wchar_t* key)
{
    astelio::tip::TextService::TestUseSettingsKey(key);
}
