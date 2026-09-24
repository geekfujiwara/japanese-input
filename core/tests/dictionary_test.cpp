#include "astelio/dictionary.h"
#include "astelio/dictionary_builder.h"

#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using astelio::ConnectionMatrix;
using astelio::DictionaryBuilder;
using astelio::DictionaryEntry;
using astelio::DictionaryError;
using astelio::DictionarySourceEntry;
using astelio::SourceError;
using astelio::SystemDictionary;

ConnectionMatrix Matrix()
{
    ConnectionMatrix matrix{4, 0, 3, std::vector<std::int16_t>(16, 100)};
    matrix.costs[1 * 4 + 2] = 7;
    return matrix;
}

DictionarySourceEntry Word(std::u16string reading, std::u16string surface, std::int16_t cost, std::uint16_t id = 1)
{
    return DictionarySourceEntry{std::move(reading), std::move(surface), id, id, 0, cost};
}

std::vector<std::byte> BuildSample()
{
    DictionaryBuilder builder(Matrix());
    EXPECT_TRUE(builder.Add(Word(u"わたし", u"私", 300)));
    EXPECT_TRUE(builder.Add(Word(u"わたし", u"渡し", 900, 2)));
    EXPECT_TRUE(builder.Add(Word(u"わたし", u"わたし", 500)));
    EXPECT_TRUE(builder.Add(Word(u"わた", u"綿", 600)));
    EXPECT_TRUE(builder.Add(Word(u"わ", u"輪", 800)));
    EXPECT_TRUE(builder.Add(Word(u"は", u"は", 50, 2)));
    EXPECT_TRUE(builder.Add(Word(u"わたしは", u"私は", 1000)));
    EXPECT_TRUE(builder.Add(Word(u"わたし", u"私", 400))); // duplicate, costlier
    return builder.Build();
}

std::vector<std::u16string> Surfaces(const std::vector<DictionaryEntry>& entries)
{
    std::vector<std::u16string> surfaces;
    for (const DictionaryEntry& entry : entries) {
        surfaces.emplace_back(entry.surface);
    }
    return surfaces;
}

TEST(SystemDictionary, LooksUpAReadingCheapestFirstAndDropsDuplicates)
{
    const std::vector<std::byte> bytes = BuildSample();
    const std::optional<SystemDictionary> dictionary = SystemDictionary::Open(bytes);
    ASSERT_TRUE(dictionary);
    EXPECT_EQ(dictionary->reading_count(), 5u);
    EXPECT_EQ(dictionary->entry_count(), 7u);

    const std::vector<DictionaryEntry> entries = dictionary->Lookup(u"わたし");
    EXPECT_EQ(Surfaces(entries), (std::vector<std::u16string>{u"私", u"わたし", u"渡し"}));
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].cost, 300);
    EXPECT_EQ(entries[2].left_id, 2);
    EXPECT_TRUE(dictionary->Lookup(u"わたしが").empty());
    EXPECT_TRUE(dictionary->Lookup(u"").empty());
}

TEST(SystemDictionary, CommonPrefixSearchVisitsEveryPrefixShortestFirst)
{
    const std::vector<std::byte> bytes = BuildSample();
    const std::optional<SystemDictionary> dictionary = SystemDictionary::Open(bytes);
    ASSERT_TRUE(dictionary);

    std::vector<std::pair<std::size_t, std::u16string>> found;
    dictionary->CommonPrefixSearch(u"わたしはがくせい", [&found](std::size_t length, const DictionaryEntry& entry) {
        found.emplace_back(length, std::u16string(entry.surface));
    });
    const std::vector<std::pair<std::size_t, std::u16string>> expected = {
        {1, u"輪"}, {2, u"綿"}, {3, u"私"}, {3, u"わたし"}, {3, u"渡し"}, {4, u"私は"}};
    EXPECT_EQ(found, expected);

    found.clear();
    dictionary->CommonPrefixSearch(u"はし", [&found](std::size_t length, const DictionaryEntry& entry) {
        found.emplace_back(length, std::u16string(entry.surface));
    });
    EXPECT_EQ(found, (std::vector<std::pair<std::size_t, std::u16string>>{{1, u"は"}}));
}

