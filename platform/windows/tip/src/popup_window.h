#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

namespace astelio::tip {

// Base of the candidate list and the emoji palette: a topmost popup that never takes focus.
// On Windows 10/11 the background is an acrylic blur (frosted glass); with transparency effects off
// or in high contrast it is opaque.
class PopupWindow {
public:
    enum class Backdrop : unsigned char {
        Opaque,
        Acrylic,        // blur that also works while the window is inactive
        SystemBackdrop, // Windows 11 DWM backdrop (solid when inactive)
    };

    struct Palette {
        D2D1_COLOR_F background;
        D2D1_COLOR_F text;
        D2D1_COLOR_F secondary;
        D2D1_COLOR_F highlight;
        D2D1_COLOR_F highlight_text;
    };

    virtual ~PopupWindow();
    PopupWindow(const PopupWindow&) = delete;
    PopupWindow& operator=(const PopupWindow&) = delete;

    void Hide();
    HWND window() const { return window_; }

protected:
    PopupWindow() = default;

    // Creates the window (and the shared Direct2D / DirectWrite factories) on first use.
    bool EnsureWindow();
    // Reads the theme and applies the backdrop; call before showing.
    void ApplyBackdrop();
    // Shows `size_dips` below `anchor` (above it near the screen bottom), `left_inset_dips` left of its left edge.
    void Place(SIZE size_dips, float left_inset_dips, const RECT& anchor);
    UINT Dpi() const;
    Palette MakePalette() const;
    IDWriteFactory* DWrite() const;

    // Draws the content between BeginDraw and EndDraw; `width` is in DIPs.
    virtual void Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                        float width) = 0;
    // Left click at (x, y) in DIPs.
    virtual void OnClick(float /*x*/, float /*y*/) {}

    static const wchar_t* Wide(const std::u16string& text) { return reinterpret_cast<const wchar_t*>(text.c_str()); }

    HWND window_ = nullptr;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void Paint();

    Backdrop backdrop_ = Backdrop::Opaque;
    bool dark_ = false;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
};

} // namespace astelio::tip
