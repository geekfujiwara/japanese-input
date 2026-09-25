#include "candidate_window.h"

#include "module.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace astelio::tip {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kClassName[] = L"AstelioCandidateWindow";
// DWM attributes from newer SDKs, by value so older headers still build.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kWindowCornerPreference = 33;
constexpr DWORD kSystemBackdropType = 38;
constexpr int kCornerRound = 2;
constexpr int kBackdropTransientWindow = 3; // acrylic

// Layout in device-independent pixels.
constexpr float kPadding = 6.0f;
constexpr float kRowHeight = 30.0f;
constexpr float kNumberLeft = 10.0f;
constexpr float kNumberWidth = 22.0f;
constexpr float kTextLeft = kPadding + kNumberLeft + kNumberWidth;
constexpr float kRightPadding = 16.0f;
constexpr float kFooterHeight = 22.0f;
constexpr float kMinWidth = 140.0f;
constexpr float kMaxTextWidth = 480.0f;
constexpr float kGap = 4.0f;

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

struct Palette {
    D2D1_COLOR_F background;
    D2D1_COLOR_F text;
    D2D1_COLOR_F secondary;
    D2D1_COLOR_F highlight;
    D2D1_COLOR_F highlight_text;
};

Palette MakePalette(bool glass, bool dark)
{
    if (HighContrast()) {
        return {SystemColor(COLOR_WINDOW), SystemColor(COLOR_WINDOWTEXT), SystemColor(COLOR_WINDOWTEXT),
                SystemColor(COLOR_HIGHLIGHT), SystemColor(COLOR_HIGHLIGHTTEXT)};
    }
    const D2D1_COLOR_F text = dark ? Color(1.0f, 1.0f, 1.0f, 0.95f) : Color(0.0f, 0.0f, 0.0f, 0.9f);
    const D2D1_COLOR_F secondary = dark ? Color(1.0f, 1.0f, 1.0f, 0.6f) : Color(0.0f, 0.0f, 0.0f, 0.55f);
    D2D1_COLOR_F background;
    if (glass) {
        // A light tint over the blurred backdrop keeps the text readable.
        background = dark ? Color(0.08f, 0.08f, 0.08f, 0.35f) : Color(1.0f, 1.0f, 1.0f, 0.4f);
    } else {
        background = dark ? Color(0.17f, 0.17f, 0.17f, 1.0f) : Color(0.98f, 0.98f, 0.98f, 1.0f);
    }
    return {background, text, secondary, Accent(dark ? 0.5f : 0.3f), text};
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

const wchar_t* Wide(const std::u16string& text)
{
    return reinterpret_cast<const wchar_t*>(text.c_str());
}

} // namespace

CandidateWindow::~CandidateWindow()
{
    if (window_ == nullptr) {
        return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
    target_.Reset();
    text_format_.Reset();
    small_format_.Reset();
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

bool CandidateWindow::EnsureWindow()
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
    if (window_ == nullptr) {
        return false;
    }
    if (FAILED(g_dwrite->CreateTextFormat(L"Yu Gothic UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"ja-JP",
                                          text_format_.GetAddressOf())) ||
        FAILED(g_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ja-JP",
                                          small_format_.GetAddressOf()))) {
        return false;
    }
    text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    small_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    small_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

void CandidateWindow::ApplyBackdrop()
{
    dark_ = !ReadUserFlag(L"AppsUseLightTheme", true);
    const BOOL dark = dark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(window_, kUseImmersiveDarkMode, &dark, sizeof(dark));
    const int corner = kCornerRound;
    DwmSetWindowAttribute(window_, kWindowCornerPreference, &corner, sizeof(corner));

    glass_ = false;
    if (ReadUserFlag(L"EnableTransparency", true) && !HighContrast()) {
        const int backdrop = kBackdropTransientWindow;
        glass_ = SUCCEEDED(DwmSetWindowAttribute(window_, kSystemBackdropType, &backdrop, sizeof(backdrop)));
    }
    const MARGINS margins = glass_ ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(window_, &margins);
}

SIZE CandidateWindow::MeasureDips()
{
    float widest = 0.0f;
    for (std::size_t i = page_begin_; i < page_end_; ++i) {
        ComPtr<IDWriteTextLayout> layout;
        const std::u16string& text = candidates_[i];
        if (SUCCEEDED(g_dwrite->CreateTextLayout(Wide(text), static_cast<UINT32>(text.size()), text_format_.Get(),
                                                 kMaxTextWidth, kRowHeight, layout.GetAddressOf()))) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                widest = std::max(widest, std::min(metrics.widthIncludingTrailingWhitespace, kMaxTextWidth));
            }
        }
    }
    const float width = std::max(kMinWidth, kTextLeft + widest + kRightPadding);
    const float height = kPadding * 2 + static_cast<float>(page_end_ - page_begin_) * kRowHeight + kFooterHeight;
    return SIZE{static_cast<LONG>(std::ceil(width)), static_cast<LONG>(std::ceil(height))};
}

