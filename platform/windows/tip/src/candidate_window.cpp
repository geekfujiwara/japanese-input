#include "candidate_window.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace astelio::tip {
namespace {

using Microsoft::WRL::ComPtr;

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
// The history mark (a clock) at the end of learned rows.
constexpr float kMarkWidth = 20.0f;
constexpr wchar_t kHistoryMark[] = L"\u23F2";
// Ctrl+Del 履歴から削除
constexpr wchar_t kForgetHint[] = L"Ctrl+Del \u5C65\u6B74\u304B\u3089\u524A\u9664";
constexpr float kHintWidth = 150.0f;

} // namespace

bool CandidateWindow::EnsureFormats()
{
    if (!EnsureWindow()) {
        return false;
    }
    if (text_format_ && small_format_) {
        return true;
    }
    if (FAILED(DWrite()->CreateTextFormat(L"Yu Gothic UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"ja-JP",
                                          text_format_.ReleaseAndGetAddressOf())) ||
        FAILED(DWrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ja-JP",
                                          small_format_.ReleaseAndGetAddressOf()))) {
        text_format_.Reset();
        small_format_.Reset();
        return false;
    }
    text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    small_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    small_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

SIZE CandidateWindow::MeasureDips()
{
    float widest = 0.0f;
    for (std::size_t i = page_begin_; i < page_end_; ++i) {
        ComPtr<IDWriteTextLayout> layout;
        const std::u16string& text = candidates_[i];
        if (SUCCEEDED(DWrite()->CreateTextLayout(Wide(text), static_cast<UINT32>(text.size()), text_format_.Get(),
                                                 kMaxTextWidth, kRowHeight, layout.GetAddressOf()))) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                widest = std::max(widest, std::min(metrics.widthIncludingTrailingWhitespace, kMaxTextWidth));
            }
        }
    }
    bool any_learned = false;
    for (std::size_t i = page_begin_; i < page_end_; ++i) {
        any_learned = any_learned || Learned(i);
    }
    float width = std::max(kMinWidth, kTextLeft + widest + kRightPadding + (any_learned ? kMarkWidth : 0.0f));
    if (has_selection_ && Learned(selected_)) {
        width = std::max(width, kPadding * 2 + kHintWidth + 60.0f);
    }
    const float height = kPadding * 2 + static_cast<float>(page_end_ - page_begin_) * kRowHeight + kFooterHeight;
    return SIZE{static_cast<LONG>(std::ceil(width)), static_cast<LONG>(std::ceil(height))};
}

void CandidateWindow::Show(const std::vector<std::u16string>& candidates, std::size_t selected, const RECT& anchor,
                          const std::vector<bool>& learned)
{
    if (candidates.empty() || !EnsureFormats()) {
        Hide();
        return;
    }
    candidates_ = candidates;
    learned_ = learned;
    has_selection_ = selected != kNoSelection;
    selected_ = has_selection_ ? std::min(selected, candidates_.size() - 1) : 0;
    constexpr std::size_t kPage = 9;
    page_begin_ = selected_ / kPage * kPage;
    page_end_ = std::min(page_begin_ + kPage, candidates_.size());
    ApplyBackdrop();
    Place(MeasureDips(), kTextLeft, anchor);
}

void CandidateWindow::Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                             float width)
{
    for (std::size_t i = page_begin_; i < page_end_; ++i) {
        const float top = kPadding + static_cast<float>(i - page_begin_) * kRowHeight;
        const bool selected = has_selection_ && i == selected_;
        if (selected) {
            brush->SetColor(palette.highlight);
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(kPadding, top, width - kPadding, top + kRowHeight), 4.0f, 4.0f), brush);
        }
        const wchar_t number[2] = {static_cast<wchar_t>(L'1' + (i - page_begin_)), L'\0'};
        brush->SetColor(selected ? palette.highlight_text : palette.secondary);
        target->DrawTextW(number, 1, small_format_.Get(),
                          D2D1::RectF(kPadding + kNumberLeft, top, kTextLeft, top + kRowHeight), brush);
        brush->SetColor(selected ? palette.highlight_text : palette.text);
        const std::u16string& text = candidates_[i];
        const float mark = Learned(i) ? kMarkWidth : 0.0f;
        target->DrawTextW(Wide(text), static_cast<UINT32>(text.size()), text_format_.Get(),
                          D2D1::RectF(kTextLeft, top, width - kRightPadding / 2 - mark, top + kRowHeight), brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        if (Learned(i)) {
            brush->SetColor(selected ? palette.highlight_text : palette.secondary);
            target->DrawTextW(kHistoryMark, 1, small_format_.Get(),
                              D2D1::RectF(width - kPadding - kMarkWidth, top, width - kPadding, top + kRowHeight),
                              brush);
        }
    }
    const std::wstring footer = has_selection_
                                    ? std::to_wstring(selected_ + 1) + L" / " + std::to_wstring(candidates_.size())
                                    : std::wstring(L"Tab \u2192 \u9078\u629E");
    const float footer_top = kPadding + static_cast<float>(page_end_ - page_begin_) * kRowHeight;
    brush->SetColor(palette.secondary);
    if (has_selection_ && Learned(selected_)) {
        target->DrawTextW(kForgetHint, static_cast<UINT32>(std::size(kForgetHint) - 1), small_format_.Get(),
                          D2D1::RectF(kPadding + 4.0f, footer_top, width - kPadding, footer_top + kFooterHeight),
                          brush);
    }
    small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    target->DrawTextW(footer.c_str(), static_cast<UINT32>(footer.size()), small_format_.Get(),
                      D2D1::RectF(kPadding, footer_top, width - kPadding - 4.0f, footer_top + kFooterHeight), brush);
    small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

} // namespace astelio::tip
