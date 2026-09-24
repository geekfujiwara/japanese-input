#include "astelio/romaji_table.h"

#include <initializer_list>
#include <utility>

namespace astelio {
namespace {

struct Entry {
    const char16_t* input;
    const char16_t* output;
};

constexpr Entry kDefaultEntries[] = {
    {u"a", u"あ"}, {u"i", u"い"}, {u"u", u"う"}, {u"e", u"え"}, {u"o", u"お"},
    {u"ka", u"か"}, {u"ki", u"き"}, {u"ku", u"く"}, {u"ke", u"け"}, {u"ko", u"こ"},
    {u"kya", u"きゃ"}, {u"kyi", u"きぃ"}, {u"kyu", u"きゅ"}, {u"kye", u"きぇ"}, {u"kyo", u"きょ"},
    {u"ca", u"か"}, {u"ci", u"し"}, {u"cu", u"く"}, {u"ce", u"せ"}, {u"co", u"こ"},
    {u"qa", u"くぁ"}, {u"qi", u"くぃ"}, {u"qu", u"く"}, {u"qe", u"くぇ"}, {u"qo", u"くぉ"},
    {u"sa", u"さ"}, {u"si", u"し"}, {u"shi", u"し"}, {u"su", u"す"}, {u"se", u"せ"}, {u"so", u"そ"},
    {u"sya", u"しゃ"}, {u"syi", u"しぃ"}, {u"syu", u"しゅ"}, {u"sye", u"しぇ"}, {u"syo", u"しょ"},
    {u"sha", u"しゃ"}, {u"shu", u"しゅ"}, {u"she", u"しぇ"}, {u"sho", u"しょ"},
    {u"ta", u"た"}, {u"ti", u"ち"}, {u"chi", u"ち"}, {u"tu", u"つ"}, {u"tsu", u"つ"}, {u"te", u"て"}, {u"to", u"と"},
    {u"tya", u"ちゃ"}, {u"tyi", u"ちぃ"}, {u"tyu", u"ちゅ"}, {u"tye", u"ちぇ"}, {u"tyo", u"ちょ"},
    {u"cha", u"ちゃ"}, {u"chu", u"ちゅ"}, {u"che", u"ちぇ"}, {u"cho", u"ちょ"},
    {u"cya", u"ちゃ"}, {u"cyi", u"ちぃ"}, {u"cyu", u"ちゅ"}, {u"cye", u"ちぇ"}, {u"cyo", u"ちょ"},
    {u"tsa", u"つぁ"}, {u"tsi", u"つぃ"}, {u"tse", u"つぇ"}, {u"tso", u"つぉ"},
    {u"tha", u"てゃ"}, {u"thi", u"てぃ"}, {u"thu", u"てゅ"}, {u"the", u"てぇ"}, {u"tho", u"てょ"},
    {u"twu", u"とぅ"},
    {u"na", u"な"}, {u"ni", u"に"}, {u"nu", u"ぬ"}, {u"ne", u"ね"}, {u"no", u"の"},
    {u"nya", u"にゃ"}, {u"nyi", u"にぃ"}, {u"nyu", u"にゅ"}, {u"nye", u"にぇ"}, {u"nyo", u"にょ"},
    {u"n", u"ん"}, {u"nn", u"ん"}, {u"n'", u"ん"}, {u"xn", u"ん"},
    {u"ha", u"は"}, {u"hi", u"ひ"}, {u"hu", u"ふ"}, {u"fu", u"ふ"}, {u"he", u"へ"}, {u"ho", u"ほ"},
    {u"hya", u"ひゃ"}, {u"hyi", u"ひぃ"}, {u"hyu", u"ひゅ"}, {u"hye", u"ひぇ"}, {u"hyo", u"ひょ"},
    {u"fa", u"ふぁ"}, {u"fi", u"ふぃ"}, {u"fe", u"ふぇ"}, {u"fo", u"ふぉ"},
    {u"fya", u"ふゃ"}, {u"fyu", u"ふゅ"}, {u"fyo", u"ふょ"},
    {u"ma", u"ま"}, {u"mi", u"み"}, {u"mu", u"む"}, {u"me", u"め"}, {u"mo", u"も"},
    {u"mya", u"みゃ"}, {u"myi", u"みぃ"}, {u"myu", u"みゅ"}, {u"mye", u"みぇ"}, {u"myo", u"みょ"},
    {u"ya", u"や"}, {u"yu", u"ゆ"}, {u"ye", u"いぇ"}, {u"yo", u"よ"},
    {u"ra", u"ら"}, {u"ri", u"り"}, {u"ru", u"る"}, {u"re", u"れ"}, {u"ro", u"ろ"},
    {u"rya", u"りゃ"}, {u"ryi", u"りぃ"}, {u"ryu", u"りゅ"}, {u"rye", u"りぇ"}, {u"ryo", u"りょ"},
    {u"wa", u"わ"}, {u"wi", u"うぃ"}, {u"wu", u"う"}, {u"we", u"うぇ"}, {u"wo", u"を"},
    {u"wha", u"うぁ"}, {u"whi", u"うぃ"}, {u"whu", u"う"}, {u"whe", u"うぇ"}, {u"who", u"うぉ"},
    {u"ga", u"が"}, {u"gi", u"ぎ"}, {u"gu", u"ぐ"}, {u"ge", u"げ"}, {u"go", u"ご"},
    {u"gya", u"ぎゃ"}, {u"gyi", u"ぎぃ"}, {u"gyu", u"ぎゅ"}, {u"gye", u"ぎぇ"}, {u"gyo", u"ぎょ"},
    {u"za", u"ざ"}, {u"zi", u"じ"}, {u"ji", u"じ"}, {u"zu", u"ず"}, {u"ze", u"ぜ"}, {u"zo", u"ぞ"},
    {u"zya", u"じゃ"}, {u"zyi", u"じぃ"}, {u"zyu", u"じゅ"}, {u"zye", u"じぇ"}, {u"zyo", u"じょ"},
    {u"ja", u"じゃ"}, {u"ju", u"じゅ"}, {u"je", u"じぇ"}, {u"jo", u"じょ"},
    {u"jya", u"じゃ"}, {u"jyi", u"じぃ"}, {u"jyu", u"じゅ"}, {u"jye", u"じぇ"}, {u"jyo", u"じょ"},
    {u"da", u"だ"}, {u"di", u"ぢ"}, {u"du", u"づ"}, {u"de", u"で"}, {u"do", u"ど"},
    {u"dya", u"ぢゃ"}, {u"dyi", u"ぢぃ"}, {u"dyu", u"ぢゅ"}, {u"dye", u"ぢぇ"}, {u"dyo", u"ぢょ"},
    {u"dha", u"でゃ"}, {u"dhi", u"でぃ"}, {u"dhu", u"でゅ"}, {u"dhe", u"でぇ"}, {u"dho", u"でょ"},
    {u"dwu", u"どぅ"},
    {u"ba", u"ば"}, {u"bi", u"び"}, {u"bu", u"ぶ"}, {u"be", u"べ"}, {u"bo", u"ぼ"},
    {u"bya", u"びゃ"}, {u"byi", u"びぃ"}, {u"byu", u"びゅ"}, {u"bye", u"びぇ"}, {u"byo", u"びょ"},
    {u"pa", u"ぱ"}, {u"pi", u"ぴ"}, {u"pu", u"ぷ"}, {u"pe", u"ぺ"}, {u"po", u"ぽ"},
    {u"pya", u"ぴゃ"}, {u"pyi", u"ぴぃ"}, {u"pyu", u"ぴゅ"}, {u"pye", u"ぴぇ"}, {u"pyo", u"ぴょ"},
    {u"va", u"ゔぁ"}, {u"vi", u"ゔぃ"}, {u"vu", u"ゔ"}, {u"ve", u"ゔぇ"}, {u"vo", u"ゔぉ"},
    {u"xa", u"ぁ"}, {u"xi", u"ぃ"}, {u"xu", u"ぅ"}, {u"xe", u"ぇ"}, {u"xo", u"ぉ"},
    {u"la", u"ぁ"}, {u"li", u"ぃ"}, {u"lu", u"ぅ"}, {u"le", u"ぇ"}, {u"lo", u"ぉ"},
    {u"xya", u"ゃ"}, {u"xyu", u"ゅ"}, {u"xyo", u"ょ"}, {u"lya", u"ゃ"}, {u"lyu", u"ゅ"}, {u"lyo", u"ょ"},
    {u"xtu", u"っ"}, {u"xtsu", u"っ"}, {u"ltu", u"っ"}, {u"ltsu", u"っ"},
    {u"xwa", u"ゎ"}, {u"lwa", u"ゎ"},
};

// Doubled consonants produce a small tsu and keep one consonant: "kk" -> "っ" + "k".
constexpr char16_t kSokuonConsonants[] = u"bcdfghjklmpqrstvwxyz";

} // namespace

RomajiTable::RomajiTable(const std::vector<RomajiRule>& rules)
{
    for (const RomajiRule& rule : rules) {
        rules_.insert_or_assign(rule.input, rule);
    }
}

const RomajiTable& RomajiTable::Default()
{
    static const RomajiTable table = [] {
        std::vector<RomajiRule> rules;
        for (const Entry& entry : kDefaultEntries) {
            rules.push_back({entry.input, entry.output, u""});
        }
        for (const char16_t* c = kSokuonConsonants; *c != u'\0'; ++c) {
            rules.push_back({std::u16string(2, *c), u"っ", std::u16string(1, *c)});
        }
        return RomajiTable(rules);
    }();
    return table;
}

const RomajiRule* RomajiTable::FindExact(std::u16string_view input) const
{
    const auto it = rules_.find(input);
    return it == rules_.end() ? nullptr : &it->second;
}

RomajiTable::AddResult RomajiTable::Add(RomajiRule rule)
{
    std::u16string key = rule.input;
    const bool inserted = rules_.try_emplace(std::move(key), std::move(rule)).second;
    return inserted ? AddResult::Added : AddResult::AlreadyExists;
}

bool RomajiTable::Remove(std::u16string_view input)
{
    const auto it = rules_.find(input);
    if (it == rules_.end()) {
        return false;
    }
    rules_.erase(it);
    return true;
}

bool RomajiTable::HasLongerRule(std::u16string_view input) const
{
    auto it = rules_.upper_bound(input);
    return it != rules_.end() && it->first.size() > input.size() &&
           std::u16string_view{it->first}.substr(0, input.size()) == input;
}

bool RomajiTable::HasRuleStartingWith(std::u16string_view input) const
{
    return FindExact(input) != nullptr || HasLongerRule(input);
}

} // namespace astelio
