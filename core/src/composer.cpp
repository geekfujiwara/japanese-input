#include "astelio/composer.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace astelio {
namespace {

bool IsUpper(char16_t c) { return c >= u'A' && c <= u'Z'; }

bool IsPrintableAscii(char16_t c) { return c >= 0x21 && c <= 0x7E; }

bool IsDigit(char16_t c) { return c >= u'0' && c <= u'9'; }

// Digits only (half or full width), at least one.
bool IsNumber(std::u16string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char16_t c) {
        return IsDigit(c) || (c >= u'\uFF10' && c <= u'\uFF19');
    });
}

// Guards against tables whose pending text never shrinks.
constexpr int kMaxResolveSteps = 256;

} // namespace

Composer::Composer(const RomajiTable& table, CharacterSettings settings)
    : table_(&table), settings_(std::move(settings))
{
}

void Composer::InsertKey(char16_t key)
{
    if (!IsPrintableAscii(key)) {
        return;
    }
    // R-10: a digit right after the ". " of a list number makes it a decimal point ("3.14").
    const bool after_list_period = list_period_;
    list_period_ = false;
    if (after_list_period && IsDigit(key) && !before_.empty() && before_.back() == u' ') {
        before_.pop_back();
    }

    if (temporary_alphanumeric_) {
        InsertSymbol(AlphanumericModeCharacter(key, settings_));
        return;
    }

    if (IsUpper(key)) {
        ResolvePending(true);
        temporary_alphanumeric_ = true;
        before_ += AlphanumericModeCharacter(key, settings_);
        return;
    }

    std::u16string candidate = pending_ + key;
    if (table_->HasRuleStartingWith(candidate) || (key >= u'a' && key <= u'z')) {
        pending_ = std::move(candidate);
        ResolvePending(false);
        return;
    }

    ResolvePending(true);
    if (key == u'.' && settings_.list_number_period && after_.empty() && IsNumber(before_)) {
        before_ += u". ";
        list_period_ = true;
        return;
    }
    InsertSymbol(KanaModeCharacter(key, settings_));
}

void Composer::InsertSymbol(std::u16string text)
{
    if (settings_.auto_close_brackets && text.size() == 1) {
        if (IsClosingBracket(text.front()) && !after_.empty() && after_.front() == text.front()) {
            before_ += text;
            after_.erase(0, 1);
            return;
        }
        if (const char16_t closing = ClosingBracket(text.front()); closing != 0) {
            before_ += text;
            after_.insert(after_.begin(), closing);
            return;
        }
    }
    before_ += text;
}

void Composer::ResolvePending(bool flush)
{
    for (int step = 0; step < kMaxResolveSteps && !pending_.empty(); ++step) {
        if (!flush && table_->HasLongerRule(pending_)) {
            return;
        }
        if (const RomajiRule* rule = table_->FindExact(pending_)) {
            before_ += rule->output;
            pending_ = rule->pending;
            continue;
        }
        const RomajiRule* prefix_rule = nullptr;
        std::size_t prefix_length = pending_.size() - 1;
        for (; prefix_length > 0; --prefix_length) {
            prefix_rule = table_->FindExact(std::u16string_view{pending_}.substr(0, prefix_length));
            if (prefix_rule != nullptr) {
                break;
            }
        }
        if (prefix_rule != nullptr) {
            before_ += prefix_rule->output;
            pending_ = prefix_rule->pending + pending_.substr(prefix_length);
            continue;
        }
        before_ += ToWidth(pending_.front(), settings_.letters);
        pending_.erase(0, 1);
    }
    if (flush && !pending_.empty()) {
        for (char16_t c : pending_) {
            before_ += ToWidth(c, settings_.letters);
        }
        pending_.clear();
    }
}

void Composer::Backspace()
{
    list_period_ = false;
    if (!pending_.empty()) {
        pending_.pop_back();
    } else if (!before_.empty()) {
        // R-11: an empty pair of brackets goes as a whole.
        if (settings_.auto_close_brackets && !after_.empty() && ClosingBracket(before_.back()) == after_.front()) {
            after_.erase(0, 1);
        }
        before_.pop_back();
    }
}

void Composer::Delete()
{
    list_period_ = false;
    ResolvePending(true);
    if (!after_.empty()) {
        after_.erase(0, 1);
    }
}

void Composer::MoveLeft()
{
    ResolvePending(true);
    if (!before_.empty()) {
        after_.insert(after_.begin(), before_.back());
        before_.pop_back();
    }
    list_period_ = false;
}

void Composer::MoveRight()
{
    list_period_ = false;
    ResolvePending(true);
    if (!after_.empty()) {
        before_.push_back(after_.front());
        after_.erase(0, 1);
    }
}

void Composer::ExitTemporaryAlphanumeric()
{
    temporary_alphanumeric_ = false;
}

std::u16string Composer::Text() const
{
    return before_ + pending_ + after_;
}

std::size_t Composer::Cursor() const
{
    return before_.size() + pending_.size();
}

bool Composer::Empty() const
{
    return before_.empty() && pending_.empty() && after_.empty();
}

std::u16string Composer::Commit()
{
    ResolvePending(true);
    std::u16string text = before_ + after_;
    Clear();
    return text;
}

void Composer::Clear()
{
    before_.clear();
    pending_.clear();
    after_.clear();
    temporary_alphanumeric_ = false;
    list_period_ = false;
}

void Composer::SetText(std::u16string text)
{
    Clear();
    before_ = std::move(text);
}

} // namespace astelio
