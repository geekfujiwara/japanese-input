#include "astelio/kana_forms.h"

#include <algorithm>
#include <array>
#include <map>

namespace astelio {
namespace {

struct HalfWidth {
    char16_t base;
    char16_t mark; // 0, U+FF9E (dakuten) or U+FF9F (handakuten)
};

constexpr char16_t kDakuten = u'\uFF9E';
constexpr char16_t kHandakuten = u'\uFF9F';

// Katakana U+30A1 (ァ) to U+30F6 (ヶ).
constexpr std::array<HalfWidth, 86> kHalfWidthKatakana = {{
    {u'\uFF67', 0}, {u'\uFF71', 0}, {u'\uFF68', 0}, {u'\uFF72', 0}, {u'\uFF69', 0}, {u'\uFF73', 0},
    {u'\uFF6A', 0}, {u'\uFF74', 0}, {u'\uFF6B', 0}, {u'\uFF75', 0},                   // ァ-オ
    {u'\uFF76', 0}, {u'\uFF76', kDakuten}, {u'\uFF77', 0}, {u'\uFF77', kDakuten},     // カガキギ
    {u'\uFF78', 0}, {u'\uFF78', kDakuten}, {u'\uFF79', 0}, {u'\uFF79', kDakuten},     // クグケゲ
    {u'\uFF7A', 0}, {u'\uFF7A', kDakuten},                                            // コゴ
    {u'\uFF7B', 0}, {u'\uFF7B', kDakuten}, {u'\uFF7C', 0}, {u'\uFF7C', kDakuten},     // サザシジ
    {u'\uFF7D', 0}, {u'\uFF7D', kDakuten}, {u'\uFF7E', 0}, {u'\uFF7E', kDakuten},     // スズセゼ
    {u'\uFF7F', 0}, {u'\uFF7F', kDakuten},                                            // ソゾ
    {u'\uFF80', 0}, {u'\uFF80', kDakuten}, {u'\uFF81', 0}, {u'\uFF81', kDakuten},     // タダチヂ
    {u'\uFF6F', 0}, {u'\uFF82', 0}, {u'\uFF82', kDakuten},                            // ッツヅ
    {u'\uFF83', 0}, {u'\uFF83', kDakuten}, {u'\uFF84', 0}, {u'\uFF84', kDakuten},     // テデトド
    {u'\uFF85', 0}, {u'\uFF86', 0}, {u'\uFF87', 0}, {u'\uFF88', 0}, {u'\uFF89', 0},   // ナ-ノ
    {u'\uFF8A', 0}, {u'\uFF8A', kDakuten}, {u'\uFF8A', kHandakuten},                  // ハバパ
    {u'\uFF8B', 0}, {u'\uFF8B', kDakuten}, {u'\uFF8B', kHandakuten},                  // ヒビピ
    {u'\uFF8C', 0}, {u'\uFF8C', kDakuten}, {u'\uFF8C', kHandakuten},                  // フブプ
    {u'\uFF8D', 0}, {u'\uFF8D', kDakuten}, {u'\uFF8D', kHandakuten},                  // ヘベペ
    {u'\uFF8E', 0}, {u'\uFF8E', kDakuten}, {u'\uFF8E', kHandakuten},                  // ホボポ
    {u'\uFF8F', 0}, {u'\uFF90', 0}, {u'\uFF91', 0}, {u'\uFF92', 0}, {u'\uFF93', 0},   // マ-モ
    {u'\uFF6C', 0}, {u'\uFF94', 0}, {u'\uFF6D', 0}, {u'\uFF95', 0}, {u'\uFF6E', 0},
    {u'\uFF96', 0},                                                                   // ャ-ヨ
    {u'\uFF97', 0}, {u'\uFF98', 0}, {u'\uFF99', 0}, {u'\uFF9A', 0}, {u'\uFF9B', 0},   // ラ-ロ
    {u'\uFF9C', 0}, {u'\uFF9C', 0}, {u'\uFF72', 0}, {u'\uFF74', 0}, {u'\uFF66', 0},   // ヮワヰヱヲ
    {u'\uFF9D', 0}, {u'\uFF73', kDakuten}, {u'\uFF76', 0}, {u'\uFF79', 0},            // ンヴヵヶ
}};

char16_t HalfWidthSymbol(char16_t c)
{
    switch (c) {
    case u'\u30FC': return u'\uFF70'; // ー
    case u'\u30FB': return u'\uFF65'; // ・
    case u'\u300C': return u'\uFF62'; // 「
    case u'\u300D': return u'\uFF63'; // 」
    case u'\u3001': return u'\uFF64'; // 、
    case u'\u3002': return u'\uFF61'; // 。
    case u'\u309B': return kDakuten;
    case u'\u309C': return kHandakuten;
    default: return 0;
    }
}

bool IsVowelOrY(char16_t c)
{
    return c == u'a' || c == u'i' || c == u'u' || c == u'e' || c == u'o' || c == u'y' || c == u'n';
}

bool IsConsonant(char16_t c)
{
    return c >= u'a' && c <= u'z' && !IsVowelOrY(c);
}

// Hepburn spellings win over the Kunrei ones in the default table.
bool PreferredSpelling(std::u16string_view input)
{
    static constexpr std::u16string_view kHepburn[] = {u"shi", u"chi", u"tsu", u"fu",  u"ji",  u"sha", u"shu",
                                                       u"sho", u"cha", u"chu", u"cho", u"ja",  u"ju",  u"jo"};
    for (const std::u16string_view spelling : kHepburn) {
        if (input == spelling) {
            return true;
        }
    }
    return false;
}

int SpellingScore(std::u16string_view input)
{
    int score = static_cast<int>(input.size());
    if (input.front() == u'x' || input.front() == u'l') {
        score += 100; // small kana spelled explicitly
    }
    if (input.find(u'\'') != std::u16string_view::npos) {
        score += 50;
    }
    if ((input.front() == u'c' || input.front() == u'q') && !PreferredSpelling(input)) {
        score += 20; // "ca", "qa": accepted when typed, but "ka" is the usual spelling
    }
    if (PreferredSpelling(input)) {
        score -= 10;
    }
    return score;
}

} // namespace

std::u16string ToHalfWidthKatakana(std::u16string_view text)
{
    std::u16string result;
    for (char16_t c : text) {
        if (c >= u'\u3041' && c <= u'\u3096') {
            c = static_cast<char16_t>(c + 0x60); // hiragana -> katakana
        }
        if (c >= u'\u30A1' && c <= u'\u30F6') {
            const HalfWidth& half = kHalfWidthKatakana[c - u'\u30A1'];
            result.push_back(half.base);
            if (half.mark != 0) {
                result.push_back(half.mark);
            }
        } else if (const char16_t symbol = HalfWidthSymbol(c); symbol != 0) {
            result.push_back(symbol);
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::u16string ToFullWidthAscii(std::u16string_view text)
{
    std::u16string result(text);
    for (char16_t& c : result) {
        if (c >= 0x21 && c <= 0x7E) {
            c = static_cast<char16_t>(c + 0xFEE0);
        } else if (c == u' ') {
            c = u'\u3000';
        }
    }
    return result;
}

std::u16string KanaToRomaji(std::u16string_view text, const RomajiTable& table)
{
    // kana output -> spelling, for rules that finish on their own (no pending input).
    std::map<std::u16string, std::u16string, std::less<>> spellings;
    std::size_t longest = 1;
    for (const auto& [input, rule] : table.rules()) {
        if (rule.output.empty() || !rule.pending.empty() || input.empty()) {
            continue;
        }
        const auto found = spellings.find(rule.output);
        if (found == spellings.end() || SpellingScore(input) < SpellingScore(found->second)) {
            spellings[rule.output] = input;
        }
        longest = std::max(longest, rule.output.size());
    }

    std::u16string result;
    bool sokuon = false;
    for (std::size_t i = 0; i < text.size();) {
        std::u16string_view spelling;
        std::size_t length = std::min(longest, text.size() - i);
        for (; length > 0; --length) {
            const auto found = spellings.find(text.substr(i, length));
            if (found != spellings.end()) {
                spelling = found->second;
                break;
            }
        }
        if (length == 0) {
            if (sokuon) {
                result += u"xtu";
                sokuon = false;
            }
            result.push_back(text[i]);
            ++i;
            continue;
        }
        const std::u16string_view kana = text.substr(i, length);
        if (kana == u"\u3063") { // っ doubles the next consonant
            if (sokuon) {
                result += u"xtu";
            }
            sokuon = true;
            i += length;
            continue;
        }
        if (sokuon) {
            result += IsConsonant(spelling.front()) ? std::u16string(1, spelling.front()) : std::u16string(u"xtu");
            sokuon = false;
        }
        if (kana == u"\u3093") { // ん before a vowel, y or n needs "nn"
            const bool ambiguous = i + 1 < text.size() && [&] {
                for (std::size_t next = std::min(longest, text.size() - i - 1); next > 0; --next) {
                    const auto found = spellings.find(text.substr(i + 1, next));
                    if (found != spellings.end()) {
                        return IsVowelOrY(found->second.front());
                    }
                }
                return false;
            }();
            result += ambiguous ? u"nn" : u"n";
        } else {
            result += spelling;
        }
        i += length;
    }
    if (sokuon) {
        result += u"xtu";
    }
    return result;
}

} // namespace astelio
