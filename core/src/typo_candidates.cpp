#include "astelio/typo_candidates.h"

#include "astelio/composer.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace astelio {
namespace {

constexpr std::u16string_view kInsertableKeys = u"abcdefghijklmnopqrstuvwxyz-";
constexpr std::u16string_view kVowels = u"aiueo";
constexpr std::size_t kCandidatesToScore = 20;

bool IsVowel(char16_t key)
{
    return kVowels.find(key) != std::u16string_view::npos;
}

// Keys typed for a similar sound: ふ is hu or fu, じ is ji or zi, and ヴ and ブ are often mixed up.
std::u16string_view SimilarKeys(char16_t key)
{
    switch (key) {
    case u'h': return u"f";
    case u'f': return u"h";
    case u'j': return u"z";
    case u'z': return u"j";
    case u'v': return u"b";
    case u'b': return u"v";
    default: return {};
    }
}

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
    std::unordered_map<std::u16string, std::int32_t> variants; // keys -> penalty of the edit
    const auto add = [&variants](std::u16string variant, std::int32_t penalty) {
        const auto [found, inserted] = variants.emplace(std::move(variant), penalty);
        if (!inserted && penalty < found->second) {
            found->second = penalty;
        }
    };
    const std::u16string typed(keys);
    for (std::size_t i = 0; i < typed.size(); ++i) {
        add(typed.substr(0, i) + typed.substr(i + 1), kTypoKeyPenalty); // an extra key
        std::u16string replaced = typed;
        for (const char16_t neighbour : NeighbouringKeys(typed[i])) {
            replaced[i] = neighbour;
            add(replaced, kTypoKeyPenalty);
        }
        for (const char16_t similar : SimilarKeys(typed[i])) {
            replaced[i] = similar;
            add(replaced, kTypoSoundPenalty);
        }
        if (IsVowel(typed[i])) {
            for (const char16_t vowel : kVowels) {
                replaced[i] = vowel;
                add(replaced, kTypoSoundPenalty);
            }
        }
        if (i + 1 < typed.size()) {
            std::u16string swapped = typed;
            std::swap(swapped[i], swapped[i + 1]);
            add(std::move(swapped), kTypoKeyPenalty);
            // Both keys of a doubled consonant (っ) slipped the same way.
            if (typed[i] == typed[i + 1] && !IsVowel(typed[i]) && typed[i] != u'n') {
                std::u16string doubled = typed;
                for (const std::u16string_view keys_like : {NeighbouringKeys(typed[i]), SimilarKeys(typed[i])}) {
                    for (const char16_t other : keys_like) {
                        doubled[i] = doubled[i + 1] = other;
                        add(doubled, kTypoSoundPenalty);
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i <= typed.size(); ++i) {
        for (const char16_t missing : kInsertableKeys) {
            add(typed.substr(0, i) + missing + typed.substr(i), kTypoKeyPenalty);
        }
    }
    variants.erase(typed);

    const std::u16string typed_reading = ToKana(typed, table);
    std::unordered_map<std::u16string, std::size_t> by_surface;
    for (const auto& [variant, penalty] : variants) {
        std::u16string reading = ToKana(variant, table);
        if (reading.empty() || reading == typed_reading || HasLeftoverRomaji(reading)) {
            continue;
        }
        for (const DictionaryEntry& entry : dictionary.Lookup(reading)) {
            TypoCandidate candidate{variant, reading, std::u16string(entry.surface), entry.cost + penalty,
                                    entry.left_id, entry.right_id};
            const auto found = by_surface.find(candidate.surface);
            if (found != by_surface.end()) {
                TypoCandidate& known = result[found->second];
                if (candidate.cost < known.cost ||
                    (candidate.cost == known.cost && candidate.keys < known.keys)) {
                    known = std::move(candidate);
                }
                continue;
            }
            by_surface.emplace(candidate.surface, result.size());
            result.push_back(std::move(candidate));
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

std::optional<TypoCandidate> SuggestTypoCorrection(std::u16string_view keys, const RomajiTable& table,
                                                   const SystemDictionary& dictionary,
                                                   std::optional<std::uint16_t> previous_right_id)
{
    const std::uint16_t previous = previous_right_id.value_or(dictionary.bos_id());
    const auto standalone = [&](std::uint16_t left, std::uint16_t right, std::int32_t cost) {
        return dictionary.ConnectionCost(previous, left) + cost + dictionary.ConnectionCost(right, dictionary.eos_id());
    };
    std::optional<std::int32_t> typed_score;
    for (const DictionaryEntry& entry : dictionary.Lookup(ToKana(keys, table))) {
        const std::int32_t score = standalone(entry.left_id, entry.right_id, entry.cost);
        typed_score = typed_score ? std::min(*typed_score, score) : score;
    }

    std::optional<TypoCandidate> best;
    std::int32_t best_score = 0;
    for (TypoCandidate& candidate : FindTypoCandidates(keys, table, dictionary, kCandidatesToScore)) {
        // Inflection fragments (出しゃ) and particles are not what a slipped key usually meant.
        if (candidate.reading.size() < 2 || dictionary.word_type(candidate.left_id) != WordType::Content) {
            continue;
        }
        const std::int32_t score = standalone(candidate.left_id, candidate.right_id, candidate.cost);
        if (!best || score < best_score) {
            best_score = score;
            best = std::move(candidate);
        }
    }
    if (best && typed_score && best_score + kTypoSuggestMargin >= *typed_score) {
        return std::nullopt;
    }
    return best;
}

} // namespace astelio
