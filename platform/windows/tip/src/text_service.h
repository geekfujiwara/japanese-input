#pragma once

#include "astelio/converter.h"
#include "astelio/input_session.h"
#include "astelio/modifier_tap_tracker.h"
#include "astelio/user_dictionary.h"

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace astelio::tip {

class CandidateWindow;
class EmojiWindow;
class LangBarButton;
class ModeWindow;

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

    // The "settings" item of the mode button's menu: opens the settings app for this app (C-12).
    void OpenSettings();

    // Runs inside an edit session: commits `commit`, then shows the session's uncommitted text.
    HRESULT ApplyToDocument(TfEditCookie cookie, ITfContext* context, const std::u16string& commit,
                            const std::u16string& undo);

    static HRESULT TestKey(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL key_up, BOOL* eaten);
    static HRESULT TestUseDictionary(const wchar_t* path);
    static HWND TestCandidateWindow();
    static HWND TestEmojiWindow();
    static HWND TestModeWindow();
    static void TestUseLearningFile(const wchar_t* path);
    static void TestUseSettingsKey(const wchar_t* key);
    static void TestUseUserDictionaryFile(const wchar_t* path);

private:
    TextService();
    ~TextService();

    void UseConverter(const Converter* converter);
    // Reads the settings the settings app writes (HKCU\Software\AstelioIME) into the session.
    void ApplySettings();
    // Whether the IME takes `key` in `context`.
    bool WillHandle(ITfContext* context, const KeyEvent& key);
    // Loads the history again when another app (or the history window) changed the file.
    void RefreshLearning(bool force = false);
    // D-02: the same for the user dictionary, which the manager window or another app may have changed.
    void RefreshUserDictionary(bool force = false);
    // Gives the user dictionary to the converter and the session.
    void PassUserDictionary();
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

    HRESULT RequestEdit(ITfContext* context, std::u16string commit, std::u16string undo = {});
    // B-08: removes `text` when it is just before the caret; returns whether it did.
    bool RemoveBeforeCaret(TfEditCookie cookie, ITfContext* context, const std::u16string& text);
    HRESULT StartComposition(TfEditCookie cookie, ITfContext* context);
    HRESULT EndComposition(TfEditCookie cookie);
    // Switches the mode, commits into `context` when leaving Japanese, and updates the indicators.
    // `show`: the user switched, so the new mode pops up near the caret (B-12).
    HRESULT SetMode(bool japanese, ITfContext* context, bool show = false);
    Microsoft::WRL::ComPtr<ITfContext> FocusedContext() const;
    Microsoft::WRL::ComPtr<ITfCompartment> OpenCloseCompartment() const;
    // C-14: the mode shared between apps (nullptr when sharing is off or TSF has no global compartment).
    Microsoft::WRL::ComPtr<ITfCompartment> SharedModeCompartment() const;
    // Takes the shared mode, when there is one and it differs.
    void FollowSharedMode();
    void StartModeIndicators();
    void StopModeIndicators();
    void PublishMode();

    LONG ref_count_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    bool key_sink_advised_ = false;
    InputSession session_;
    // A copy of the process's shared converter, so that it can carry this service's user dictionary.
    std::optional<Converter> converter_;
    LearningHistory learning_;
    std::uint64_t learning_stamp_ = 0;
    bool learning_on_ = true;
    bool recording_allowed_ = true; // not in secret mode, and this app is not left out (D-06)
    UserDictionary user_dictionary_;
    std::uint64_t user_dictionary_stamp_ = 0;
    std::wstring app_name_;
    bool app_disabled_ = false;
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    ModifierTapTracker alt_taps_;
    std::optional<ModifierSide> pending_alt_tap_;
    DWORD compartment_cookie_ = TF_INVALID_COOKIE;
    DWORD shared_mode_cookie_ = TF_INVALID_COOKIE;
    bool share_mode_ = true;
    LangBarButton* mode_button_ = nullptr;
    bool mode_button_added_ = false;
    std::unique_ptr<CandidateWindow> candidate_window_;
    std::unique_ptr<EmojiWindow> emoji_window_;
    std::unique_ptr<ModeWindow> mode_window_;
    TfGuidAtom attribute_atoms_[3] = {TF_INVALID_GUIDATOM, TF_INVALID_GUIDATOM, TF_INVALID_GUIDATOM};
};

} // namespace astelio::tip
