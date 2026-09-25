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
// Head alternatives per segment weighed against the neighbouring segments' meanings.
constexpr std::size_t kContextAlternatives = 4;

struct Node {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::u16string_view surface;
    std::uint16_t left = 0;
    std::uint16_t right = 0;
    std::int32_t cost = 0;
    std::uint16_t meaning = 0;
    std::int32_t best = kInfinity;
    std::int32_t previous = -1; // -1: beginning of sentence
};

bool IsAsciiGraphic(char16_t c)
{
    return c >= 0x21 && c <= 0x7E;
}

Node MakeNode(std::size_t begin, std::size_t end, std::u16string_view surface, std::uint16_t left,
              std::uint16_t right, std::int32_t cost, std::uint16_t meaning = 0)
{
    Node node;
    node.begin = begin;
    node.end = end;
    node.surface = surface;
    node.left = left;
    node.right = right;
    node.cost = cost;
    node.meaning = meaning;
    return node;
}

// A segment's meaning is that of its last word that carries one (content words, dependent verbs),
// else of its first word with a non-neutral meaning.
class SegmentMeaning {
public:
    explicit SegmentMeaning(const SystemDictionary& dictionary)
        : dictionary_(dictionary), meaning_(dictionary.neutral_meaning())
    {
    }

    void Add(std::uint16_t meaning, std::uint16_t left, std::uint16_t right)
    {
        if ((meaning_ == dictionary_.neutral_meaning() && meaning != dictionary_.neutral_meaning()) ||
            dictionary_.gives_meaning(left) || dictionary_.gives_meaning(right)) {
            meaning_ = meaning;
        }
    }

    std::uint16_t value() const { return meaning_; }

private:
    const SystemDictionary& dictionary_;
    std::uint16_t meaning_;
};

struct ContextOption {
    std::int32_t cost = 0;
    std::u16string text;
    std::uint16_t meaning = 0;
};

// Picks one option per segment minimising option costs plus the meaning costs between neighbours.
std::vector<std::size_t> ChooseInContext(const SystemDictionary& dictionary,
                                         const std::vector<std::vector<ContextOption>>& options)
{
    std::vector<std::vector<std::int32_t>> total(options.size());
    std::vector<std::vector<std::size_t>> from(options.size());
    for (std::size_t s = 0; s < options.size(); ++s) {
        total[s].assign(options[s].size(), kInfinity);
        from[s].assign(options[s].size(), 0);
        for (std::size_t k = 0; k < options[s].size(); ++k) {
            if (s == 0) {
                total[s][k] = options[s][k].cost;
                continue;
            }
            for (std::size_t j = 0; j < options[s - 1].size(); ++j) {
                const std::int32_t value = total[s - 1][j] +
                                           dictionary.MeaningCost(options[s - 1][j].meaning, options[s][k].meaning) +
                                           options[s][k].cost;
                if (value < total[s][k]) {
                    total[s][k] = value;
                    from[s][k] = j;
                }
            }
        }
    }
    std::vector<std::size_t> chosen(options.size(), 0);
    if (options.empty()) {
        return chosen;
    }
    const std::vector<std::int32_t>& last = total.back();
    chosen.back() = static_cast<std::size_t>(std::min_element(last.begin(), last.end()) - last.begin());
    for (std::size_t s = options.size() - 1; s > 0; --s) {
        chosen[s - 1] = from[s][chosen[s]];
    }
    return chosen;
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
    std::u16string best;
    for (const ConvertedSegment& segment : Convert(reading)) {
        best += segment.candidates.front();
    }
    if (best != reading) {
        predictions.push_back(std::move(best));
    }
    for (const SystemDictionary::Prediction& found : dictionary_.PredictiveSearch(reading, limit * 2)) {
        if (predictions.size() >= limit) {
            break;
        }
        if (found.reading.size() > reading.size()) {
            AddUnique(predictions, std::u16string(found.entry.surface));
        }
    }
    const TypoCorrection corrected = CorrectTypos(reading);
    if (corrected.text != reading) {
        for (const SystemDictionary::Prediction& found : dictionary_.PredictiveSearch(corrected.text, limit)) {
            if (predictions.size() >= limit) {
                break;
            }
            AddUnique(predictions, std::u16string(found.entry.surface));
        }
    }
    return predictions;
}

