#pragma once

#include "popup_window.h"

#include <cstddef>
#include <string>
#include <vector>

namespace astelio::tip {

// Candidate list popup (frosted glass, see PopupWindow).
class CandidateWindow final : public PopupWindow {
public:
    CandidateWindow() = default;

    // `anchor`: screen rectangle of the focused segment. The list opens below it, or above near the screen bottom.
    // `selected` == kNoSelection shows predictions while typing (nothing highlighted, Tab hint in the footer).
    // `learned[i]`: candidate i comes from the history (D-04); it gets a mark, and Ctrl+Del removes it.
    void Show(const std::vector<std::u16string>& candidates, std::size_t selected, const RECT& anchor,
              const std::vector<bool>& learned = {});

    static constexpr std::size_t kNoSelection = static_cast<std::size_t>(-1);

private:
    bool EnsureFormats();
    SIZE MeasureDips();
    bool Learned(std::size_t index) const { return index < learned_.size() && learned_[index]; }
    void Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                float width) override;

    std::vector<std::u16string> candidates_;
    std::vector<bool> learned_;
    std::size_t selected_ = 0;
    bool has_selection_ = false;
    std::size_t page_begin_ = 0;
    std::size_t page_end_ = 0;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
};

} // namespace astelio::tip
