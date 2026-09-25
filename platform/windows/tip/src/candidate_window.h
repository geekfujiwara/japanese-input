#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstddef>
#include <string>
#include <vector>

namespace astelio::tip {

// Candidate list popup. On Windows 11 the background is the DWM acrylic backdrop (frosted glass);
// with transparency effects off, in high contrast, or on Windows 10 it is opaque.
class CandidateWindow {
public:
    CandidateWindow() = default;
    ~CandidateWindow();
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    // `anchor`: screen rectangle of the focused segment. The list opens below it, or above near the screen bottom.
    void Show(const std::vector<std::u16string>& candidates, std::size_t selected, const RECT& anchor);
    void Hide();
    HWND window() const { return window_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    bool EnsureWindow();
    void ApplyBackdrop();
    SIZE MeasureDips();
    void Paint();

    HWND window_ = nullptr;
    bool glass_ = false;
    bool dark_ = false;
    std::vector<std::u16string> candidates_;
    std::size_t selected_ = 0;
    std::size_t page_begin_ = 0;
    std::size_t page_end_ = 0;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
};

} // namespace astelio::tip
