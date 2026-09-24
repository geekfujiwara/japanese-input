#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

struct RomajiRule {
    std::u16string input;   // e.g. u"kk"
    std::u16string output;  // e.g. u"っ"
    std::u16string pending; // input kept for the next rule, e.g. u"k"
};

class RomajiTable {
public:
    RomajiTable() = default;
    explicit RomajiTable(const std::vector<RomajiRule>& rules);

    static const RomajiTable& Default();

    enum class AddResult : unsigned char {
        Added,
        AlreadyExists,
    };

    // Callers validate rules first (see ValidateRomajiRule).
    AddResult Add(RomajiRule rule);
    bool Remove(std::u16string_view input);

    const RomajiRule* FindExact(std::u16string_view input) const;
    // True when some rule starts with `input` and is longer than it.
    bool HasLongerRule(std::u16string_view input) const;
    bool HasRuleStartingWith(std::u16string_view input) const;

    const std::map<std::u16string, RomajiRule, std::less<>>& rules() const { return rules_; }

private:
    std::map<std::u16string, RomajiRule, std::less<>> rules_;
};

} // namespace astelio
