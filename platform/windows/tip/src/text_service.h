#pragma once

#include "astelio/input_session.h"
#include "astelio/modifier_tap_tracker.h"

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <memory>
#include <optional>
#include <string>

namespace astelio::tip {

class CandidateWindow;
class EmojiWindow;
class LangBarButton;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfCompartmentEventSink,
                          public ITfDisplayAttributeProvider {
public:
    static HRESULT Create(REFIID riid, void** object);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor / ITfTextInputProcessorEx
    STDMETHODIMP Activate(ITfThreadMgr* thread_mgr, TfClientId client_id) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id, DWORD flags) override;
    STDMETHODIMP Deactivate() override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie cookie, ITfComposition* composition) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID compartment) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** attributes) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** attribute) override;

    bool JapaneseMode() const { return session_.JapaneseMode(); }
    // Mode button click: switches the mode in the focused document.
    HRESULT ToggleMode();

    // Runs inside an edit session: commits `commit`, then shows the session's uncommitted text.
    HRESULT ApplyToDocument(TfEditCookie cookie, ITfContext* context, const std::u16string& commit);

    static HRESULT TestKey(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL key_up, BOOL* eaten);
    static HRESULT TestUseDictionary(const wchar_t* path);
    static HWND TestCandidateWindow();
    static HWND TestEmojiWindow();

private:
    TextService();
    ~TextService();

    void UseConverter(const Converter* converter);
    // Sends the session's output to the document and saves the emoji history when it changed.
    HRESULT Deliver(ITfContext* context, SessionOutput output);
    void OnEmojiClick(bool category, std::size_t index);
    HRESULT ApplyText(TfEditCookie cookie, ITfContext* context, const std::u16string& commit);
    void UpdateCandidateWindow(TfEditCookie cookie, ITfContext* context);
    bool CompositionRect(TfEditCookie cookie, ITfContext* context, LONG offset, LONG length, RECT* rect) const;
    void HideCandidateWindow();
    // Underlines the composition: dotted while typing, solid per segment (bold for the focused one) while converting.
    void ApplyDisplayAttributes(TfEditCookie cookie, ITfContext* context, ITfRange* composition);
    void ClearDisplayAttributes(TfEditCookie cookie, ITfContext* context, ITfRange* range);

    HRESULT RequestEdit(ITfContext* context, std::u16string commit);
    HRESULT StartComposition(TfEditCookie cookie, ITfContext* context);
    HRESULT EndComposition(TfEditCookie cookie);
    // Switches the mode, commits into `context` when leaving Japanese, and updates the indicators.
    HRESULT SetMode(bool japanese, ITfContext* context);
    Microsoft::WRL::ComPtr<ITfContext> FocusedContext() const;
    Microsoft::WRL::ComPtr<ITfCompartment> OpenCloseCompartment() const;
    void StartModeIndicators();
    void StopModeIndicators();
    void PublishMode();

    LONG ref_count_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    bool key_sink_advised_ = false;
    InputSession session_;
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    ModifierTapTracker alt_taps_;
    std::optional<ModifierSide> pending_alt_tap_;
    DWORD compartment_cookie_ = TF_INVALID_COOKIE;
    LangBarButton* mode_button_ = nullptr;
    bool mode_button_added_ = false;
    std::unique_ptr<CandidateWindow> candidate_window_;
    std::unique_ptr<EmojiWindow> emoji_window_;
    TfGuidAtom attribute_atoms_[3] = {TF_INVALID_GUIDATOM, TF_INVALID_GUIDATOM, TF_INVALID_GUIDATOM};
};

} // namespace astelio::tip
