#pragma once

#include <windows.h>

#include <msctf.h>
#include <olectl.h>
#include <textstor.h>
#include <wrl/client.h>

#include <string>

namespace astelio::tip::testing {

// Minimal in-memory document for driving a TIP through real TSF in tests.
class TestTextStore final : public ITextStoreACP {
public:
    TestTextStore() = default;

    const std::wstring& Text() const { return text_; }
    LONG SelectionStart() const { return selection_start_; }
    LONG SelectionEnd() const { return selection_end_; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITextStoreACP
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* unknown, DWORD mask) override;
    STDMETHODIMP UnadviseSink(IUnknown* unknown) override;
    STDMETHODIMP RequestLock(DWORD lock_flags, HRESULT* session_result) override;
    STDMETHODIMP GetStatus(TS_STATUS* status) override;
    STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG length, LONG* result_start, LONG* result_end) override;
    STDMETHODIMP GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) override;
    STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override;
    STDMETHODIMP GetText(LONG start, LONG end, WCHAR* plain, ULONG plain_capacity, ULONG* plain_length,
                         TS_RUNINFO* runs, ULONG run_capacity, ULONG* run_count, LONG* next) override;
    STDMETHODIMP SetText(DWORD flags, LONG start, LONG end, const WCHAR* text, ULONG length,
                         TS_TEXTCHANGE* change) override;
    STDMETHODIMP GetFormattedText(LONG start, LONG end, IDataObject** data) override;
    STDMETHODIMP GetEmbedded(LONG position, REFGUID service, REFIID riid, IUnknown** unknown) override;
    STDMETHODIMP QueryInsertEmbedded(const GUID* service, const FORMATETC* format, BOOL* insertable) override;
    STDMETHODIMP InsertEmbedded(DWORD flags, LONG start, LONG end, IDataObject* data, TS_TEXTCHANGE* change) override;
    STDMETHODIMP RequestSupportedAttrs(DWORD flags, ULONG count, const TS_ATTRID* attributes) override;
    STDMETHODIMP RequestAttrsAtPosition(LONG position, ULONG count, const TS_ATTRID* attributes, DWORD flags) override;
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG position, ULONG count, const TS_ATTRID* attributes,
                                                     DWORD flags) override;
    STDMETHODIMP FindNextAttrTransition(LONG start, LONG halt, ULONG count, const TS_ATTRID* attributes, DWORD flags,
                                        LONG* next, BOOL* found, LONG* found_offset) override;
    STDMETHODIMP RetrieveRequestedAttrs(ULONG count, TS_ATTRVAL* values, ULONG* fetched) override;
    STDMETHODIMP GetEndACP(LONG* end) override;
    STDMETHODIMP GetActiveView(TsViewCookie* view) override;
    STDMETHODIMP GetACPFromPoint(TsViewCookie view, const POINT* point, DWORD flags, LONG* position) override;
    STDMETHODIMP GetTextExt(TsViewCookie view, LONG start, LONG end, RECT* rect, BOOL* clipped) override;
    STDMETHODIMP GetScreenExt(TsViewCookie view, RECT* rect) override;
    STDMETHODIMP GetWnd(TsViewCookie view, HWND* window) override;
    STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG length, LONG* start, LONG* end,
                                       TS_TEXTCHANGE* change) override;
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD flags, IDataObject* data, LONG* start, LONG* end,
                                           TS_TEXTCHANGE* change) override;

private:
    ~TestTextStore() = default;

    bool ValidRange(LONG start, LONG end) const;

    LONG ref_count_ = 1;
    Microsoft::WRL::ComPtr<ITextStoreACPSink> sink_;
    DWORD lock_ = 0;
    DWORD pending_lock_ = 0;
    std::wstring text_;
    LONG selection_start_ = 0;
    LONG selection_end_ = 0;
};

} // namespace astelio::tip::testing
