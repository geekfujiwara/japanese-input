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
    // A key put at `at` of `variant` by the edit costs more when few people type it (c other than ch, q, x, l).
    const auto put = [&add](std::u16string variant, std::size_t at, std::int32_t penalty) {
        const char16_t key = variant[at];
        const bool rare = key == u'q' || key == u'x' || key == u'l' ||
                          (key == u'c' && (at + 1 >= variant.size() || variant[at + 1] != u'h'));
        add(std::move(variant), rare ? penalty + kTypoRareKeySurcharge : penalty);
    };
    const std::u16string typed(keys);
    for (std::size_t i = 0; i < typed.size(); ++i) {
        // An extra key; a key pressed twice ("kaigii") is the most common.
        const bool repeated = (i > 0 && typed[i - 1] == typed[i]) && (IsVowel(typed[i]) || typed[i] == u'-');
        add(typed.substr(0, i) + typed.substr(i + 1), repeated ? kTypoLikelyPenalty : kTypoKeyPenalty);
        std::u16string replaced = typed;
        for (const char16_t neighbour : NeighbouringKeys(typed[i])) {
            replaced[i] = neighbour;
            put(replaced, i, kTypoKeyPenalty);
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
                        put(doubled, i, kTypoSoundPenalty);
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i <= typed.size(); ++i) {
        for (const char16_t missing : kInsertableKeys) {
            put(typed.substr(0, i) + missing + typed.substr(i), i, kTypoKeyPenalty);
        }
        // A long vowel left out ("ryoko" for "ryokou", "sense" for "sensei"), and ん before a な-row kana
        // ("konichiha"), which takes two n.
        if (i > 0 && (typed[i - 1] == u'o' || typed[i - 1] == u'u')) {
            add(typed.substr(0, i) + u'u' + typed.substr(i), kTypoLikelyPenalty);
        }
        if (i > 0 && typed[i - 1] == u'e') {
            add(typed.substr(0, i) + u'i' + typed.substr(i), kTypoLikelyPenalty);
        }
        if (i + 1 < typed.size() && typed[i] == u'n' && (i == 0 || typed[i - 1] != u'n')) {
            add(typed.substr(0, i) + u"nn" + typed.substr(i), kTypoKeyPenalty);
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
            // Particles and endings are not what a slipped key usually meant.
            if (dictionary.word_type(entry.left_id) != WordType::Content) {
                continue;
            }
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
                                                   std::optional<std::uint16_t> previous_right_id,
                                                   std::int32_t margin)
{
    const std::uint16_t previous = previous_right_id.value_or(dictionary.bos_id());
    const auto score_of = [&](std::uint16_t left, std::int32_t cost) {
        return dictionary.ConnectionCost(previous, left) + cost;
    };
    std::optional<std::int32_t> typed_score;
    for (const DictionaryEntry& entry : dictionary.Lookup(ToKana(keys, table))) {
        const std::int32_t score = score_of(entry.left_id, entry.cost);
        typed_score = typed_score ? std::min(*typed_score, score) : score;
    }

    std::optional<TypoCandidate> best;
    std::int32_t best_score = 0;
    for (TypoCandidate& candidate : FindTypoCandidates(keys, table, dictionary, kCandidatesToScore)) {
        if (candidate.reading.size() < 2) {
            continue;
        }
        const std::int32_t score = score_of(candidate.left_id, candidate.cost);
        if (!best || score < best_score) {
            best_score = score;
            best = std::move(candidate);
        }
    }
    if (best && typed_score && best_score + margin >= *typed_score) {
        return std::nullopt;
    }
    return best;
}

} // namespace astelio
