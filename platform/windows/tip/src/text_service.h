#pragma once

#include "astelio/input_session.h"

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <string>

namespace astelio::tip {

class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink, public ITfCompositionSink {
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

    // Runs inside an edit session: commits `commit`, then shows the session's uncommitted text.
    HRESULT ApplyToDocument(TfEditCookie cookie, ITfContext* context, const std::u16string& commit);

private:
    TextService();
    ~TextService();

    HRESULT RequestEdit(ITfContext* context, std::u16string commit);
    HRESULT StartComposition(TfEditCookie cookie, ITfContext* context);
    HRESULT EndComposition(TfEditCookie cookie);

    LONG ref_count_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    bool key_sink_advised_ = false;
    InputSession session_;
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
};

} // namespace astelio::tip
