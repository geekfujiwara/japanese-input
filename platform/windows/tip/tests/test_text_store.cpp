#include "test_text_store.h"

#include <algorithm>

namespace astelio::tip::testing {

STDMETHODIMP TestTextStore::QueryInterface(REFIID riid, void** object)
{
    if (object == nullptr) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_ITextStoreACP) {
        *object = static_cast<ITextStoreACP*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) TestTextStore::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

STDMETHODIMP_(ULONG) TestTextStore::Release()
{
    const LONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) {
        delete this;
    }
    return static_cast<ULONG>(count);
}

bool TestTextStore::ValidRange(LONG start, LONG end) const
{
    const auto size = static_cast<LONG>(text_.size());
    return start >= 0 && start <= end && end <= size;
}

STDMETHODIMP TestTextStore::AdviseSink(REFIID riid, IUnknown* unknown, DWORD /*mask*/)
{
    if (riid != IID_ITextStoreACPSink || unknown == nullptr) {
        return E_INVALIDARG;
    }
    if (sink_) {
        return CONNECT_E_ADVISELIMIT;
    }
    return unknown->QueryInterface(IID_PPV_ARGS(&sink_));
}

STDMETHODIMP TestTextStore::UnadviseSink(IUnknown* /*unknown*/)
{
    sink_.Reset();
    return S_OK;
}

STDMETHODIMP TestTextStore::RequestLock(DWORD lock_flags, HRESULT* session_result)
{
    if (session_result == nullptr) {
        return E_INVALIDARG;
    }
    if (!sink_) {
        return E_UNEXPECTED;
    }
    if (lock_ != 0) {
        if ((lock_flags & TS_LF_SYNC) != 0) {
            *session_result = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        pending_lock_ = lock_flags & TS_LF_READWRITE;
        *session_result = TS_S_ASYNC;
        return S_OK;
    }
    lock_ = lock_flags & TS_LF_READWRITE;
    *session_result = sink_->OnLockGranted(lock_);
    lock_ = 0;
    while (pending_lock_ != 0) {
        lock_ = pending_lock_;
        pending_lock_ = 0;
        sink_->OnLockGranted(lock_);
        lock_ = 0;
    }
    return S_OK;
}

STDMETHODIMP TestTextStore::GetStatus(TS_STATUS* status)
{
    if (status == nullptr) {
        return E_INVALIDARG;
    }
    status->dwDynamicFlags = 0;
    status->dwStaticFlags = TS_SS_NOHIDDENTEXT;
    return S_OK;
}

STDMETHODIMP TestTextStore::QueryInsert(LONG start, LONG end, ULONG /*length*/, LONG* result_start,
                                        LONG* result_end)
{
    if (result_start == nullptr || result_end == nullptr) {
        return E_INVALIDARG;
    }
    if (!ValidRange(start, end)) {
        return E_INVALIDARG;
    }
    *result_start = start;
    *result_end = end;
    return S_OK;
}

STDMETHODIMP TestTextStore::GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched)
{
    if (selection == nullptr || fetched == nullptr) {
        return E_INVALIDARG;
    }
    if (lock_ == 0) {
        return TS_E_NOLOCK;
    }
    *fetched = 0;
    if (count == 0 || (index != 0 && index != TF_DEFAULT_SELECTION)) {
        return S_OK;
    }
    selection[0].acpStart = selection_start_;
    selection[0].acpEnd = selection_end_;
    selection[0].style.ase = TS_AE_END;
    selection[0].style.fInterimChar = FALSE;
    *fetched = 1;
    return S_OK;
}

STDMETHODIMP TestTextStore::SetSelection(ULONG count, const TS_SELECTION_ACP* selection)
{
    if (selection == nullptr || count == 0) {
        return E_INVALIDARG;
    }
    if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE) {
        return TS_E_NOLOCK;
    }
    if (!ValidRange(selection[0].acpStart, selection[0].acpEnd)) {
        return TS_E_INVALIDPOS;
    }
    selection_start_ = selection[0].acpStart;
    selection_end_ = selection[0].acpEnd;
    return S_OK;
}

STDMETHODIMP TestTextStore::GetText(LONG start, LONG end, WCHAR* plain, ULONG plain_capacity, ULONG* plain_length,
                                    TS_RUNINFO* runs, ULONG run_capacity, ULONG* run_count, LONG* next)
{
    if (plain_length == nullptr || run_count == nullptr || next == nullptr) {
        return E_INVALIDARG;
    }
    if (lock_ == 0) {
        return TS_E_NOLOCK;
    }
    const auto size = static_cast<LONG>(text_.size());
    if (end == -1) {
        end = size;
    }
    if (!ValidRange(start, end)) {
        return TS_E_INVALIDPOS;
    }
    ULONG copied = static_cast<ULONG>(end - start);
    if (plain != nullptr) {
        copied = std::min(copied, plain_capacity);
        std::copy_n(text_.data() + start, copied, plain);
    } else {
        copied = plain_capacity == 0 ? copied : std::min(copied, plain_capacity);
    }
    *plain_length = plain != nullptr ? copied : 0;
    *run_count = 0;
    if (runs != nullptr && run_capacity > 0 && copied > 0) {
        runs[0].uCount = copied;
        runs[0].type = TS_RT_PLAIN;
        *run_count = 1;
    }
    *next = start + static_cast<LONG>(copied);
    return S_OK;
}

