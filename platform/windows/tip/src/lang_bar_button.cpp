#include "lang_bar_button.h"

#include "astelio/tip/guids.h"
#include "module.h"
#include "text_service.h"

#include <oleauto.h>
#include <olectl.h>

#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

namespace astelio::tip {
namespace {

constexpr DWORD kSinkCookie = 1;

bool LightTaskbar()
{
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS &&
           value != 0;
}

// Draws the mode letter as an alpha icon so it reads on both light and dark taskbars.
HICON CreateModeIcon(const wchar_t* text)
{
    const int size = GetSystemMetrics(SM_CXSMICON);
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = size;
    bitmap_info.bmiHeader.biHeight = -size;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        return nullptr;
    }
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (color == nullptr) {
        DeleteDC(dc);
        return nullptr;
    }
    const HGDIOBJ old_bitmap = SelectObject(dc, color);
    HFONT font = CreateFontW(-size * 7 / 8, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                             L"Yu Gothic UI");
    const HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT area{0, 0, size, size};
    DrawTextW(dc, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();

    const std::uint32_t ink = LightTaskbar() ? 0x000000 : 0xFFFFFF;
    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) {
        const std::uint32_t coverage = pixels[i] & 0xFF;
        pixels[i] = (coverage << 24) | ink;
    }

    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(dc, old_bitmap);
    DeleteDC(dc);

    const std::vector<BYTE> mask_bits(static_cast<std::size_t>((size + 15) / 16 * 2 * size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, mask_bits.data());
    ICONINFO icon_info{};
    icon_info.fIcon = TRUE;
    icon_info.hbmMask = mask;
    icon_info.hbmColor = color;
    HICON icon = mask != nullptr ? CreateIconIndirect(&icon_info) : nullptr;
    if (mask != nullptr) {
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

} // namespace

LangBarButton* LangBarButton::Create(TextService* service)
{
    return new (std::nothrow) LangBarButton(service);
}

LangBarButton::LangBarButton(TextService* service) : service_(service)
{
    AddModuleRef();
}

LangBarButton::~LangBarButton()
{
    ReleaseModuleRef();
}

bool LangBarButton::JapaneseMode() const
{
    return service_ != nullptr && service_->JapaneseMode();
}

void LangBarButton::NotifyModeChanged()
{
    if (sink_) {
        sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** object)
{
    if (object == nullptr) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) {
        *object = static_cast<ITfLangBarItemButton*>(this);
    } else if (riid == IID_ITfSource) {
        *object = static_cast<ITfSource*>(this);
    } else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

STDMETHODIMP_(ULONG) LangBarButton::Release()
{
    const LONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) {
        delete this;
    }
    return static_cast<ULONG>(count);
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info)
{
    if (info == nullptr) {
        return E_INVALIDARG;
    }
    info->clsidService = kTextServiceClsid;
    info->guidItem = kLangBarInputModeGuid;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = 0;
    wcscpy_s(info->szDescription, L"Astelio \u5165\u529B\u30E2\u30FC\u30C9");
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* status)
{
    if (status == nullptr) {
        return E_INVALIDARG;
    }
    *status = 0;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL /*show*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tooltip)
{
    if (tooltip == nullptr) {
        return E_INVALIDARG;
    }
    *tooltip = SysAllocString(JapaneseMode() ? L"\u65E5\u672C\u8A9E\u5165\u529B\uFF08\u53F3Alt\uFF09"
                                             : L"\u82F1\u8A9E\u5165\u529B\uFF08\u5DE6Alt\uFF09");
    return *tooltip != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT point, const RECT* /*area*/)
{
    if (service_ == nullptr) {
        return S_OK;
    }
    if (click == TF_LBI_CLK_LEFT) {
        return service_->ToggleMode();
    }
    try {
        ShowMenu(point);
    } catch (...) {
        return E_UNEXPECTED;
    }
    return S_OK;
}

// D-04 / D-05: right click shows the history menu.
void LangBarButton::ShowMenu(POINT point)
{
    enum : UINT { kToggle = 1, kManage, kClear };
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    // 入力履歴を使う / 入力履歴の管理... / 入力履歴をすべて削除...
    AppendMenuW(menu, MF_STRING | (service_->LearningOn() ? MF_CHECKED : MF_UNCHECKED), kToggle,
                L"\u5165\u529B\u5C65\u6B74\u3092\u4F7F\u3046");
    AppendMenuW(menu, MF_STRING, kManage, L"\u5165\u529B\u5C65\u6B74\u306E\u7BA1\u7406...");
    AppendMenuW(menu, MF_STRING, kClear, L"\u5165\u529B\u5C65\u6B74\u3092\u3059\u3079\u3066\u524A\u9664...");

    // Owned by the app's focused window, like Mozc: a window of another thread cannot track the menu, and
    // TPM_NONOTIFY keeps the owner from changing the menu.
    HWND owner = GetFocus();
    HWND temporary = nullptr;
    if (owner == nullptr) {
        temporary = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, point.x, point.y, 0, 0, nullptr,
                                    nullptr, ModuleHandle(), nullptr);
        owner = temporary;
    }
    if (owner == nullptr) {
        DestroyMenu(menu);
        return;
    }
    if (const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            point.x = std::clamp(point.x, info.rcWork.left, info.rcWork.right);
        }
    }
    const UINT chosen = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, point.x, point.y, 0,
        owner, nullptr));
    DestroyMenu(menu);
    if (service_ != nullptr) {
        switch (chosen) {
        case kToggle: service_->OnLearningCommand(TextService::LearningCommand::Toggle, owner); break;
        case kManage: service_->OnLearningCommand(TextService::LearningCommand::Manage, owner); break;
        case kClear: service_->OnLearningCommand(TextService::LearningCommand::Clear, owner); break;
        default: break;
        }
    }
    if (temporary != nullptr) {
        DestroyWindow(temporary);
    }
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* /*menu*/)
{
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT /*id*/)
{
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* icon)
{
    if (icon == nullptr) {
        return E_INVALIDARG;
    }
    try {
        *icon = CreateModeIcon(JapaneseMode() ? L"\u3042" : L"A");
    } catch (...) {
        *icon = nullptr;
    }
    return *icon != nullptr ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* text)
{
    if (text == nullptr) {
        return E_INVALIDARG;
    }
    *text = SysAllocString(JapaneseMode() ? L"\u3042" : L"A");
    return *text != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie)
{
    if (sink == nullptr || cookie == nullptr) {
        return E_INVALIDARG;
    }
    if (riid != IID_ITfLangBarItemSink) {
        return CONNECT_E_CANNOTCONNECT;
    }
    if (sink_) {
        return CONNECT_E_ADVISELIMIT;
    }
    const HRESULT hr = sink->QueryInterface(IID_PPV_ARGS(&sink_));
    if (FAILED(hr)) {
        return E_NOINTERFACE;
    }
    *cookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie)
{
    if (cookie != kSinkCookie || !sink_) {
        return CONNECT_E_NOCONNECTION;
    }
    sink_.Reset();
    return S_OK;
}

} // namespace astelio::tip
