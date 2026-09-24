#include "astelio/romaji_table_io.h"

#include "astelio/utf.h"

#include <utility>
#include <vector>

namespace astelio {
namespace {

// Keys a US keyboard types without Shift-letter (uppercase starts alphanumeric mode instead).
bool IsRuleKey(char16_t c)
{
    return c >= 0x21 && c <= 0x7E && !(c >= u'A' && c <= u'Z');
}

bool AreRuleKeys(std::u16string_view text)
{
    for (char16_t c : text) {
        if (!IsRuleKey(c)) {
            return false;
        }
    }
    return true;
}

bool HasControlCharacter(std::u16string_view text)
{
    for (char16_t c : text) {
        if (c < 0x20 || c == 0x7F) {
            return true;
        }
    }
    return false;
}

std::vector<std::string_view> Split(std::string_view line, char separator)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = line.find(separator, start);
        fields.push_back(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            return fields;
        }
        start = end + 1;
    }
}

RomajiTableParseResult Fail(RomajiTableError error, std::size_t line)
{
    RomajiTableParseResult result;
    result.error = error;
    result.line = line;
    return result;
}

} // namespace

std::optional<RomajiTableError> ValidateRomajiRule(const RomajiRule& rule)
{
    if (rule.input.empty() || rule.input.size() > kMaxRomajiFieldLength || !AreRuleKeys(rule.input)) {
        return RomajiTableError::InvalidInput;
    }
    if (rule.output.size() > kMaxRomajiFieldLength || HasControlCharacter(rule.output)) {
        return RomajiTableError::InvalidOutput;
    }
    if (!AreRuleKeys(rule.pending) || rule.pending.size() >= rule.input.size()) {
        return RomajiTableError::InvalidPending;
    }
    if (rule.output.empty() && rule.pending.empty()) {
        return RomajiTableError::EmptyRule;
    }
    return std::nullopt;
}

RomajiTableParseResult ParseRomajiTable(std::string_view utf8)
{
    if (utf8.size() > kMaxRomajiTableBytes) {
        return Fail(RomajiTableError::TooLarge, 0);
    }
    if (utf8.substr(0, 3) == "\xEF\xBB\xBF") {
        utf8.remove_prefix(3);
    }

    RomajiTable table;
    std::size_t line_number = 0;
    for (std::string_view line : Split(utf8, '\n')) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string_view> fields = Split(line, '\t');
        if (fields.size() < 2) {
            return Fail(RomajiTableError::MissingOutput, line_number);
        }
        if (fields.size() > 3) {
            return Fail(RomajiTableError::TooManyFields, line_number);
        }

        std::u16string parts[3];
        for (std::size_t i = 0; i < fields.size(); ++i) {
            std::optional<std::u16string> converted = Utf8ToUtf16(fields[i]);
            if (!converted) {
                return Fail(RomajiTableError::InvalidUtf8, line_number);
            }
            parts[i] = std::move(*converted);
        }
        RomajiRule rule{std::move(parts[0]), std::move(parts[1]), std::move(parts[2])};

        if (const auto error = ValidateRomajiRule(rule)) {
            return Fail(*error, line_number);
        }
        if (table.rules().size() >= kMaxRomajiRules) {
            return Fail(RomajiTableError::TooManyRules, line_number);
        }
        if (table.Add(rule) != RomajiTable::AddResult::Added) {
            return Fail(RomajiTableError::Duplicate, line_number);
        }
    }

    RomajiTableParseResult result;
    result.table = std::move(table);
    return result;
}

std::string SerializeRomajiTable(const RomajiTable& table)
{
    std::string out = "# Astelio IME romaji table: input<TAB>output[<TAB>pending]\n";
    for (const auto& [input, rule] : table.rules()) {
        out += Utf16ToUtf8(rule.input);
        out += '\t';
        out += Utf16ToUtf8(rule.output);
        if (!rule.pending.empty()) {
            out += '\t';
            out += Utf16ToUtf8(rule.pending);
        }
        out += '\n';
    }
    return out;
}

} // namespace astelio
