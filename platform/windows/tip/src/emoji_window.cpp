#include "emoji_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace astelio::tip {
namespace {

// Layout in device-independent pixels.
constexpr float kPadding = 6.0f;
constexpr float kSearchHeight = 34.0f;
constexpr float kTabHeight = 32.0f;
constexpr float kGridGap = 4.0f;
constexpr float kCell = 36.0f;
constexpr float kFooterHeight = 22.0f;
constexpr std::size_t kColumns = InputSession::kEmojiColumns;
constexpr std::size_t kRows = InputSession::kEmojiRows;
constexpr float kWidth = kPadding * 2 + kCell * static_cast<float>(kColumns);
constexpr float kGridTop = kPadding + kSearchHeight + kTabHeight + kGridGap;
constexpr float kFooterTop = kGridTop + kCell * static_cast<float>(kRows);
constexpr float kHeight = kFooterTop + kFooterHeight + kPadding;
constexpr float kTabWidth = (kWidth - kPadding * 2) / static_cast<float>(kEmojiCategoryCount);

// Tab icons and names in EmojiCategory order.
constexpr std::array<const wchar_t*, kEmojiCategoryCount> kTabIcons = {
    L"\U0001F558", L"\U0001F600", L"\U0001F44B", L"\U0001F436", L"\U0001F34E",
    L"\u26BD",     L"\U0001F697", L"\U0001F4A1", L"\u2764\uFE0F", L"\U0001F3C1",
};
constexpr std::array<const wchar_t*, kEmojiCategoryCount> kCategoryNames = {
    L"履歴", L"顔", L"人", L"自然", L"食べ物", L"活動", L"旅行", L"物", L"記号", L"旗",
};

const D2D1_DRAW_TEXT_OPTIONS kColorText = D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;

void DrawString(ID2D1RenderTarget* target, const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                ID2D1SolidColorBrush* brush)
{
    target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush, kColorText);
}

} // namespace

