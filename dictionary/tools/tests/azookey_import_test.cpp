#include "azookey_import.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

using astelio::azookey::Entry;

void Append(std::vector<std::byte>& out, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::byte*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

template <typename T>
void Append(std::vector<std::byte>& out, T value)
{
    Append(out, &value, sizeof(T));
}

struct Word {
    std::uint16_t left;
    std::uint16_t right;
    std::uint16_t meaning;
    float value;
};

std::vector<std::byte> Record(const std::vector<Word>& words, const std::string& text)
{
    std::vector<std::byte> record;
    Append<std::uint16_t>(record, static_cast<std::uint16_t>(words.size()));
    for (const Word& word : words) {
        Append(record, word.left);
        Append(record, word.right);
        Append(record, word.meaning);
        Append(record, word.value);
    }
    Append(record, text.data(), text.size());
    return record;
}

std::vector<std::byte> LoudsText(const std::vector<std::vector<std::byte>>& records)
{
    std::vector<std::byte> file;
    Append<std::uint16_t>(file, static_cast<std::uint16_t>(records.size()));
    std::uint32_t offset = static_cast<std::uint32_t>(2 + records.size() * 4);
    for (const std::vector<std::byte>& record : records) {
        Append(file, offset);
        offset += static_cast<std::uint32_t>(record.size());
    }
    for (const std::vector<std::byte>& record : records) {
        file.insert(file.end(), record.begin(), record.end());
    }
    return file;
}

TEST(AzooKeyImport, ParsesNodesAndUsesTheRubyForEmptyWords)
{
    const std::vector<std::byte> file = LoudsText({
        Record({}, ""),
        Record({{1285, 1285, 10, -9.5f}, {1288, 1288, 501, -30.0f}},
               "\xE3\x83\xAF\xE3\x82\xBF\xE3\x82\xB7\t\xE7\xA7\x81\t"), // ワタシ 私 (empty = ruby)
    });
    std::vector<Entry> entries;
    ASSERT_TRUE(astelio::azookey::ParseLoudsText(file, entries));
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].reading, u"ワタシ");
    EXPECT_EQ(entries[0].surface, u"私");
    EXPECT_EQ(entries[0].left_id, 1285);
    EXPECT_EQ(entries[0].meaning_id, 10);
    EXPECT_FLOAT_EQ(entries[0].value, -9.5f);
    EXPECT_EQ(entries[1].surface, u"ワタシ");
    EXPECT_EQ(entries[1].right_id, 1288);
}

TEST(AzooKeyImport, RejectsMalformedFiles)
{
    std::vector<Entry> entries;
    EXPECT_FALSE(astelio::azookey::ParseLoudsText({}, entries));

    std::vector<std::byte> truncated = LoudsText({Record({{1, 1, 0, -1.0f}}, "\xE3\x82\xA2\t\xE4\xBA\x9C")});
    truncated.resize(truncated.size() - 8);
    EXPECT_FALSE(astelio::azookey::ParseLoudsText(truncated, entries));

    const std::vector<std::byte> missing_word = LoudsText({Record({{1, 1, 0, -1.0f}, {1, 1, 0, -2.0f}}, "a\tb")});
    EXPECT_FALSE(astelio::azookey::ParseLoudsText(missing_word, entries));

    std::vector<std::byte> bad_offset = LoudsText({Record({}, "")});
    bad_offset[2] = std::byte{0xFF};
    EXPECT_FALSE(astelio::azookey::ParseLoudsText(bad_offset, entries));
}

TEST(AzooKeyImport, ParsesConnectionRows)
{
    std::vector<std::byte> row;
    Append<std::int32_t>(row, -1);
    Append<float>(row, -25.0f);
    Append<std::int32_t>(row, 7);
    Append<float>(row, -1.5f);
    const auto parsed = astelio::azookey::ParseConnectionRow(row);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->size(), 2u);
    EXPECT_EQ((*parsed)[1].first, 7);
    EXPECT_FLOAT_EQ((*parsed)[1].second, -1.5f);

    row[0] = std::byte{0};
    EXPECT_FALSE(astelio::azookey::ParseConnectionRow(row)) << "the first pair must be the row default";
    EXPECT_FALSE(astelio::azookey::ParseConnectionRow(std::span<const std::byte>(row.data(), 12)));
}

TEST(AzooKeyImport, ConvertsReadingsAndCosts)
{
    EXPECT_EQ(astelio::azookey::KatakanaToHiragana(u"ワタシヴァイオリンー・ヽA"), u"わたしゔぁいおりんー・ゝA");
    EXPECT_EQ(astelio::azookey::ToCost(-9.5f), 950);
    EXPECT_EQ(astelio::azookey::ToCost(0.0f), 0);
    EXPECT_EQ(astelio::azookey::ToCost(-1000.0f), INT16_MAX);
    EXPECT_EQ(astelio::azookey::ToCost(1000.0f), INT16_MIN);
}

TEST(AzooKeyImport, ParsesTheMeaningMatrix)
{
    constexpr std::size_t kCells = std::size_t{astelio::azookey::kMeaningCount} * astelio::azookey::kMeaningCount;
    std::vector<std::byte> file;
    for (std::size_t i = 0; i < kCells; ++i) {
        Append(file, i == 3 ? -1.5f : 0.0f);
    }
    Append(file, 7.0f); // trailing data is ignored
    const auto parsed = astelio::azookey::ParseMeaningMatrix(file);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->size(), kCells);
    EXPECT_EQ((*parsed)[3], -1.5f);
    EXPECT_FALSE(astelio::azookey::ParseMeaningMatrix(std::span<const std::byte>(file.data(), kCells * 4 - 4)));

    EXPECT_TRUE(astelio::azookey::GivesMeaning(900)) << "dependent verb";
    EXPECT_TRUE(astelio::azookey::GivesMeaning(1300)) << "dependent noun";
    EXPECT_FALSE(astelio::azookey::GivesMeaning(200)) << "particle";
}

} // namespace
