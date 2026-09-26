#pragma once

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

namespace astelio::tip {

class TextService;

// Input mode button (あ / A) shown in the taskbar input indicator (B-12).
class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
public:
    // Returns nullptr when out of memory. The caller owns one reference.
    static LangBarButton* Create(TextService* service);

    // The service is going away; TSF may still hold references to the button.
    void Detach() { service_ = nullptr; }
    void NotifyModeChanged();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
    STDMETHODIMP GetStatus(DWORD* status) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT point, const RECT* area) override;
    STDMETHODIMP InitMenu(ITfMenu* menu) override;
    STDMETHODIMP OnMenuSelect(UINT id) override;
    STDMETHODIMP GetIcon(HICON* icon) override;
    STDMETHODIMP GetText(BSTR* text) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) override;
    STDMETHODIMP UnadviseSink(DWORD cookie) override;

private:
    explicit LangBarButton(TextService* service);
    ~LangBarButton();

    bool JapaneseMode() const;
    void ShowMenu(POINT point);

    LONG ref_count_ = 1;
    TextService* service_;
    Microsoft::WRL::ComPtr<ITfLangBarItemSink> sink_;
};

} // namespace astelio::tip