TEST(SystemDictionary, ConnectionCostsComeFromTheMatrix)
{
    const std::vector<std::byte> bytes = BuildSample();
    const std::optional<SystemDictionary> dictionary = SystemDictionary::Open(bytes);
    ASSERT_TRUE(dictionary);
    EXPECT_EQ(dictionary->id_count(), 4);
    EXPECT_EQ(dictionary->bos_id(), 0);
    EXPECT_EQ(dictionary->eos_id(), 3);
    EXPECT_EQ(dictionary->ConnectionCost(1, 2), 7);
    EXPECT_EQ(dictionary->ConnectionCost(2, 1), 100);
    EXPECT_EQ(dictionary->ConnectionCost(4, 0), INT16_MAX);
}

TEST(SystemDictionary, BuilderRejectsEntriesOutsideTheFormat)
{
    DictionaryBuilder builder(Matrix());
    EXPECT_FALSE(builder.Add(Word(u"", u"x", 1)));
    EXPECT_FALSE(builder.Add(Word(u"x", u"", 1)));
    EXPECT_FALSE(builder.Add(Word(u"x", u"x", 1, 4)));
    EXPECT_FALSE(builder.Add(Word(std::u16string(256, u'あ'), u"x", 1)));
    EXPECT_FALSE(builder.Add(Word(u"a\tb", u"x", 1)));
    EXPECT_EQ(builder.size(), 0u);

    const std::vector<std::byte> bytes = builder.Build();
    const std::optional<SystemDictionary> dictionary = SystemDictionary::Open(bytes);
    ASSERT_TRUE(dictionary) << "an empty dictionary is still valid";
    EXPECT_TRUE(dictionary->Lookup(u"x").empty());
}

// T-D01-1, F-02: broken files are rejected without crashing.
TEST(SystemDictionary, RejectsTruncatedAndCorruptFiles)
{
    const std::vector<std::byte> bytes = BuildSample();
    DictionaryError error{};
    for (std::size_t size = 0; size < bytes.size(); size += 7) {
        const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        EXPECT_FALSE(SystemDictionary::Open(truncated, &error)) << "size " << size;
    }

    std::vector<std::byte> bad_magic = bytes;
    bad_magic[0] = std::byte{'X'};
    EXPECT_FALSE(SystemDictionary::Open(bad_magic, &error));
    EXPECT_EQ(error, DictionaryError::BadMagic);

    std::vector<std::byte> bad_version = bytes;
    bad_version[8] = std::byte{9};
    EXPECT_FALSE(SystemDictionary::Open(bad_version, &error));
    EXPECT_EQ(error, DictionaryError::UnsupportedVersion);

    std::vector<std::byte> shifted(bytes.size() + 4);
    std::memcpy(shifted.data() + 1, bytes.data(), bytes.size());
    EXPECT_FALSE(SystemDictionary::Open(std::span<const std::byte>(shifted.data() + 1, bytes.size()), &error));
    EXPECT_EQ(error, DictionaryError::Misaligned);
}

TEST(SystemDictionary, RandomCorruptionNeverReadsOutOfBounds)
{
    const std::vector<std::byte> bytes = BuildSample();
    std::mt19937 random(20260925);
    std::uniform_int_distribution<std::size_t> position(0, bytes.size() - 1);
    std::uniform_int_distribution<int> value(0, 255);
    for (int round = 0; round < 2000; ++round) {
        std::vector<std::byte> corrupt = bytes;
        for (int i = 0; i < 4; ++i) {
            corrupt[position(random)] = static_cast<std::byte>(value(random));
        }
        const std::optional<SystemDictionary> dictionary = SystemDictionary::Open(corrupt);
        if (!dictionary) {
            continue;
        }
        std::size_t visited = 0;
        dictionary->CommonPrefixSearch(u"わたしは", [&visited](std::size_t length, const DictionaryEntry& entry) {
            visited += length + entry.surface.size();
        });
        EXPECT_LE(dictionary->Lookup(u"わたし").size(), dictionary->entry_count());
        static_cast<void>(dictionary->ConnectionCost(dictionary->bos_id(), dictionary->eos_id()));
    }
}

