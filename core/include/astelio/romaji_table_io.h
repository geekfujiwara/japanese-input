#pragma once

#include "astelio/romaji_table.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace astelio {

// Text format (UTF-8, optional BOM): one rule per line, "input<TAB>output[<TAB>pending]".
// Lines starting with '#' and blank lines are ignored.

enum class RomajiTableError : std::uint8_t {
    TooLarge,
    InvalidUtf8,
    TooManyRules,
    MissingOutput,
    TooManyFields,
    InvalidInput,     // empty, too long, or not lowercase-ASCII keys
    InvalidOutput,    // too long or contains control characters
    InvalidPending,   // not keys, or not shorter than the input (would never finish)
    EmptyRule,        // no output and no pending
    Duplicate,
};

struct RomajiTableParseResult {
    std::optional<RomajiTable> table;
    RomajiTableError error = RomajiTableError::TooLarge;
    std::size_t line = 0; // 1-based line of the error
};

inline constexpr std::size_t kMaxRomajiTableBytes = 1024 * 1024;
inline constexpr std::size_t kMaxRomajiRules = 10000;
inline constexpr std::size_t kMaxRomajiFieldLength = 16;

std::optional<RomajiTableError> ValidateRomajiRule(const RomajiRule& rule);

RomajiTableParseResult ParseRomajiTable(std::string_view utf8);
std::string SerializeRomajiTable(const RomajiTable& table);

} // namespace astelio
