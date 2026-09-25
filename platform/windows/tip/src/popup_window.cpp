#include "popup_window.h"

#include "module.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace astelio::tip {
namespace {

constexpr wchar_t kClassName[] = L"AstelioPopupWindow";
// DWM attributes from newer SDKs, by value so older headers still build.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kWindowCornerPreference = 33;
constexpr DWORD kSystemBackdropType = 38;
constexpr int kCornerRound = 2;
constexpr int kBackdropNone = 1;
constexpr int kBackdropTransientWindow = 3; // acrylic, but solid while the window is inactive
constexpr float kGap = 4.0f;

// SetWindowCompositionAttribute (user32, undocumented but stable since Windows 10): the only way to get an
// acrylic blur on a window that is never activated. Looked up at run time; missing means no blur.
struct AccentPolicy {
    int state;
    int flags;
    DWORD gradient_color; // AABBGGRR tint over the blur
    int animation_id;
};
struct CompositionAttributeData {
    int attribute;
    void* data;
    SIZE_T size;
};
using SetWindowCompositionAttributeFunction = BOOL(WINAPI*)(HWND, CompositionAttributeData*);
constexpr int kAccentPolicyAttribute = 19;
constexpr int kAccentDisabled = 0;
constexpr int kAccentAcrylicBlurBehind = 4;

bool SetAccent(HWND window, int state, DWORD gradient_color)
{
    static const auto set_attribute = reinterpret_cast<SetWindowCompositionAttributeFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (set_attribute == nullptr) {
        return false;
    }
    AccentPolicy policy{state, 0, gradient_color, 0};
    CompositionAttributeData data{kAccentPolicyAttribute, &policy, sizeof(policy)};
    return set_attribute(window, &data) != FALSE;
}

// Shared by the windows of all threads; released with the last window, never from DllMain or static
// destructors (d2d1/dwrite must not be called under the loader lock or after they shut down).
ID2D1Factory* g_d2d = nullptr;
IDWriteFactory* g_dwrite = nullptr;
bool g_class_registered = false;
long g_window_count = 0;
SRWLOCK g_lock = SRWLOCK_INIT;

bool ReadUserFlag(const wchar_t* name, bool fallback)
{
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name,
                     RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return fallback;
    }
    return value != 0;
}

bool HighContrast()
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

D2D1_COLOR_F Color(float r, float g, float b, float a)
{
    return D2D1::ColorF(r, g, b, a);
}

D2D1_COLOR_F SystemColor(int index)
{
    const COLORREF color = GetSysColor(index);
    return Color(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, 1.0f);
}

D2D1_COLOR_F Accent(float alpha)
{
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (FAILED(DwmGetColorizationColor(&argb, &opaque))) {
        return Color(0.0f, 0.47f, 0.84f, alpha);
    }
    return Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, alpha);
}

bool EnsureFactories()
{
    if (g_d2d == nullptr && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &g_d2d))) {
        return false;
    }
    if (g_dwrite == nullptr && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                          reinterpret_cast<IUnknown**>(&g_dwrite)))) {
        return false;
    }
    return true;
}

int Scale(float dips, UINT dpi)
{
    return static_cast<int>(std::lround(dips * static_cast<float>(dpi) / 96.0f));
}

} // namespace

