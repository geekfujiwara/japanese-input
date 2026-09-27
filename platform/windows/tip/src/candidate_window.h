#pragma once

#include "popup_window.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace astelio::tip {

// Candidate list popup (frosted glass, see PopupWindow).
class CandidateWindow final : public PopupWindow {
public:
    // What a row shows besides its text.
    enum class Mark : unsigned char {
        None,
        Learned, // chosen before (D-04): a clock; Ctrl+Del removes it from the history
        Typo,    // B-14: もしかして
    };

    // `on_emoji_click(i)`: emoji i of the related emoji column was clicked.
    explicit CandidateWindow(std::function<void(std::size_t)> on_emoji_click = nullptr)
        : on_emoji_click_(std::move(on_emoji_click))
    {
    }

    // `anchor`: screen rectangle of the focused segment. The list opens below it, or above near the screen bottom.
    // `selected` == kNoSelection shows predictions while typing (nothing highlighted, Tab hint in the footer).
    // `marks[i]`: the mark of candidate i (missing means none). `emoji`: the related emoji column (B-03), with
    // `emoji_selected` highlighted when it has the focus.
    void Show(const std::vector<std::u16string>& candidates, std::size_t selected, const RECT& anchor,
              const std::vector<Mark>& marks = {}, const std::vector<std::u16string>& emoji = {},
              std::size_t emoji_selected = kNoSelection);

    static constexpr std::size_t kNoSelection = static_cast<std::size_t>(-1);

private:
    bool EnsureFormats();
    SIZE MeasureDips();
    Mark MarkOf(std::size_t index) const { return index < marks_.size() ? marks_[index] : Mark::None; }
    bool Learned(std::size_t index) const { return MarkOf(index) == Mark::Learned; }
    void Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                float width) override;
    void OnClick(float x, float y) override;

    std::function<void(std::size_t)> on_emoji_click_;
    std::vector<std::u16string> emoji_;
    std::size_t emoji_selected_ = kNoSelection;
    float emoji_left_ = 0.0f;
    float emoji_width_ = 0.0f;
    std::vector<std::u16string> candidates_;
    std::vector<Mark> marks_;
    std::size_t selected_ = 0;
    bool has_selection_ = false;
    // B-03: the candidates shown together, in columns of 9 (at most 3).
    std::size_t page_begin_ = 0;
    std::size_t page_end_ = 0;
    std::vector<float> column_left_;
    std::vector<float> column_width_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
};

} // namespace astelio::tip
