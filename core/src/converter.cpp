#include "astelio/converter.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace astelio {
namespace {

constexpr std::size_t kMaxEntriesPerReading = 64;
constexpr std::size_t kMaxCandidates = 50;
constexpr std::int32_t kInfinity = std::numeric_limits<std::int32_t>::max() / 2;
// Added to words found only after fixing a typo, so exact readings win when both make sense.
constexpr std::int32_t kTypoPenalty = 400;
// D-08: a word whose join with the next ones spells a suppressed word is avoided in the next search.
constexpr std::int32_t kSuppressedPenalty = 1'000'000;
constexpr int kMaxSuppressedRetries = 4;

struct Node {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::u16string_view surface;
    std::uint16_t left = 0;
    std::uint16_t right = 0;
    std::int32_t cost = 0;
    std::int32_t best = kInfinity;
    std::int32_t previous = -1; // -1: beginning of sentence
};

bool IsAsciiGraphic(char16_t c)
{
    return c >= 0x21 && c <= 0x7E;
}

Node MakeNode(std::size_t begin, std::size_t end, std::u16string_view surface, std::uint16_t left,
              std::uint16_t right, std::int32_t cost)
{
    Node node;
    node.begin = begin;
    node.end = end;
    node.surface = surface;
    node.left = left;
    node.right = right;
    node.cost = cost;
    return node;
}

bool IsSegmentBoundary(WordType former, WordType latter)
{
    if (former == WordType::Edge || latter == WordType::Edge || latter == WordType::Suffix) {
        return false;
    }
    return former != WordType::Prefix;
}

void AddUnique(std::vector<std::u16string>& list, std::u16string text)
{
    if (std::find(list.begin(), list.end(), text) == list.end()) {
        list.push_back(std::move(text));
    }
}

using UserWord = UserDictionary::Word;
using UserPos = UserDictionary::PartOfSpeech;

std::vector<const UserWord*> SuppressedWords(const UserDictionary* user)
{
    std::vector<const UserWord*> suppressed;
    if (user != nullptr) {
        for (const UserWord& word : user->Words()) {
            if (word.pos == UserPos::Suppressed) {
                suppressed.push_back(&word);
            }
        }
    }
    return suppressed;
}

// UserDictionary::Suppressed over the few suppressed words only (it is asked for every dictionary entry).
bool Hidden(const std::vector<const UserWord*>& suppressed, std::u16string_view reading, std::u16string_view surface)
{
    return std::any_of(suppressed.begin(), suppressed.end(), [&](const UserWord* word) {
        return word->surface == surface && (word->reading.empty() || reading.empty() || word->reading == reading);
    });
}

// The first node of words on `path` that together spell a suppressed word over the same reading, or nullptr.
const Node* SpelledSuppressed(const std::vector<const Node*>& path, std::u16string_view reading,
                              const std::vector<const UserWord*>& suppressed)
{
    constexpr std::size_t kLongestJoin = 4;
    for (std::size_t first = 0; first < path.size(); ++first) {
        std::u16string text(path[first]->surface);
        for (std::size_t k = first + 1; k < path.size() && k < first + kLongestJoin; ++k) {
            text += path[k]->surface;
            if (Hidden(suppressed, reading.substr(path[first]->begin, path[k]->end - path[first]->begin), text)) {
                return path[first];
            }
        }
    }
    return nullptr;
}

bool CollapsesWhenDoubled(char16_t c)
{
    switch (c) {
    case u'\u3063': // っ
    case u'\u3093': // ん
    case u'\u30FC': // ー
    case u'\u3041': case u'\u3043': case u'\u3045': case u'\u3047': case u'\u3049': // ぁぃぅぇぉ
    case u'\u3083': case u'\u3085': case u'\u3087': case u'\u308E':                  // ゃゅょゎ
        return true;
    default:
        return false;
    }
}

} // namespace

TypoCorrection CorrectTypos(std::u16string_view text)
{
    TypoCorrection correction;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i > 0 && text[i] == text[i - 1] && CollapsesWhenDoubled(text[i])) {
            continue;
        }
        correction.text.push_back(text[i]);
        correction.origin.push_back(i);
    }
    correction.origin.push_back(text.size());
    return correction;
}

std::u16string HiraganaToKatakana(std::u16string_view text)
{
    std::u16string result(text);
    for (char16_t& c : result) {
        if ((c >= u'\u3041' && c <= u'\u3096') || c == u'\u309D' || c == u'\u309E') {
            c = static_cast<char16_t>(c + 0x60);
        }
    }
    return result;
}