PopupWindow::~PopupWindow()
{
    if (window_ == nullptr) {
        return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
    target_.Reset();
    AcquireSRWLockExclusive(&g_lock);
    if (--g_window_count == 0) {
        if (g_class_registered) {
            UnregisterClassW(kClassName, ModuleHandle());
            g_class_registered = false;
        }
        if (g_dwrite != nullptr) {
            g_dwrite->Release();
            g_dwrite = nullptr;
        }
        if (g_d2d != nullptr) {
            g_d2d->Release();
            g_d2d = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool PopupWindow::EnsureWindow()
{
    if (window_ != nullptr) {
        return true;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool ready = EnsureFactories();
    if (ready && !g_class_registered) {
        WNDCLASSEXW window_class{sizeof(window_class)};
        window_class.lpfnWndProc = WindowProc;
        window_class.hInstance = ModuleHandle();
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kClassName;
        ready = RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        g_class_registered = ready;
    }
    if (ready) {
        window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0,
                                  0, 1, 1, nullptr, nullptr, ModuleHandle(), this);
        if (window_ != nullptr) {
            ++g_window_count;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return window_ != nullptr;
}

IDWriteFactory* PopupWindow::DWrite() const
{
    return g_dwrite;
}

UINT PopupWindow::Dpi() const
{
    const UINT dpi = window_ != nullptr ? GetDpiForWindow(window_) : 0;
    return dpi != 0 ? dpi : 96;
}

void PopupWindow::ApplyBackdrop()
{
    dark_ = !ReadUserFlag(L"AppsUseLightTheme", true);
    const BOOL dark = dark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(window_, kUseImmersiveDarkMode, &dark, sizeof(dark));
    const int corner = kCornerRound;
    DwmSetWindowAttribute(window_, kWindowCornerPreference, &corner, sizeof(corner));

    backdrop_ = Backdrop::Opaque;
    const bool transparency = ReadUserFlag(L"EnableTransparency", true) && !HighContrast();
    // Semi-transparent tint (AABBGGRR) so the blurred background shows through.
    const DWORD tint = dark_ ? 0x88202020 : 0x88F3F3F3;
    if (transparency && SetAccent(window_, kAccentAcrylicBlurBehind, tint)) {
        backdrop_ = Backdrop::Acrylic;
    } else {
        SetAccent(window_, kAccentDisabled, 0);
        const int backdrop = transparency ? kBackdropTransientWindow : kBackdropNone;
        if (SUCCEEDED(DwmSetWindowAttribute(window_, kSystemBackdropType, &backdrop, sizeof(backdrop))) &&
            transparency) {
            backdrop_ = Backdrop::SystemBackdrop;
        }
    }
    const MARGINS margins = backdrop_ != Backdrop::Opaque ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(window_, &margins);
}

PopupWindow::Palette PopupWindow::MakePalette() const
{
    if (HighContrast()) {
        return {SystemColor(COLOR_WINDOW), SystemColor(COLOR_WINDOWTEXT), SystemColor(COLOR_WINDOWTEXT),
                SystemColor(COLOR_HIGHLIGHT), SystemColor(COLOR_HIGHLIGHTTEXT)};
    }
    const D2D1_COLOR_F text = dark_ ? Color(1.0f, 1.0f, 1.0f, 0.95f) : Color(0.0f, 0.0f, 0.0f, 0.9f);
    const D2D1_COLOR_F secondary = dark_ ? Color(1.0f, 1.0f, 1.0f, 0.6f) : Color(0.0f, 0.0f, 0.0f, 0.55f);
    D2D1_COLOR_F background;
    switch (backdrop_) {
    case Backdrop::Acrylic:
        background = Color(0.0f, 0.0f, 0.0f, 0.0f); // the accent tint is part of the blur
        break;
    case Backdrop::SystemBackdrop:
        background = dark_ ? Color(0.08f, 0.08f, 0.08f, 0.35f) : Color(1.0f, 1.0f, 1.0f, 0.4f);
        break;
    case Backdrop::Opaque:
    default:
        background = dark_ ? Color(0.17f, 0.17f, 0.17f, 1.0f) : Color(0.98f, 0.98f, 0.98f, 1.0f);
        break;
    }
    return {background, text, secondary, Accent(dark_ ? 0.5f : 0.3f), text};
}

void PopupWindow::Place(SIZE size_dips, float left_inset_dips, const RECT& anchor)
{
    const UINT dpi = Dpi();
    const int width = Scale(static_cast<float>(size_dips.cx), dpi);
    const int height = Scale(static_cast<float>(size_dips.cy), dpi);
    const int gap = Scale(kGap, dpi);
    int x = anchor.left - Scale(left_inset_dips, dpi);
    int y = anchor.bottom + gap;

    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const RECT& work = monitor.rcWork;
        if (y + height > work.bottom) {
            y = anchor.top - height - gap;
        }
        x = std::clamp(x, static_cast<int>(work.left), std::max<int>(work.left, work.right - width));
        y = std::max<int>(y, work.top);
    }
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);
}

void PopupWindow::Hide()
{
    if (window_ != nullptr && IsWindowVisible(window_)) {
        ShowWindow(window_, SW_HIDE);
    }
}

void PopupWindow::Paint()
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    if (!target_) {
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        g_d2d->CreateDCRenderTarget(&properties, target_.GetAddressOf());
    }
    if (target_ && SUCCEEDED(target_->BindDC(dc, &client))) {
        const UINT dpi = Dpi();
        target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        const Palette palette = MakePalette();
        const float width = static_cast<float>(client.right) * 96.0f / static_cast<float>(dpi);

        target_->BeginDraw();
        target_->Clear(palette.background);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        target_->CreateSolidColorBrush(palette.text, brush.GetAddressOf());
        if (brush) {
            Render(target_.Get(), brush.Get(), palette, width);
        }
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
            target_.Reset();
        }
    }
    EndPaint(window_, &paint);
}

LRESULT CALLBACK PopupWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_PAINT:
        if (self != nullptr) {
            try {
                self->Paint();
            } catch (...) {
                ValidateRect(window, nullptr);
            }
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (self != nullptr) {
            const float scale = 96.0f / static_cast<float>(self->Dpi());
            try {
                self->OnClick(static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                              static_cast<float>(GET_Y_LPARAM(lparam)) * scale);
            } catch (...) {
                // A failed click changes nothing.
            }
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace astelio::tip
