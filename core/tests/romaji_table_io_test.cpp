#include "astelio/composer.h"
#include "astelio/romaji_table_io.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace astelio {
namespace {

std::u16string TypeWith(const RomajiTable& table, std::u16string_view keys)
{
    Composer composer(table, CharacterSettings{});
    for (char16_t key : keys) {
        composer.InsertKey(key);
    }
    return composer.Commit();
}

TEST(RomajiTableIo, DefaultRulesAreValid)
{
    for (const auto& [input, rule] : RomajiTable::Default().rules()) {
        EXPECT_FALSE(ValidateRomajiRule(rule).has_value()) << "input length " << input.size();
    }
}

// T-C04-1
TEST(RomajiTableIo, AddedAndRemovedRulesChangeTyping)
{
    RomajiTable table = RomajiTable::Default();
    const RomajiRule rule{u"kf", u"き", u""};
    ASSERT_FALSE(ValidateRomajiRule(rule).has_value());
    EXPECT_EQ(table.Add(rule), RomajiTable::AddResult::Added);
    EXPECT_EQ(table.Add(rule), RomajiTable::AddResult::AlreadyExists);
    EXPECT_EQ(TypeWith(table, u"kf"), u"き");

    EXPECT_TRUE(table.Remove(u"kf"));
    EXPECT_FALSE(table.Remove(u"kf"));
    EXPECT_EQ(TypeWith(table, u"kf"), u"kf");
}

TEST(RomajiTableIo, CustomRulesCanUseSymbolKeys)
{
    RomajiTable table = RomajiTable::Default();
    ASSERT_EQ(table.Add({u"z;", u"ざい", u""}), RomajiTable::AddResult::Added);
    EXPECT_EQ(TypeWith(table, u"z;"), u"ざい");
}

TEST(RomajiTableIo, ExportThenImportGivesTheSameTable)
{
    const RomajiTable& original = RomajiTable::Default();
    const RomajiTableParseResult parsed = ParseRomajiTable(SerializeRomajiTable(original));
    ASSERT_TRUE(parsed.table.has_value()) << "line " << parsed.line;
    ASSERT_EQ(parsed.table->rules().size(), original.rules().size());
    for (const auto& [input, rule] : original.rules()) {
        const RomajiRule* imported = parsed.table->FindExact(input);
        ASSERT_NE(imported, nullptr);
        EXPECT_EQ(imported->output, rule.output);
        EXPECT_EQ(imported->pending, rule.pending);
    }
}

TEST(RomajiTableIo, ImportsCommentsBlankLinesCrLfAndBom)
{
    const RomajiTableParseResult parsed =
        ParseRomajiTable("\xEF\xBB\xBF# comment\r\n\r\nka\t\xE3\x81\x8B\r\ntt\t\xE3\x81\xA3\tt\r\n");
    ASSERT_TRUE(parsed.table.has_value()) << "line " << parsed.line;
    EXPECT_EQ(parsed.table->rules().size(), 2u);
    EXPECT_EQ(TypeWith(*parsed.table, u"ka"), u"か");
    EXPECT_EQ(TypeWith(*parsed.table, u"tt"), u"っt");
}

// T-C04-2
TEST(RomajiTableIo, RejectsInvalidFilesWithTheLineNumber)
{
    struct Case {
        std::string_view text;
        RomajiTableError error;
        std::size_t line;
    };
    const Case cases[] = {
        {"ka\t\xE3\x81\x8B\nka\t\xE3\x81\x8D\n", RomajiTableError::Duplicate, 2},
        {"ka\n", RomajiTableError::MissingOutput, 1},
        {"ka\ta\tb\tc\n", RomajiTableError::TooManyFields, 1},
        {"Ka\t\xE3\x81\x8B\n", RomajiTableError::InvalidInput, 1},
        {"k a\t\xE3\x81\x8B\n", RomajiTableError::InvalidInput, 1},
        {"ka\t\xFF\n", RomajiTableError::InvalidUtf8, 1},
        {"ka\t\xC0\xAF\n", RomajiTableError::InvalidUtf8, 1},
        {"kk\t\xE3\x81\xA3\tkk\n", RomajiTableError::InvalidPending, 1},
        {"ka\t\n", RomajiTableError::EmptyRule, 1},
        {"ka\tabcdefghijklmnopq\n", RomajiTableError::InvalidOutput, 1},
    };
    for (const Case& c : cases) {
        const RomajiTableParseResult parsed = ParseRomajiTable(c.text);
        EXPECT_FALSE(parsed.table.has_value());
        EXPECT_EQ(parsed.error, c.error);
        EXPECT_EQ(parsed.line, c.line);
    }
}

TEST(RomajiTableIo, RejectsHugeFilesAndTooManyRules)
{
    const RomajiTableParseResult huge = ParseRomajiTable(std::string(kMaxRomajiTableBytes + 1, '#'));
    EXPECT_FALSE(huge.table.has_value());
    EXPECT_EQ(huge.error, RomajiTableError::TooLarge);

    std::string many;
    for (std::size_t i = 0; i <= kMaxRomajiRules; ++i) {
        many += "q" + std::to_string(i) + "\tx\n";
    }
    const RomajiTableParseResult too_many = ParseRomajiTable(many);
    EXPECT_FALSE(too_many.table.has_value());
    EXPECT_EQ(too_many.error, RomajiTableError::TooManyRules);
}

} // namespace
} // namespace astelio