std::vector<std::u16string> Converter::Predict(std::u16string_view reading, std::size_t limit) const
{
    std::vector<std::u16string> predictions;
    if (reading.empty() || limit == 0) {
        return predictions;
    }
    const std::vector<const UserWord*> suppressed = SuppressedWords(user_dictionary_);
    std::u16string best;
    for (const ConvertedSegment& segment : Convert(reading)) {
        best += segment.candidates.front();
    }
    if (best != reading && !Hidden(suppressed, reading, best)) {
        predictions.push_back(std::move(best));
    }
    if (user_dictionary_ != nullptr) {
        for (std::u16string& surface : user_dictionary_->Predict(reading, limit)) {
            if (predictions.size() >= limit) {
                break;
            }
            AddUnique(predictions, std::move(surface));
        }
    }
    for (const SystemDictionary::Prediction& found : dictionary_.PredictiveSearch(reading, limit * 2)) {
        if (predictions.size() >= limit) {
            break;
        }
        if (found.reading.size() > reading.size() && !Hidden(suppressed, found.reading, found.entry.surface)) {
            AddUnique(predictions, std::u16string(found.entry.surface));
        }
    }
    const TypoCorrection corrected = CorrectTypos(reading);
    if (corrected.text != reading) {
        for (const SystemDictionary::Prediction& found : dictionary_.PredictiveSearch(corrected.text, limit)) {
            if (predictions.size() >= limit) {
                break;
            }
            if (!Hidden(suppressed, found.reading, found.entry.surface)) {
                AddUnique(predictions, std::u16string(found.entry.surface));
            }
        }
    }
    return predictions;
}

