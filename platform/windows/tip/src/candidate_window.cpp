#include "candidate_window.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
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
// The history mark (a clock) at the end of learned rows, and the もしかして label at the end of a typo row.
constexpr float kMarkWidth = 20.0f;
constexpr wchar_t kHistoryMark[] = L"\u23F2";
constexpr float kTypoLabelWidth = 64.0f;
constexpr wchar_t kTypoLabel[] = L"\u3082\u3057\u304B\u3057\u3066";
// Ctrl+Del 履歴から削除
constexpr wchar_t kForgetHint[] = L"Ctrl+Del \u5C65\u6B74\u304B\u3089\u524A\u9664";
constexpr float kHintWidth = 150.0f;
// B-03: columns of 9 candidates, at most 3 (InputSession::kCandidateGridSize).
constexpr std::size_t kColumnRows = 9;
constexpr std::size_t kColumns = 3;
// ← → 列の移動
constexpr wchar_t kColumnHint[] = L"\u2190 \u2192 \u5217\u306E\u79FB\u52D5";
// B-03: the related emoji column right of the candidates.
constexpr float kEmojiColumnWidth = kNumberLeft + kNumberWidth + 34.0f;

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
    column_left_.clear();
    column_width_.clear();
    float left = kPadding;
    for (std::size_t first = page_begin_; first < page_end_; first += kColumnRows) {
        const std::size_t last = std::min(first + kColumnRows, page_end_);
        float widest = 0.0f;
        float mark_width = 0.0f;
        for (std::size_t i = first; i < last; ++i) {
            ComPtr<IDWriteTextLayout> layout;
            const std::u16string& text = candidates_[i];
            if (SUCCEEDED(DWrite()->CreateTextLayout(Wide(text), static_cast<UINT32>(text.size()),
                                                     text_format_.Get(), kMaxTextWidth, kRowHeight,
                                                     layout.GetAddressOf()))) {
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                    widest = std::max(widest, std::min(metrics.widthIncludingTrailingWhitespace, kMaxTextWidth));
                }
            }
            mark_width = std::max(mark_width, MarkOf(i) == Mark::Typo      ? kTypoLabelWidth
                                              : MarkOf(i) == Mark::Learned ? kMarkWidth
                                                                           : 0.0f);
        }
        const float width = std::max(kMinWidth - kPadding * 2,
                                     kNumberLeft + kNumberWidth + widest + kRightPadding + mark_width);
        column_left_.push_back(left);
        column_width_.push_back(width);
        left += width;
    }
    emoji_left_ = left;
    emoji_width_ = emoji_.empty() ? 0.0f : kEmojiColumnWidth;
    left += emoji_width_;
    float width = left + kPadding;
    if (has_selection_ && (Learned(selected_) || column_left_.size() > 1)) {
        width = std::max(width, kPadding * 2 + kHintWidth + 60.0f);
    }
    // The last column takes what the footer added.
    if (!emoji_.empty()) {
        emoji_width_ += width - left - kPadding;
    } else if (!column_width_.empty()) {
        column_width_.back() += width - left - kPadding;
    }
    const std::size_t rows = std::max(std::min(kColumnRows, page_end_ - page_begin_), emoji_.size());
    const float height = kPadding * 2 + static_cast<float>(rows) * kRowHeight + kFooterHeight;
    return SIZE{static_cast<LONG>(std::ceil(width)), static_cast<LONG>(std::ceil(height))};
}

void CandidateWindow::Show(const std::vector<std::u16string>& candidates, std::size_t selected, const RECT& anchor,
                          const std::vector<Mark>& marks, const std::vector<std::u16string>& emoji,
                          std::size_t emoji_selected)
{
    if (candidates.empty() || !EnsureFormats()) {
        Hide();
        return;
    }
    candidates_ = candidates;
    marks_ = marks;
    emoji_.assign(emoji.begin(), emoji.begin() + static_cast<std::ptrdiff_t>(std::min(emoji.size(), kColumnRows)));
    emoji_selected_ = emoji_selected < emoji_.size() ? emoji_selected : kNoSelection;
    has_selection_ = selected != kNoSelection;
    selected_ = has_selection_ ? std::min(selected, candidates_.size() - 1) : 0;
    // Predictions while typing stay in one column of 9.
    const std::size_t page = has_selection_ ? kColumnRows * kColumns : kColumnRows;
    page_begin_ = selected_ / page * page;
    page_end_ = std::min(page_begin_ + page, candidates_.size());
    ApplyBackdrop();
    Place(MeasureDips(), kTextLeft, anchor);
}

