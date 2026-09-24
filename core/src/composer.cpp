#include "astelio/composer.h"

#include <utility>

namespace astelio {
namespace {

bool IsUpper(char16_t c) { return c >= u'A' && c <= u'Z'; }

bool IsPrintableAscii(char16_t c) { return c >= 0x21 && c <= 0x7E; }

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

    if (temporary_alphanumeric_) {
        before_ += AlphanumericModeCharacter(key, settings_);
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
    before_ += KanaModeCharacter(key, settings_);
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
    if (!pending_.empty()) {
        pending_.pop_back();
    } else if (!before_.empty()) {
        before_.pop_back();
    }
}

void Composer::Delete()
{
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
}

void Composer::MoveRight()
{
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
}

void Composer::SetText(std::u16string text)
{
    Clear();
    before_ = std::move(text);
}

} // namespace astelio