TEST(DictionarySource, ParsesTheConnectionMatrixWithRowDefaults)
{
    SourceError error;
    const std::optional<ConnectionMatrix> matrix = astelio::ParseConnectionSource(
        "\xEF\xBB\xBF# comment\r\nsize\t3\t0\t2\r\n1\t2\t-5\n1\t*\t40\n0\t0\t9\ntype\t2\tsuffix\nunknown\t1\t700\n", &error);
    ASSERT_TRUE(matrix) << error.line << ": " << error.message;
    EXPECT_EQ(matrix->size, 3);
    EXPECT_EQ(matrix->eos_id, 2);
    EXPECT_EQ(matrix->costs, (std::vector<std::int16_t>{9, 0, 0, 40, 40, -5, 0, 0, 0}));
    EXPECT_EQ(matrix->word_types, (std::vector<astelio::WordType>{astelio::WordType::Content,
                                                                  astelio::WordType::Content,
                                                                  astelio::WordType::Suffix}));
    EXPECT_EQ(matrix->unknown_id, 1);
    EXPECT_EQ(matrix->unknown_cost, 700);
    EXPECT_FALSE(astelio::ParseConnectionSource("size\t3\t0\t2\ntype\t1\tverb\n", &error));

    DictionaryBuilder builder(*matrix);
    const std::vector<std::byte> bytes = builder.Build();
    const std::optional<SystemDictionary> dictionary = SystemDictionary::Open(bytes);
    ASSERT_TRUE(dictionary);
    EXPECT_EQ(dictionary->word_type(2), astelio::WordType::Suffix);
    EXPECT_EQ(dictionary->word_type(0), astelio::WordType::Content);
    EXPECT_EQ(dictionary->unknown_id(), 1);
    EXPECT_EQ(dictionary->unknown_cost(), 700);
}

TEST(DictionarySource, ReportsTheLineOfABadConnectionSource)
{
    SourceError error;
    EXPECT_FALSE(astelio::ParseConnectionSource("1\t2\t3\n", &error));
    EXPECT_EQ(error.line, 1u);
    EXPECT_FALSE(astelio::ParseConnectionSource("size\t2\t0\t1\n\n1\t2\t3\n", &error));
    EXPECT_EQ(error.line, 3u);
    EXPECT_FALSE(astelio::ParseConnectionSource("size\t2\t0\t1\n1\t1\t40000\n", &error));
    EXPECT_FALSE(astelio::ParseConnectionSource("size\t0\t0\t0\n", &error));
    EXPECT_FALSE(astelio::ParseConnectionSource("", &error));
}

TEST(DictionarySource, ParsesWordsAndSkipsBadLines)
{
    std::vector<SourceError> errors;
    const std::vector<DictionarySourceEntry> entries = astelio::ParseWordSource(
        "# reading\tsurface\tleft\tright\tmeaning\tcost\n"
        "\xE3\x82\x8F\xE3\x81\x9F\xE3\x81\x97\t\xE7\xA7\x81\t1\t1\t5\t300\n" // わたし 私
        "a\tb\t1\t1\t0\n"                                                   // 5 fields
        "a\tb\t9\t1\t0\t1\n"                                                // id out of range
        "a\tb\t1\t1\t0\tx\n"                                                // cost
        "\xFF\tb\t1\t1\t0\t1\n",                                            // invalid UTF-8
        4, errors);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].reading, u"わたし");
    EXPECT_EQ(entries[0].surface, u"私");
    EXPECT_EQ(entries[0].meaning_id, 5);
    EXPECT_EQ(entries[0].cost, 300);
    ASSERT_EQ(errors.size(), 4u);
    EXPECT_EQ(errors[0].line, 3u);
    EXPECT_EQ(errors[3].line, 6u);
}

} // namespace
