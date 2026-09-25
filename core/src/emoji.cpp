#include "astelio/emoji.h"

#include <algorithm>
#include <unordered_map>

namespace astelio {
namespace {

std::vector<char32_t> CodePoints(std::u16string_view text)
{
    std::vector<char32_t> points;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
            text[i + 1] <= 0xDFFF) {
            points.push_back(0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                             (static_cast<char32_t>(text[i + 1]) - 0xDC00));
            ++i;
        } else {
            points.push_back(unit);
        }
    }
    return points;
}

bool In(char32_t c, char32_t first, char32_t last)
{
    return c >= first && c <= last;
}

bool IsJoinerOrModifier(char32_t c)
{
    return c == 0x200D || c == 0xFE0F || c == 0xFE0E || c == 0x20E3 || In(c, 0x1F3FB, 0x1F3FF) ||
           In(c, 0xE0020, 0xE007F);
}

bool IsPictograph(char32_t c)
{
    return In(c, 0x1F000, 0x1FAFF) || In(c, 0x2600, 0x27BF) || In(c, 0x2B00, 0x2BFF) || In(c, 0x231A, 0x23FF) ||
           c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299;
}

bool IsPeople(char32_t c)
{
    return In(c, 0x1F440, 0x1F450) || In(c, 0x1F466, 0x1F478) || c == 0x1F47C || In(c, 0x1F481, 0x1F487) ||
           c == 0x1F48F || c == 0x1F491 || In(c, 0x1F574, 0x1F57A) || c == 0x1F590 || In(c, 0x1F595, 0x1F596) ||
           In(c, 0x1F645, 0x1F64F) || In(c, 0x1F6B4, 0x1F6B6) || c == 0x1F6C0 || In(c, 0x1F90C, 0x1F90F) ||
           In(c, 0x1F918, 0x1F91F) || c == 0x1F926 || In(c, 0x1F930, 0x1F939) || In(c, 0x1F93C, 0x1F93E) ||
           In(c, 0x1F9B0, 0x1F9B9) || c == 0x1F9BB || In(c, 0x1F9CD, 0x1F9CF) || In(c, 0x1F9D1, 0x1F9DF) ||
           In(c, 0x1FAC3, 0x1FAC5) || In(c, 0x1FAF0, 0x1FAF8) || c == 0x261D || c == 0x26F9 ||
           In(c, 0x270A, 0x270D);
}

bool IsSmiley(char32_t c)
{
    return In(c, 0x1F600, 0x1F644) || In(c, 0x1F910, 0x1F92F) || In(c, 0x1F970, 0x1F97A) || c == 0x1F9D0 ||
           In(c, 0x1F479, 0x1F47B) || In(c, 0x1F47D, 0x1F480) || c == 0x1F4A9 || In(c, 0x1F493, 0x1F49F) ||
           c == 0x1F5A4 || In(c, 0x1F90D, 0x1F90E) || c == 0x1F9E1 || In(c, 0x2763, 0x2764) || c == 0x263A ||
           c == 0x2639 || In(c, 0x1FAE0, 0x1FAE8);
}

bool IsNature(char32_t c)
{
    return In(c, 0x1F400, 0x1F43F) || In(c, 0x1F980, 0x1F9AE) || In(c, 0x1F300, 0x1F30C) ||
           In(c, 0x1F311, 0x1F32C) || In(c, 0x1F330, 0x1F344) || c == 0x1F490 || c == 0x1F4AE ||
           In(c, 0x1FAB0, 0x1FABF) || In(c, 0x2600, 0x2604) || c == 0x2614 || c == 0x26A1 ||
           In(c, 0x26C4, 0x26C5) || c == 0x2744 || c == 0x1F54A || In(c, 0x1F577, 0x1F578);
}

bool IsFood(char32_t c)
{
    return In(c, 0x1F345, 0x1F37F) || In(c, 0x1F950, 0x1F96F) || In(c, 0x1F9C0, 0x1F9CB) ||
           In(c, 0x1FAD0, 0x1FADB) || c == 0x2615;
}

bool IsActivity(char32_t c)
{
    return In(c, 0x1F380, 0x1F393) || In(c, 0x1F396, 0x1F39F) || In(c, 0x1F3A0, 0x1F3D3) || c == 0x1F3F8 ||
           c == 0x1F93A || In(c, 0x1F940, 0x1F94F) || In(c, 0x26BD, 0x26BE) || c == 0x26F3 || c == 0x26F8 ||
           c == 0x1F6F7 || c == 0x1F6F9 || c == 0x1F9E7 || c == 0x1F9E9 || c == 0x1F9F8 ||
           In(c, 0x1FA80, 0x1FA86) || c == 0x265F || c == 0x1F004 || c == 0x1F0CF;
}

bool IsTravel(char32_t c)
{
    return In(c, 0x1F680, 0x1F6FF) || In(c, 0x1F3D4, 0x1F3F0) || In(c, 0x1F30D, 0x1F310) ||
           In(c, 0x1F5FA, 0x1F5FF) || In(c, 0x26E9, 0x26FA) || c == 0x2708 || c == 0x26F5 || c == 0x26FD;
}