void CandidateWindow::Show(const std::vector<std::u16string>& candidates, std::size_t selected, const RECT& anchor)
{
    if (candidates.empty() || !EnsureWindow() || !text_format_ || !small_format_) {
        Hide();
        return;
    }
    candidates_ = candidates;
    selected_ = std::min(selected, candidates_.size() - 1);
    constexpr std::size_t kPage = 9;
    page_begin_ = selected_ / kPage * kPage;
    page_end_ = std::min(page_begin_ + kPage, candidates_.size());
    ApplyBackdrop();

    const UINT dpi = GetDpiForWindow(window_) != 0 ? GetDpiForWindow(window_) : 96;
    const SIZE dips = MeasureDips();
    const int width = Scale(static_cast<float>(dips.cx), dpi);
    const int height = Scale(static_cast<float>(dips.cy), dpi);
    const int gap = Scale(kGap, dpi);
    int x = anchor.left - Scale(kTextLeft, dpi);
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

void CandidateWindow::Hide()
{
    if (window_ != nullptr && IsWindowVisible(window_)) {
        ShowWindow(window_, SW_HIDE);
    }
}

void CandidateWindow::Paint()
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
        const UINT dpi = GetDpiForWindow(window_) != 0 ? GetDpiForWindow(window_) : 96;
        target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        const Palette palette = MakePalette(glass_, dark_);
        const float width = static_cast<float>(client.right) * 96.0f / static_cast<float>(dpi);

        target_->BeginDraw();
        target_->Clear(palette.background);
        ComPtr<ID2D1SolidColorBrush> brush;
        target_->CreateSolidColorBrush(palette.text, brush.GetAddressOf());
        if (brush) {
            for (std::size_t i = page_begin_; i < page_end_; ++i) {
                const float top = kPadding + static_cast<float>(i - page_begin_) * kRowHeight;
                const bool selected = i == selected_;
                if (selected) {
                    brush->SetColor(palette.highlight);
                    target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(kPadding, top, width - kPadding, top + kRowHeight), 4.0f, 4.0f),
                        brush.Get());
                }
                const wchar_t number[2] = {static_cast<wchar_t>(L'1' + (i - page_begin_)), L'\0'};
                brush->SetColor(selected ? palette.highlight_text : palette.secondary);
                target_->DrawTextW(number, 1, small_format_.Get(),
                                   D2D1::RectF(kPadding + kNumberLeft, top, kTextLeft, top + kRowHeight), brush.Get());
                brush->SetColor(selected ? palette.highlight_text : palette.text);
                const std::u16string& text = candidates_[i];
                target_->DrawTextW(Wide(text), static_cast<UINT32>(text.size()), text_format_.Get(),
                                   D2D1::RectF(kTextLeft, top, width - kRightPadding / 2, top + kRowHeight),
                                   brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            const std::wstring footer = std::to_wstring(selected_ + 1) + L" / " + std::to_wstring(candidates_.size());
            const float footer_top = kPadding + static_cast<float>(page_end_ - page_begin_) * kRowHeight;
            brush->SetColor(palette.secondary);
            small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            target_->DrawTextW(footer.c_str(), static_cast<UINT32>(footer.size()), small_format_.Get(),
                               D2D1::RectF(kPadding, footer_top, width - kPadding - 4.0f, footer_top + kFooterHeight),
                               brush.Get());
            small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
            target_.Reset();
        }
    }
    EndPaint(window_, &paint);
}

LRESULT CALLBACK CandidateWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
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