std::vector<ConvertedSegment> Converter::Convert(std::u16string_view reading,
                                                 std::span<const std::size_t> fixed_lengths,
                                                 std::optional<std::uint16_t> previous_right_id) const
{
    const std::size_t n = reading.size();
    if (n == 0) {
        return {};
    }
    const std::uint16_t start = previous_right_id.value_or(dictionary_.bos_id());

    std::vector<bool> forced(n + 1, false);
    std::size_t fixed_end = 0;
    for (const std::size_t length : fixed_lengths) {
        if (length == 0 || length > n - fixed_end) {
            break;
        }
        forced[fixed_end] = true;
        fixed_end += length;
        forced[fixed_end] = true;
    }
    // Words must not cross a boundary the user fixed.
    std::vector<std::size_t> limit(n + 1, n);
    for (std::size_t p = n; p-- > 0;) {
        limit[p] = forced[p + 1] ? p + 1 : limit[p + 1];
    }

    // Words read from the typo-corrected text span the original characters they came from.
    const TypoCorrection correction = CorrectTypos(reading);
    const bool has_typos = correction.text.size() != n;
    std::vector<std::size_t> corrected_at(n + 1, SIZE_MAX);
    for (std::size_t i = correction.origin.size(); i-- > 0;) {
        corrected_at[correction.origin[i]] = i;
    }

    // D-02: the user's words by where their reading starts; D-08: the words never to offer.
    const std::vector<const UserWord*> suppressed = SuppressedWords(user_dictionary_);
    std::vector<std::vector<const UserWord*>> user_words(n);
    if (user_dictionary_ != nullptr) {
        for (const UserWord& word : user_dictionary_->Words()) {
            if (word.pos == UserPos::Suppressed || word.reading.empty() ||
                Hidden(suppressed, word.reading, word.surface)) {
                continue;
            }
            for (std::size_t at = reading.find(word.reading); at != std::u16string_view::npos;
                 at = reading.find(word.reading, at + 1)) {
                user_words[at].push_back(&word);
            }
        }
    }
    // User words join sentences as nouns: dearer than common words, cheaper than text the dictionary does not know.
    const std::int32_t user_cost = dictionary_.unknown_cost() / 2;

    std::vector<Node> nodes;
    for (std::size_t begin = 0; begin < n; ++begin) {
        const std::u16string_view rest = reading.substr(begin, limit[begin] - begin);
        std::size_t current_length = 0;
        std::size_t count = 0;
        dictionary_.CommonPrefixSearch(rest, [&](std::size_t length, const DictionaryEntry& entry) {
            if (!suppressed.empty() && Hidden(suppressed, rest.substr(0, length), entry.surface)) {
                return;
            }
            if (length != current_length) {
                current_length = length;
                count = 0;
            }
            if (count++ < kMaxEntriesPerReading) {
                nodes.push_back(
                    MakeNode(begin, begin + length, entry.surface, entry.left_id, entry.right_id, entry.cost));
            }
        });
        if (has_typos && corrected_at[begin] != SIZE_MAX) {
            const std::size_t from = corrected_at[begin];
            dictionary_.CommonPrefixSearch(
                std::u16string_view(correction.text).substr(from),
                [&](std::size_t length, const DictionaryEntry& entry) {
                    const std::size_t end = correction.origin[from + length];
                    if (end - begin != length && end <= limit[begin] &&
                        !Hidden(suppressed, std::u16string_view(correction.text).substr(from, length),
                                entry.surface)) {
                        nodes.push_back(MakeNode(begin, end, entry.surface, entry.left_id, entry.right_id,
                                                 entry.cost + kTypoPenalty));
                    }
                });
        }
        for (const UserWord* word : user_words[begin]) {
            const std::size_t end = begin + word->reading.size();
            // A shortcut reading (短縮よみ) only as a whole segment: all the text, or a segment the user fixed.
            const bool fits = word->pos == UserPos::Abbreviation ? (begin == 0 || forced[begin]) && end == limit[begin]
                                                                  : end <= limit[begin];
            if (fits) {
                nodes.push_back(MakeNode(begin, end, word->surface, dictionary_.unknown_id(), dictionary_.unknown_id(),
                                         user_cost));
            }
        }
        // Text the dictionary does not know: one character, or a whole run of half-width letters and symbols.
        std::size_t length = 1;
        if (IsAsciiGraphic(rest[0])) {
            while (length < rest.size() && IsAsciiGraphic(rest[length])) {
                ++length;
            }
        }
        nodes.push_back(MakeNode(begin, begin + length, rest.substr(0, length), dictionary_.unknown_id(),
                                 dictionary_.unknown_id(), dictionary_.unknown_cost()));
    }

    // Viterbi over nodes in order of their start; predecessors of a node end where it begins.
    const auto shortest_path = [&]() {
        for (Node& node : nodes) {
            node.best = kInfinity;
            node.previous = -1;
        }
        std::vector<std::vector<std::int32_t>> ending(n + 1);
        std::vector<std::int32_t> heads;
        for (std::size_t i = 0; i < nodes.size();) {
            const std::size_t begin = nodes[i].begin;
            // Only the cheapest predecessor per right id matters.
            heads = ending[begin];
            std::sort(heads.begin(), heads.end(), [&nodes](std::int32_t a, std::int32_t b) {
                return std::pair(nodes[a].right, nodes[a].best) < std::pair(nodes[b].right, nodes[b].best);
            });
            heads.erase(std::unique(heads.begin(), heads.end(),
                                    [&nodes](std::int32_t a, std::int32_t b) {
                                        return nodes[a].right == nodes[b].right;
                                    }),
                        heads.end());
            for (; i < nodes.size() && nodes[i].begin == begin; ++i) {
                Node& node = nodes[i];
                if (begin == 0) {
                    node.best = dictionary_.ConnectionCost(start, node.left) + node.cost;
                } else {
                    for (const std::int32_t head : heads) {
                        const std::int32_t total =
                            nodes[head].best + dictionary_.ConnectionCost(nodes[head].right, node.left) + node.cost;
                        if (total < node.best) {
                            node.best = total;
                            node.previous = head;
                        }
                    }
                }
                if (node.best < kInfinity) {
                    ending[node.end].push_back(static_cast<std::int32_t>(i));
                }
            }
        }

        std::int32_t last = -1;
        std::int32_t last_total = kInfinity;
        for (const std::int32_t index : ending[n]) {
            const std::int32_t total =
                nodes[index].best + dictionary_.ConnectionCost(nodes[index].right, dictionary_.eos_id());
            if (total < last_total) {
                last_total = total;
                last = index;
            }
        }
        std::vector<const Node*> found;
        for (std::int32_t index = last; index >= 0; index = nodes[index].previous) {
            found.push_back(&nodes[index]);
        }
        std::reverse(found.begin(), found.end());
        return found;
    };
    std::vector<const Node*> path = shortest_path();
    // D-08: words joined on the path can spell a suppressed word (日本 + 語 = 日本語); that join is avoided.
    for (int retry = 0; retry < kMaxSuppressedRetries && !suppressed.empty(); ++retry) {
        const Node* spelled = SpelledSuppressed(path, reading, suppressed);
        if (spelled == nullptr) {
            break;
        }
        nodes[static_cast<std::size_t>(spelled - nodes.data())].cost = kSuppressedPenalty;
        path = shortest_path();
    }

    // Group words into segments.
    std::vector<std::pair<std::size_t, std::size_t>> groups; // [first word, last word + 1)
    for (std::size_t k = 0; k < path.size(); ++k) {
        const bool starts = k == 0 || forced[path[k]->begin] ||
                            (path[k]->begin >= fixed_end &&
                             IsSegmentBoundary(dictionary_.word_type(path[k - 1]->right),
                                               dictionary_.word_type(path[k]->left)));
        if (starts) {
            groups.emplace_back(k, k + 1);
        } else {
            groups.back().second = k + 1;
        }
    }

    std::vector<ConvertedSegment> segments;
    for (const auto& [first, end] : groups) {
        const std::size_t begin = path[first]->begin;
        const std::size_t finish = path[end - 1]->end;
        ConvertedSegment segment;
        segment.reading = std::u16string(reading.substr(begin, finish - begin));

        std::u16string best;
        for (std::size_t k = first; k < end; ++k) {
            best += path[k]->surface;
        }
        // The head (up to the last non-suffix word) can be replaced; trailing particles and endings are kept.
        std::size_t tail = end;
        while (tail > first + 1 && dictionary_.word_type(path[tail - 1]->left) == WordType::Suffix) {
            --tail;
        }
        std::u16string tail_text;
        for (std::size_t k = tail; k < end; ++k) {
            tail_text += path[k]->surface;
        }
        const std::uint16_t previous_right = first == 0 ? start : path[first - 1]->right;
        const std::uint16_t after_segment = end < path.size() ? path[end]->left : dictionary_.eos_id();
        const std::uint16_t after_head = tail < end ? path[tail]->left : after_segment;

        std::vector<std::pair<std::int32_t, std::u16string>> scored;
        const auto score = [&](const DictionaryEntry& entry, std::uint16_t next_left) {
            return dictionary_.ConnectionCost(previous_right, entry.left_id) + entry.cost +
                   dictionary_.ConnectionCost(entry.right_id, next_left);
        };
        const std::size_t head_end = path[tail - 1]->end;
        segment.head_length = head_end - begin;
        segment.tail = tail_text;
        segment.right_id = path[end - 1]->right;
        const std::u16string_view head_reading = reading.substr(begin, head_end - begin);
        for (const DictionaryEntry& entry : dictionary_.Lookup(head_reading)) {
            if (!Hidden(suppressed, head_reading, entry.surface)) {
                scored.emplace_back(score(entry, after_head), std::u16string(entry.surface) + tail_text);
            }
        }
        if (has_typos && corrected_at[begin] != SIZE_MAX && corrected_at[head_end] != SIZE_MAX) {
            const std::u16string_view fixed = std::u16string_view(correction.text)
                                                  .substr(corrected_at[begin], corrected_at[head_end] - corrected_at[begin]);
            if (fixed.size() != head_end - begin) {
                for (const DictionaryEntry& entry : dictionary_.Lookup(fixed)) {
                    if (!Hidden(suppressed, fixed, entry.surface)) {
                        scored.emplace_back(score(entry, after_head) + kTypoPenalty,
                                            std::u16string(entry.surface) + tail_text);
                    }
                }
            }
        }
        if (tail < end) {
            for (const DictionaryEntry& entry : dictionary_.Lookup(segment.reading)) {
                if (!Hidden(suppressed, segment.reading, entry.surface)) {
                    scored.emplace_back(score(entry, after_segment), std::u16string(entry.surface));
                }
            }
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });

        // D-02: the user's words for the whole segment or its head come first.
        for (const UserWord* word : user_words[begin]) {
            const std::size_t length = word->reading.size();
            if (length == finish - begin) {
                if (segment.candidates.empty() && word->surface != best) {
                    segment.right_id = dictionary_.unknown_id();
                }
                AddUnique(segment.candidates, word->surface);
            } else if (length == head_end - begin && word->pos != UserPos::Abbreviation) {
                AddUnique(segment.candidates, word->surface + tail_text);
            }
        }
        AddUnique(segment.candidates, std::move(best));
        std::vector<std::u16string> special = NumberForms(head_reading);
        if (special.empty() && IsDateReading(head_reading)) {
            special = DateForms(head_reading, clock_ ? clock_() : CurrentLocalTime());
        }
        for (std::u16string& text : special) {
            if (!Hidden(suppressed, head_reading, text)) {
                AddUnique(segment.candidates, std::move(text) + tail_text);
            }
        }
        for (auto& item : scored) {
            if (segment.candidates.size() + 2 >= kMaxCandidates) {
                break;
            }
            AddUnique(segment.candidates, std::move(item.second));
        }
        AddUnique(segment.candidates, segment.reading);
        AddUnique(segment.candidates, HiraganaToKatakana(segment.reading));
        if (!suppressed.empty()) {
            // Words joined in the lattice (日本 + 語) can spell a suppressed word (日本語) in the head too.
            const std::u16string_view tail_view = segment.tail;
            std::erase_if(segment.candidates, [&](const std::u16string& candidate) {
                const std::u16string_view text = candidate;
                return Hidden(suppressed, segment.reading, text) ||
                       (!tail_view.empty() && text.size() > tail_view.size() && text.ends_with(tail_view) &&
                        Hidden(suppressed, head_reading, text.substr(0, text.size() - tail_view.size())));
            });
            if (segment.candidates.empty()) {
                segment.candidates.push_back(segment.reading);
            }
        }
        segments.push_back(std::move(segment));
    }
    return segments;
}

} // namespace astelio
