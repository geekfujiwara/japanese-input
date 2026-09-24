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

} // namespace

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
                nodes.push_back(
                    MakeNode(begin, begin + length, entry.surface, entry.left_id, entry.right_id, entry.cost));
            }
        });
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
        const std::uint16_t previous_right = first == 0 ? dictionary_.bos_id() : path[first - 1]->right;
        const std::uint16_t after_segment = end < path.size() ? path[end]->left : dictionary_.eos_id();
        const std::uint16_t after_head = tail < end ? path[tail]->left : after_segment;

        std::vector<std::pair<std::int32_t, std::u16string>> scored;
        const auto score = [&](const DictionaryEntry& entry, std::uint16_t next_left) {
            return dictionary_.ConnectionCost(previous_right, entry.left_id) + entry.cost +
                   dictionary_.ConnectionCost(entry.right_id, next_left);
        };
        const std::size_t head_end = path[tail - 1]->end;
        for (const DictionaryEntry& entry : dictionary_.Lookup(reading.substr(begin, head_end - begin))) {
            scored.emplace_back(score(entry, after_head), std::u16string(entry.surface) + tail_text);
        }
        if (tail < end) {
            for (const DictionaryEntry& entry : dictionary_.Lookup(segment.reading)) {
                scored.emplace_back(score(entry, after_segment), std::u16string(entry.surface));
            }
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });

        AddUnique(segment.candidates, std::move(best));
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
    return segments;
}

} // namespace astelio
