#pragma once

#include "popup_window.h"

#include "astelio/input_session.h"

#include <cstddef>
#include <functional>
#include <utility>

namespace astelio::tip {

// B-13: emoji palette with a search row, category tabs, a grid and a footer (frosted glass, see PopupWindow).
class EmojiWindow final : public PopupWindow {
public:
    struct Click {
        bool category = false; // true: a category tab, false: an emoji in the grid
        std::size_t index = 0; // EmojiCategory, or the index into EmojiPaletteView::items
    };

    explicit EmojiWindow(std::function<void(Click)> on_click) : on_click_(std::move(on_click)) {}

    // `anchor`: screen rectangle of the text being typed.
    void Show(const EmojiPaletteView& view, const RECT& anchor);

private:
    bool EnsureFormats();
    std::size_t FirstVisible() const;
    void Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                float width) override;
    void OnClick(float x, float y) override;

    std::function<void(Click)> on_click_;
    EmojiPaletteView view_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> emoji_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tab_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
};

} // namespace astelio::tip