void CandidateWindow::Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                             float width)
{
    for (std::size_t i = page_begin_; i < page_end_; ++i) {
        const std::size_t column = (i - page_begin_) / kColumnRows;
        if (column >= column_left_.size()) {
            break;
        }
        const float left = column_left_[column];
        const float right = left + column_width_[column];
        const float top = kPadding + static_cast<float>((i - page_begin_) % kColumnRows) * kRowHeight;
        const bool selected = has_selection_ && i == selected_ && emoji_selected_ == kNoSelection;
        if (selected) {
            brush->SetColor(palette.highlight);
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(left, top, right, top + kRowHeight), 4.0f, 4.0f), brush);
        }
        const wchar_t number[2] = {static_cast<wchar_t>(L'1' + (i - page_begin_) % kColumnRows), L'\0'};
        brush->SetColor(selected ? palette.highlight_text : palette.secondary);
        target->DrawTextW(number, 1, small_format_.Get(),
                          D2D1::RectF(left + kNumberLeft, top, left + kNumberLeft + kNumberWidth, top + kRowHeight),
                          brush);
        brush->SetColor(selected ? palette.highlight_text : palette.text);
        const std::u16string& text = candidates_[i];
        const Mark row_mark = MarkOf(i);
        const float mark = row_mark == Mark::Typo ? kTypoLabelWidth : row_mark == Mark::Learned ? kMarkWidth : 0.0f;
        const float text_left = left + kNumberLeft + kNumberWidth;
        target->DrawTextW(Wide(text), static_cast<UINT32>(text.size()), text_format_.Get(),
                          D2D1::RectF(text_left, top, right - kRightPadding / 2 - mark, top + kRowHeight), brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        if (row_mark != Mark::None) {
            const wchar_t* label = row_mark == Mark::Typo ? kTypoLabel : kHistoryMark;
            const UINT32 length = row_mark == Mark::Typo ? static_cast<UINT32>(std::size(kTypoLabel) - 1) : 1;
            brush->SetColor(selected ? palette.highlight_text : palette.secondary);
            small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            target->DrawTextW(label, length, small_format_.Get(),
                              D2D1::RectF(right - mark, top, right - 4.0f, top + kRowHeight), brush);
            small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }
    const std::wstring footer = has_selection_
                                    ? std::to_wstring(selected_ + 1) + L" / " + std::to_wstring(candidates_.size())
                                    : std::wstring(L"Tab \u2192 \u9078\u629E");
    const std::size_t rows = std::max(std::min(kColumnRows, page_end_ - page_begin_), emoji_.size());
    if (!emoji_.empty()) {
        brush->SetColor(palette.secondary);
        brush->SetOpacity(0.35f);
        target->DrawLine(D2D1::Point2F(emoji_left_, kPadding + 4.0f),
                         D2D1::Point2F(emoji_left_, kPadding + static_cast<float>(rows) * kRowHeight - 4.0f), brush,
                         1.0f);
        brush->SetOpacity(1.0f);
    }
    for (std::size_t j = 0; j < emoji_.size(); ++j) {
        const float top = kPadding + static_cast<float>(j) * kRowHeight;
        const float right = emoji_left_ + emoji_width_;
        const bool selected = j == emoji_selected_;
        if (selected) {
            brush->SetColor(palette.highlight);
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(emoji_left_ + 2.0f, top, right, top + kRowHeight), 4.0f, 4.0f), brush);
        }
        const wchar_t number[2] = {static_cast<wchar_t>(L'1' + j), L'\0'};
        brush->SetColor(selected ? palette.highlight_text : palette.secondary);
        target->DrawTextW(number, 1, small_format_.Get(),
                          D2D1::RectF(emoji_left_ + kNumberLeft, top, emoji_left_ + kNumberLeft + kNumberWidth,
                                      top + kRowHeight),
                          brush);
        brush->SetColor(selected ? palette.highlight_text : palette.text);
        target->DrawTextW(Wide(emoji_[j]), static_cast<UINT32>(emoji_[j].size()), text_format_.Get(),
                          D2D1::RectF(emoji_left_ + kNumberLeft + kNumberWidth, top, right, top + kRowHeight), brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    const float footer_top = kPadding + static_cast<float>(rows) * kRowHeight;
    brush->SetColor(palette.secondary);
    const wchar_t* hint = has_selection_ && Learned(selected_) ? kForgetHint
                          : has_selection_ && column_left_.size() > 1 ? kColumnHint
                                                                      : nullptr;
    if (hint != nullptr) {
        target->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), small_format_.Get(),
                          D2D1::RectF(kPadding + 4.0f, footer_top, width - kPadding, footer_top + kFooterHeight),
                          brush);
    }
    small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    target->DrawTextW(footer.c_str(), static_cast<UINT32>(footer.size()), small_format_.Get(),
                      D2D1::RectF(kPadding, footer_top, width - kPadding - 4.0f, footer_top + kFooterHeight), brush);
    small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void CandidateWindow::OnClick(float x, float y)
{
    if (emoji_.empty() || !on_emoji_click_ || x < emoji_left_ || x > emoji_left_ + emoji_width_ || y < kPadding) {
        return;
    }
    const auto row = static_cast<std::size_t>((y - kPadding) / kRowHeight);
    if (row < emoji_.size()) {
        on_emoji_click_(row);
    }
}

} // namespace astelio::tip
