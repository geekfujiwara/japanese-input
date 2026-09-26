#pragma once

#include "popup_window.h"

namespace astelio::tip {

// B-12: shows the new mode (あ / A) near the caret for a moment after the mode changes.
class ModeWindow final : public PopupWindow {
public:
    ModeWindow() = default;

    // `anchor`: screen rectangle of the caret.
    void Show(bool japanese, const RECT& anchor);

    static constexpr UINT kShowMilliseconds = 1200;

private:
    void Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                float width) override;

    bool japanese_ = true;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
};

} // namespace astelio::tip
