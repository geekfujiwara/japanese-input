#include "astelio/typo_candidates.h"

#include "astelio/composer.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace astelio {
namespace {

constexpr std::u16string_view kInsertableKeys = u"abcdefghijklmnopqrstuvwxyz-";

std::u16string ToKana(std::u16string_view keys, const RomajiTable& table)
{
    Composer composer(table, CharacterSettings{});
    for (const char16_t key : keys) {
        composer.InsertKey(key);
    }
    return composer.Commit();
}

bool HasLeftoverRomaji(std::u16string_view text)
{
    return std::any_of(text.begin(), text.end(), [](char16_t c) { return c >= u'a' && c <= u'z'; });
}

} // namespace

std::u16string_view NeighbouringKeys(char16_t key)
{
    switch (key) {
    case u'q': return u"wa";
    case u'w': return u"qesa";
    case u'e': return u"wrds";
    case u'r': return u"etfd";
    case u't': return u"rygf";
    case u'y': return u"tuhg";
    case u'u': return u"yijh";
    case u'i': return u"uokj";
    case u'o': return u"ipkl";
    case u'p': return u"ol-";
    case u'a': return u"qwsz";
    case u's': return u"awedxz";
    case u'd': return u"serfcx";
    case u'f': return u"drtgvc";
    case u'g': return u"ftyhbv";
    case u'h': return u"gyujnb";
    case u'j': return u"huikmn";
    case u'k': return u"jiolm";
    case u'l': return u"kop";
    case u'z': return u"asx";
    case u'x': return u"zsdc";
    case u'c': return u"xdfv";
    case u'v': return u"cfgb";
    case u'b': return u"vghn";
    case u'n': return u"bhjm";
    case u'm': return u"njk";
    case u'-': return u"p";
    default: return {};
    }
}

std::vector<TypoCandidate> FindTypoCandidates(std::u16string_view keys, const RomajiTable& table,
                                              const SystemDictionary& dictionary, std::size_t limit)
{
    std::vector<TypoCandidate> result;
    if (keys.empty() || limit == 0) {
        return result;
    }
    std::unordered_set<std::u16string> variants;
    const std::u16string typed(keys);
    for (std::size_t i = 0; i < typed.size(); ++i) {
        variants.insert(typed.substr(0, i) + typed.substr(i + 1)); // an extra key
        for (const char16_t neighbour : NeighbouringKeys(typed[i])) {
            std::u16string replaced = typed;
            replaced[i] = neighbour;
            variants.insert(std::move(replaced));
        }
        if (i + 1 < typed.size()) {
            std::u16string swapped = typed;
            std::swap(swapped[i], swapped[i + 1]);
            variants.insert(std::move(swapped));
        }
    }
    for (std::size_t i = 0; i <= typed.size(); ++i) {
        for (const char16_t missing : kInsertableKeys) {
            variants.insert(typed.substr(0, i) + missing + typed.substr(i));
        }
    }
    variants.erase(typed);

    const std::u16string typed_reading = ToKana(typed, table);
    std::unordered_map<std::u16string, std::size_t> by_surface;
    for (const std::u16string& variant : variants) {
        std::u16string reading = ToKana(variant, table);
        if (reading.empty() || reading == typed_reading || HasLeftoverRomaji(reading)) {
            continue;
        }
        for (const DictionaryEntry& entry : dictionary.Lookup(reading)) {
            const std::int32_t cost = entry.cost + kTypoKeyPenalty;
            const auto found = by_surface.find(std::u16string(entry.surface));
            if (found != by_surface.end()) {
                if (cost < result[found->second].cost) {
                    result[found->second] = {variant, reading, std::u16string(entry.surface), cost};
                }
                continue;
            }
            by_surface.emplace(std::u16string(entry.surface), result.size());
            result.push_back({variant, reading, std::u16string(entry.surface), cost});
        }
    }
    std::sort(result.begin(), result.end(), [](const TypoCandidate& a, const TypoCandidate& b) {
        return a.cost != b.cost ? a.cost < b.cost : a.surface < b.surface;
    });
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

} // namespace astelio