bool EmojiWindow::EnsureFormats()
{
    if (!EnsureWindow()) {
        return false;
    }
    if (emoji_format_ && tab_format_ && text_format_ && small_format_) {
        return true;
    }
    const auto create = [this](const wchar_t* family, float size, Microsoft::WRL::ComPtr<IDWriteTextFormat>& format,
                               DWRITE_TEXT_ALIGNMENT alignment) {
        if (FAILED(DWrite()->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, L"ja-JP",
                                              format.ReleaseAndGetAddressOf()))) {
            return false;
        }
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetTextAlignment(alignment);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return true;
    };
    return create(L"Segoe UI Emoji", 22.0f, emoji_format_, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           create(L"Segoe UI Emoji", 16.0f, tab_format_, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           create(L"Yu Gothic UI", 14.0f, text_format_, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           create(L"Yu Gothic UI", 12.0f, small_format_, DWRITE_TEXT_ALIGNMENT_LEADING);
}

void EmojiWindow::Show(const EmojiPaletteView& view, const RECT& anchor)
{
    if (!EnsureFormats()) {
        Hide();
        return;
    }
    view_ = view;
    ApplyBackdrop();
    Place(SIZE{static_cast<LONG>(std::ceil(kWidth)), static_cast<LONG>(std::ceil(kHeight))}, kPadding, anchor);
}

std::size_t EmojiWindow::FirstVisible() const
{
    constexpr std::size_t page = kColumns * kRows;
    return view_.selected == kNoEmojiSelection ? 0 : view_.selected / page * page;
}

void EmojiWindow::Render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Palette& palette,
                         float width)
{
    // Search row.
    const D2D1_RECT_F search = D2D1::RectF(kPadding, kPadding, width - kPadding, kPadding + kSearchHeight - 4.0f);
    D2D1_COLOR_F field = palette.text;
    field.a = 0.08f;
    brush->SetColor(field);
    target->FillRoundedRectangle(D2D1::RoundedRect(search, 4.0f, 4.0f), brush);
    const D2D1_RECT_F search_text = D2D1::RectF(search.left + 8.0f, search.top, search.right - 8.0f, search.bottom);
    if (view_.query.empty()) {
        brush->SetColor(palette.secondary);
        DrawString(target, view_.active ? L"\U0001F50D 読みで検索（例: いぬ）" : L"\U0001F50D Tab か ↓ で絵文字を選ぶ",
                   text_format_.Get(), search_text, brush);
    } else {
        brush->SetColor(palette.text);
        DrawString(target, L"\U0001F50D " + std::wstring(Wide(view_.query)), text_format_.Get(), search_text, brush);
    }

    // Category tabs.
    const float tab_top = kPadding + kSearchHeight;
    for (std::size_t i = 0; i < kEmojiCategoryCount; ++i) {
        const float left = kPadding + kTabWidth * static_cast<float>(i);
        const D2D1_RECT_F tab = D2D1::RectF(left, tab_top, left + kTabWidth, tab_top + kTabHeight);
        const bool current = view_.query.empty() && static_cast<std::size_t>(view_.category) == i;
        if (current) {
            brush->SetColor(palette.highlight);
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tab.left + 1.0f, tab.top + 2.0f,
                                                                       tab.right - 1.0f, tab.bottom - 2.0f),
                                                           4.0f, 4.0f),
                                         brush);
        }
        brush->SetColor(palette.text);
        DrawString(target, kTabIcons[i], tab_format_.Get(), tab, brush);
    }

    // Grid.
    const std::size_t first = FirstVisible();
    for (std::size_t cell = 0; cell < kColumns * kRows && first + cell < view_.items.size(); ++cell) {
        const std::size_t index = first + cell;
        const float left = kPadding + kCell * static_cast<float>(cell % kColumns);
        const float top = kGridTop + kCell * static_cast<float>(cell / kColumns);
        const D2D1_RECT_F rect = D2D1::RectF(left, top, left + kCell, top + kCell);
        if (index == view_.selected) {
            brush->SetColor(palette.highlight);
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(rect.left + 1.0f, rect.top + 1.0f, rect.right - 1.0f, rect.bottom - 1.0f),
                                  6.0f, 6.0f),
                brush);
        }
        brush->SetColor(palette.text);
        DrawString(target, std::wstring(Wide(view_.items[index])), emoji_format_.Get(), rect, brush);
    }
    if (view_.items.empty()) {
        const wchar_t* message = !view_.query.empty()                          ? L"見つかりません"
                                 : view_.category == EmojiCategory::Recent ? L"使った絵文字がここに並びます"
                                                                             : L"絵文字がありません";
        brush->SetColor(palette.secondary);
        text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawString(target, message, text_format_.Get(),
                   D2D1::RectF(kPadding, kGridTop, width - kPadding, kFooterTop), brush);
        text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // Footer: keys on the left, the category and position on the right.
    const D2D1_RECT_F footer = D2D1::RectF(kPadding + 4.0f, kFooterTop, width - kPadding - 4.0f, kFooterTop + kFooterHeight);
    brush->SetColor(palette.secondary);
    DrawString(target, view_.active ? L"Enter 決定  Tab 分類  Esc 戻る" : L"Space 変換", small_format_.Get(), footer,
               brush);
    std::wstring position = view_.query.empty() ? kCategoryNames[static_cast<std::size_t>(view_.category)] : L"検索";
    if (view_.selected != kNoEmojiSelection) {
        position += L"  " + std::to_wstring(view_.selected + 1) + L" / " + std::to_wstring(view_.items.size());
    }
    small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    DrawString(target, position, small_format_.Get(), footer, brush);
    small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void EmojiWindow::OnClick(float x, float y)
{
    if (!on_click_ || x < kPadding || x >= kWidth - kPadding) {
        return;
    }
    const float tab_top = kPadding + kSearchHeight;
    if (y >= tab_top && y < tab_top + kTabHeight) {
        const auto index = static_cast<std::size_t>((x - kPadding) / kTabWidth);
        on_click_({true, std::min(index, kEmojiCategoryCount - 1)});
        return;
    }
    if (y >= kGridTop && y < kFooterTop) {
        const auto column = static_cast<std::size_t>((x - kPadding) / kCell);
        const auto row = static_cast<std::size_t>((y - kGridTop) / kCell);
        const std::size_t index = FirstVisible() + row * kColumns + std::min(column, kColumns - 1);
        if (row < kRows && index < view_.items.size()) {
            on_click_({false, index});
        }
    }
}

} // namespace astelio::tip