bool IsObject(char32_t c)
{
    return (In(c, 0x1F4A0, 0x1F4FF) && c != 0x1F4AF) || In(c, 0x1F507, 0x1F53D) || In(c, 0x1F56F, 0x1F5F9) ||
           In(c, 0x231A, 0x23FF) || c == 0x260E || In(c, 0x1F9F0, 0x1F9FF) || In(c, 0x1FA70, 0x1FAFF);
}

} // namespace

bool IsEmoji(std::u16string_view text)
{
    if (text.empty() || text.size() > 32) {
        return false;
    }
    // Most dictionary words start with kana or kanji; reject them before decoding.
    const char16_t lead = text.front();
    if (!(lead >= 0xD83C && lead <= 0xD83E) && !(lead >= 0x2300 && lead <= 0x2BFF) && lead != 0x3030 && lead != 0x303D && lead != 0x3297 && lead != 0x3299 &&
        !(lead >= u'0' && lead <= u'9') && lead != u'#' && lead != u'*') {
        return false;
    }
    const std::vector<char32_t> points = CodePoints(text);
    const bool keycap = std::find(points.begin(), points.end(), char32_t{0x20E3}) != points.end();
    const char32_t first = points.front();
    if (!IsPictograph(first) && !(keycap && (In(first, U'0', U'9') || first == U'#' || first == U'*'))) {
        return false;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (!IsPictograph(points[i]) && !IsJoinerOrModifier(points[i])) {
            return false;
        }
    }
    return true;
}

EmojiCategory CategorizeEmoji(std::u16string_view text)
{
    const std::vector<char32_t> points = CodePoints(text);
    if (points.empty()) {
        return EmojiCategory::Symbols;
    }
    const char32_t c = points.front();
    if (In(c, 0x1F1E6, 0x1F1FF) || c == 0x1F3F4 || c == 0x1F3C1 || c == 0x1F6A9 || c == 0x1F38C) {
        return EmojiCategory::Flags;
    }
    if (IsPeople(c)) {
        return EmojiCategory::People;
    }
    if (IsSmiley(c)) {
        return EmojiCategory::Smileys;
    }
    if (IsNature(c)) {
        return EmojiCategory::Nature;
    }
    if (IsFood(c)) {
        return EmojiCategory::Food;
    }
    if (IsActivity(c)) {
        return EmojiCategory::Activities;
    }
    if (IsTravel(c)) {
        return EmojiCategory::Travel;
    }
    if (IsObject(c)) {
        return EmojiCategory::Objects;
    }
    return EmojiCategory::Symbols;
}

EmojiCatalog EmojiCatalog::FromDictionary(const SystemDictionary& dictionary)
{
    EmojiCatalog catalog;
    dictionary.ForEachEntry([&](std::u16string_view reading, const DictionaryEntry& entry) {
        if (IsEmoji(entry.surface)) {
            catalog.Add(entry.surface, reading);
        }
    });
    catalog.Finish();
    return catalog;
}

void EmojiCatalog::Add(std::u16string_view text, std::u16string_view reading)
{
    const auto found = index_.find(std::u16string(text));
    if (found != index_.end()) {
        Emoji& emoji = emoji_[found->second];
        if (std::find(emoji.readings.begin(), emoji.readings.end(), reading) == emoji.readings.end()) {
            emoji.readings.emplace_back(reading);
        }
        return;
    }
    index_.emplace(std::u16string(text), emoji_.size());
    Emoji emoji;
    emoji.text = std::u16string(text);
    emoji.readings.emplace_back(reading);
    emoji.category = CategorizeEmoji(text);
    emoji_.push_back(std::move(emoji));
}

void EmojiCatalog::Finish()
{
    for (auto& category : categories_) {
        category.clear();
    }
    for (std::size_t i = 0; i < emoji_.size(); ++i) {
        categories_[static_cast<std::size_t>(emoji_[i].category)].push_back(i);
    }
    for (auto& category : categories_) {
        std::sort(category.begin(), category.end(),
                  [this](std::size_t a, std::size_t b) { return CodePoints(emoji_[a].text) < CodePoints(emoji_[b].text); });
    }
}

const std::vector<std::size_t>& EmojiCatalog::InCategory(EmojiCategory category) const
{
    return categories_[static_cast<std::size_t>(category)];
}

std::vector<std::size_t> EmojiCatalog::Search(std::u16string_view query, std::size_t limit) const
{
    std::vector<std::size_t> starts;
    std::vector<std::size_t> contains;
    if (query.empty()) {
        return starts;
    }
    for (std::size_t i = 0; i < emoji_.size(); ++i) {
        bool prefix = false;
        bool inside = false;
        for (const std::u16string& reading : emoji_[i].readings) {
            prefix = prefix || reading.starts_with(query);
            inside = inside || reading.find(query) != std::u16string::npos;
        }
        if (prefix) {
            starts.push_back(i);
        } else if (inside) {
            contains.push_back(i);
        }
    }
    starts.insert(starts.end(), contains.begin(), contains.end());
    if (starts.size() > limit) {
        starts.resize(limit);
    }
    return starts;
}

} // namespace astelio
