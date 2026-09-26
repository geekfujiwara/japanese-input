#include "mode_window.h"

namespace astelio::tip {
namespace {

constexpr float kSizeDips = 32.0f;

} // namespace

void ModeWindow::Show(bool japanese, const RECT& anchor)
{
    if (!EnsureWindow()) {
        return;
    }
    if (!format_ && FAILED(DWrite()->CreateTextFormat(L"Yu Gothic UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f,
                                                      L"ja-JP", format_.ReleaseAndGetAddressOf()))) {
        return;
    }
    format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    japanese_ = japanese;
    ApplyBackdrop();
    Place(SIZE{static_cast<LONG>(kSizeDips), static_cast<LONG>(kSizeDips)}, 0.0f, anchor);
    HideAfter(kShowMilliseconds);
}

void ModeWindow::Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& /*palette*/,
                        float width)
{
    const wchar_t* text = japanese_ ? L"\u3042" : L"A";
    target->DrawText(text, 1, format_.Get(), D2D1::RectF(0.0f, 0.0f, width, width), brush);
}

} // namespace astelio::tip
