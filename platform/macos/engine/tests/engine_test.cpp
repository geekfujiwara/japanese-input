#include "astelio/mac/engine.h"

#include "test_dictionary.h"

#include <gtest/gtest.h>

#include <sys/stat.h>

namespace astelio::mac {
namespace {

TEST(MacEngine, MapsTheSystemDictionary)
{
    testing::TemporaryFiles files;
    Engine engine(files.WriteDictionary(), files.data());
    ASSERT_NE(engine.dictionary(), nullptr);
    EXPECT_NE(engine.EmojiCatalogForDictionary(), nullptr);
}

TEST(MacEngine, WorksWithoutTheDictionary)
{
    testing::TemporaryFiles files;
    Engine engine(files.directory() / "missing.dic", files.data());
    EXPECT_EQ(engine.dictionary(), nullptr);
    EXPECT_EQ(engine.EmojiCatalogForDictionary(), nullptr);
}

TEST(MacEngine, RejectsABrokenDictionary)
{
    testing::TemporaryFiles files;
    const std::filesystem::path path = files.directory() / "broken.dic";
    ASSERT_TRUE(Engine::WriteFileAtomically(path, "not a dictionary"));
    Engine engine(path, files.data());
    EXPECT_EQ(engine.dictionary(), nullptr);
}

// SEC-05: the history is written as a whole and readable by this user only.
TEST(MacEngine, SavesTheHistoryForThisUserOnly)
{
    testing::TemporaryFiles files;
    {
        Engine engine({}, files.data());
        engine.learning().Record(LearningHistory::Kind::Conversion, u"わたし", u"渡し");
        ASSERT_TRUE(engine.SaveLearning());
    }
    struct stat info {};
    ASSERT_EQ(stat((files.data() / "learning.tsv").c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0600u);
    ASSERT_EQ(stat(files.data().c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0700u);
    EXPECT_FALSE(std::filesystem::exists(files.data() / "learning.tsv.tmp"));

    Engine reloaded({}, files.data());
    EXPECT_TRUE(reloaded.learning().Contains(LearningHistory::Kind::Conversion, u"わたし", u"渡し"));
    ASSERT_TRUE(reloaded.ClearLearning());
    Engine cleared({}, files.data());
    EXPECT_TRUE(cleared.learning().Empty());
}

TEST(MacEngine, ReloadsTheUserDictionaryWhenTheFileChanges)
{
    testing::TemporaryFiles files;
    Engine engine({}, files.data());
    EXPECT_EQ(engine.user_dictionary(), nullptr);
    EXPECT_FALSE(engine.RefreshUserDictionary());

    UserDictionary words;
    ASSERT_TRUE(words.Add({u"あすてりお", u"Astelio", UserDictionary::PartOfSpeech::ProperNoun, {}}));
    ASSERT_TRUE(Engine::WriteFileAtomically(engine.UserDictionaryPath(), words.Serialize()));
    EXPECT_TRUE(engine.RefreshUserDictionary());
    ASSERT_NE(engine.user_dictionary(), nullptr);
    EXPECT_EQ(engine.user_dictionary()->Lookup(u"あすてりお"), std::vector<std::u16string>{u"Astelio"});
}

TEST(MacEngine, RecentEmoji)
{
    testing::TemporaryFiles files;
    Engine engine({}, files.data());
    EXPECT_TRUE(engine.LoadRecentEmoji().empty());
    ASSERT_TRUE(engine.SaveRecentEmoji({u"🐶", u"😄"}));
    EXPECT_EQ(engine.LoadRecentEmoji(), (std::vector<std::u16string>{u"🐶", u"😄"}));
}

TEST(MacEngine, IgnoresFilesThatAreTooLarge)
{
    testing::TemporaryFiles files;
    const std::filesystem::path path = files.directory() / "large.txt";
    ASSERT_TRUE(Engine::WriteFileAtomically(path, std::string(100, 'x')));
    EXPECT_FALSE(Engine::ReadFile(path, 10));
    EXPECT_EQ(Engine::ReadFile(path, 100), std::string(100, 'x'));
}

} // namespace
} // namespace astelio::mac