std::vector<ConvertedSegment> Converter::Convert(std::u16string_view reading,
                                                 std::span<const std::size_t> fixed_lengths) const
{
    const std::size_t n = reading.size();
    if (n == 0) {
        return {};
    }

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

    std::vector<Node> nodes;
    for (std::size_t begin = 0; begin < n; ++begin) {
        const std::u16string_view rest = reading.substr(begin, limit[begin] - begin);
        std::size_t current_length = 0;
        std::size_t count = 0;
        dictionary_.CommonPrefixSearch(rest, [&](std::size_t length, const DictionaryEntry& entry) {
            if (length != current_length) {
                current_length = length;
                count = 0;
            }
            if (count++ < kMaxEntriesPerReading) {
                nodes.push_back(MakeNode(begin, begin + length, entry.surface, entry.left_id, entry.right_id,
                                         entry.cost, entry.meaning_id));
            }
        });
        if (has_typos && corrected_at[begin] != SIZE_MAX) {
            const std::size_t from = corrected_at[begin];
            dictionary_.CommonPrefixSearch(
                std::u16string_view(correction.text).substr(from),
                [&](std::size_t length, const DictionaryEntry& entry) {
                    const std::size_t end = correction.origin[from + length];
                    if (end - begin != length && end <= limit[begin]) {
                        nodes.push_back(MakeNode(begin, end, entry.surface, entry.left_id, entry.right_id,
                                                 entry.cost + kTypoPenalty, entry.meaning_id));
                    }
                });
        }
        // Text the dictionary does not know: one character, or a whole run of half-width letters and symbols.
        std::size_t length = 1;
        if (IsAsciiGraphic(rest[0])) {
            while (length < rest.size() && IsAsciiGraphic(rest[length])) {
                ++length;
            }
        }
        nodes.push_back(MakeNode(begin, begin + length, rest.substr(0, length), dictionary_.unknown_id(),
                                 dictionary_.unknown_id(), dictionary_.unknown_cost(), dictionary_.neutral_meaning()));
    }

    // Viterbi over nodes in order of their start; predecessors of a node end where it begins.
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
                                [&nodes](std::int32_t a, std::int32_t b) { return nodes[a].right == nodes[b].right; }),
                    heads.end());
        for (; i < nodes.size() && nodes[i].begin == begin; ++i) {
            Node& node = nodes[i];
            if (begin == 0) {
                node.best = dictionary_.ConnectionCost(dictionary_.bos_id(), node.left) + node.cost;
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
    std::vector<const Node*> path;
    for (std::int32_t index = last; index >= 0; index = nodes[index].previous) {
        path.push_back(&nodes[index]);
    }
    std::reverse(path.begin(), path.end());

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
    std::vector<std::vector<ContextOption>> context(groups.size());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const std::size_t first = groups[g].first;
        const std::size_t end = groups[g].second;
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
        const std::uint16_t previous_right = first == 0 ? dictionary_.bos_id() : path[first - 1]->right;
        const std::uint16_t after_segment = end < path.size() ? path[end]->left : dictionary_.eos_id();
        const std::uint16_t after_head = tail < end ? path[tail]->left : after_segment;

        std::vector<std::pair<std::int32_t, std::u16string>> scored;
        std::vector<ContextOption> alternatives; // same tail, another head
        const auto score = [&](const DictionaryEntry& entry, std::uint16_t next_left) {
            return dictionary_.ConnectionCost(previous_right, entry.left_id) + entry.cost +
                   dictionary_.ConnectionCost(entry.right_id, next_left);
        };
        const auto with_tail = [&](const DictionaryEntry& entry) {
            SegmentMeaning meaning(dictionary_);
            meaning.Add(entry.meaning_id, entry.left_id, entry.right_id);
            for (std::size_t k = tail; k < end; ++k) {
                meaning.Add(path[k]->meaning, path[k]->left, path[k]->right);
            }
            return meaning.value();
        };
        const std::size_t head_end = path[tail - 1]->end;
        for (const DictionaryEntry& entry : dictionary_.Lookup(reading.substr(begin, head_end - begin))) {
            scored.emplace_back(score(entry, after_head), std::u16string(entry.surface) + tail_text);
            alternatives.push_back({scored.back().first, scored.back().second, with_tail(entry)});
        }
        if (has_typos && corrected_at[begin] != SIZE_MAX && corrected_at[head_end] != SIZE_MAX) {
            const std::u16string_view fixed = std::u16string_view(correction.text)
                                                  .substr(corrected_at[begin], corrected_at[head_end] - corrected_at[begin]);
            if (fixed.size() != head_end - begin) {
                for (const DictionaryEntry& entry : dictionary_.Lookup(fixed)) {
                    scored.emplace_back(score(entry, after_head) + kTypoPenalty,
                                        std::u16string(entry.surface) + tail_text);
                }
            }
        }
        if (tail < end) {
            for (const DictionaryEntry& entry : dictionary_.Lookup(segment.reading)) {
                scored.emplace_back(score(entry, after_segment), std::u16string(entry.surface));
            }
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });

        // The best text and, when its head is one word (so the costs compare), the cheapest other heads.
        SegmentMeaning best_meaning(dictionary_);
        for (std::size_t k = first; k < end; ++k) {
            best_meaning.Add(path[k]->meaning, path[k]->left, path[k]->right);
        }
        const bool single_head = tail == first + 1;
        const std::int32_t best_cost =
            single_head ? dictionary_.ConnectionCost(previous_right, path[first]->left) + path[first]->cost +
                              dictionary_.ConnectionCost(path[first]->right, after_head)
                        : 0;
        context[g].push_back({best_cost, best, best_meaning.value()});
        if (single_head) {
            std::stable_sort(alternatives.begin(), alternatives.end(),
                             [](const ContextOption& a, const ContextOption& b) { return a.cost < b.cost; });
            for (ContextOption& option : alternatives) {
                if (context[g].size() > kContextAlternatives) {
                    break;
                }
                if (option.text != best) {
                    context[g].push_back(std::move(option));
                }
            }
        }

        AddUnique(segment.candidates, std::move(best));
        const std::u16string_view head_reading = reading.substr(begin, head_end - begin);
        std::vector<std::u16string> special = NumberForms(head_reading);
        if (special.empty() && IsDateReading(head_reading)) {
            special = DateForms(head_reading, clock_ ? clock_() : CurrentLocalTime());
        }
        for (std::u16string& text : special) {
            AddUnique(segment.candidates, std::move(text) + tail_text);
        }
        for (auto& item : scored) {
            if (segment.candidates.size() + 2 >= kMaxCandidates) {
                break;
            }
            AddUnique(segment.candidates, std::move(item.second));
        }
        AddUnique(segment.candidates, segment.reading);
        AddUnique(segment.candidates, HiraganaToKatakana(segment.reading));
        segments.push_back(std::move(segment));
    }

    // Homophones: the meanings of neighbouring segments pick among the heads (お茶があつい -> 熱い).
    if (dictionary_.meaning_count() > 0 && segments.size() > 1) {
        const std::vector<std::size_t> chosen = ChooseInContext(dictionary_, context);
        for (std::size_t g = 0; g < segments.size(); ++g) {
            if (chosen[g] == 0) {
                continue;
            }
            std::vector<std::u16string>& candidates = segments[g].candidates;
            const auto found = std::find(candidates.begin(), candidates.end(), context[g][chosen[g]].text);
            if (found != candidates.end()) {
                std::rotate(candidates.begin(), found, found + 1);
            }
        }
    }
    return segments;
}

} // namespace astelio