STDMETHODIMP TestTextStore::SetText(DWORD /*flags*/, LONG start, LONG end, const WCHAR* text, ULONG length,
                                    TS_TEXTCHANGE* change)
{
    if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE) {
        return TS_E_NOLOCK;
    }
    if (!ValidRange(start, end) || (text == nullptr && length != 0)) {
        return TS_E_INVALIDPOS;
    }
    text_.replace(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start), text == nullptr ? L"" : text,
                  length);
    selection_start_ = selection_end_ = start + static_cast<LONG>(length);
    if (change != nullptr) {
        change->acpStart = start;
        change->acpOldEnd = end;
        change->acpNewEnd = start + static_cast<LONG>(length);
    }
    return S_OK;
}

STDMETHODIMP TestTextStore::GetFormattedText(LONG /*start*/, LONG /*end*/, IDataObject** /*data*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP TestTextStore::GetEmbedded(LONG /*position*/, REFGUID /*service*/, REFIID /*riid*/,
                                        IUnknown** /*unknown*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP TestTextStore::QueryInsertEmbedded(const GUID* /*service*/, const FORMATETC* /*format*/,
                                                BOOL* insertable)
{
    if (insertable == nullptr) {
        return E_INVALIDARG;
    }
    *insertable = FALSE;
    return S_OK;
}

STDMETHODIMP TestTextStore::InsertEmbedded(DWORD /*flags*/, LONG /*start*/, LONG /*end*/, IDataObject* /*data*/,
                                           TS_TEXTCHANGE* /*change*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP TestTextStore::RequestSupportedAttrs(DWORD /*flags*/, ULONG /*count*/, const TS_ATTRID* /*attributes*/)
{
    return S_OK;
}

STDMETHODIMP TestTextStore::RequestAttrsAtPosition(LONG /*position*/, ULONG /*count*/,
                                                   const TS_ATTRID* /*attributes*/, DWORD /*flags*/)
{
    return S_OK;
}

STDMETHODIMP TestTextStore::RequestAttrsTransitioningAtPosition(LONG /*position*/, ULONG /*count*/,
                                                                const TS_ATTRID* /*attributes*/, DWORD /*flags*/)
{
    return S_OK;
}

STDMETHODIMP TestTextStore::FindNextAttrTransition(LONG /*start*/, LONG halt, ULONG /*count*/,
                                                   const TS_ATTRID* /*attributes*/, DWORD /*flags*/, LONG* next,
                                                   BOOL* found, LONG* found_offset)
{
    if (next == nullptr || found == nullptr || found_offset == nullptr) {
        return E_INVALIDARG;
    }
    *next = halt;
    *found = FALSE;
    *found_offset = 0;
    return S_OK;
}

STDMETHODIMP TestTextStore::RetrieveRequestedAttrs(ULONG /*count*/, TS_ATTRVAL* /*values*/, ULONG* fetched)
{
    if (fetched == nullptr) {
        return E_INVALIDARG;
    }
    *fetched = 0;
    return S_OK;
}

STDMETHODIMP TestTextStore::GetEndACP(LONG* end)
{
    if (end == nullptr) {
        return E_INVALIDARG;
    }
    if (lock_ == 0) {
        return TS_E_NOLOCK;
    }
    *end = static_cast<LONG>(text_.size());
    return S_OK;
}

STDMETHODIMP TestTextStore::GetActiveView(TsViewCookie* view)
{
    if (view == nullptr) {
        return E_INVALIDARG;
    }
    *view = 1;
    return S_OK;
}

STDMETHODIMP TestTextStore::GetACPFromPoint(TsViewCookie /*view*/, const POINT* /*point*/, DWORD /*flags*/,
                                            LONG* /*position*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP TestTextStore::GetTextExt(TsViewCookie /*view*/, LONG /*start*/, LONG /*end*/, RECT* rect,
                                       BOOL* clipped)
{
    if (rect == nullptr || clipped == nullptr) {
        return E_INVALIDARG;
    }
    *rect = RECT{0, 0, 10, 20};
    *clipped = FALSE;
    return S_OK;
}

STDMETHODIMP TestTextStore::GetScreenExt(TsViewCookie /*view*/, RECT* rect)
{
    if (rect == nullptr) {
        return E_INVALIDARG;
    }
    *rect = RECT{0, 0, 400, 100};
    return S_OK;
}

STDMETHODIMP TestTextStore::GetWnd(TsViewCookie /*view*/, HWND* window)
{
    if (window == nullptr) {
        return E_INVALIDARG;
    }
    *window = nullptr;
    return S_OK;
}

STDMETHODIMP TestTextStore::InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG length, LONG* start,
                                                  LONG* end, TS_TEXTCHANGE* change)
{
    if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE) {
        return TS_E_NOLOCK;
    }
    const LONG insert_start = selection_start_;
    const LONG insert_end = selection_end_;
    if ((flags & TS_IAS_QUERYONLY) != 0) {
        if (start != nullptr) {
            *start = insert_start;
        }
        if (end != nullptr) {
            *end = insert_end;
        }
        return S_OK;
    }
    if (text == nullptr && length != 0) {
        return E_INVALIDARG;
    }
    text_.replace(static_cast<std::size_t>(insert_start), static_cast<std::size_t>(insert_end - insert_start),
                  text == nullptr ? L"" : text, length);
    const LONG new_end = insert_start + static_cast<LONG>(length);
    selection_start_ = insert_start;
    selection_end_ = new_end;
    if (start != nullptr) {
        *start = insert_start;
    }
    if (end != nullptr) {
        *end = new_end;
    }
    if (change != nullptr) {
        change->acpStart = insert_start;
        change->acpOldEnd = insert_end;
        change->acpNewEnd = new_end;
    }
    return S_OK;
}

STDMETHODIMP TestTextStore::InsertEmbeddedAtSelection(DWORD /*flags*/, IDataObject* /*data*/, LONG* /*start*/,
                                                      LONG* /*end*/, TS_TEXTCHANGE* /*change*/)
{
    return E_NOTIMPL;
}

} // namespace astelio::tip::testing
