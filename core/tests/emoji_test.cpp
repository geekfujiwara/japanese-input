#include "astelio/emoji.h"

#include "astelio/dictionary_builder.h"
#include "astelio/input_session.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <vector>

namespace astelio {
namespace {

TEST(Emoji, RecognisesSingleEmoji)
{
    EXPECT_TRUE(IsEmoji(u"😄"));
    EXPECT_TRUE(IsEmoji(u"❤️"));
    EXPECT_TRUE(IsEmoji(u"👍🏽")) << "skin tone";
    EXPECT_TRUE(IsEmoji(u"👨‍👩‍👧")) << "joined";
    EXPECT_TRUE(IsEmoji(u"🇯🇵")) << "flag";
    EXPECT_TRUE(IsEmoji(u"1️⃣")) << "keycap";
    EXPECT_FALSE(IsEmoji(u""));
    EXPECT_FALSE(IsEmoji(u"犬"));
    EXPECT_FALSE(IsEmoji(u"1"));
    EXPECT_FALSE(IsEmoji(u"a😄"));
    EXPECT_FALSE(IsEmoji(u"😄です"));
}

TEST(Emoji, GroupsByCategory)
{
    EXPECT_EQ(CategorizeEmoji(u"😄"), EmojiCategory::Smileys);
    EXPECT_EQ(CategorizeEmoji(u"❤️"), EmojiCategory::Smileys);
    EXPECT_EQ(CategorizeEmoji(u"👋"), EmojiCategory::People);
    EXPECT_EQ(CategorizeEmoji(u"🙏"), EmojiCategory::People);
    EXPECT_EQ(CategorizeEmoji(u"🐶"), EmojiCategory::Nature);
    EXPECT_EQ(CategorizeEmoji(u"🌸"), EmojiCategory::Nature);
    EXPECT_EQ(CategorizeEmoji(u"🍎"), EmojiCategory::Food);
    EXPECT_EQ(CategorizeEmoji(u"⚽"), EmojiCategory::Activities);
    EXPECT_EQ(CategorizeEmoji(u"🚗"), EmojiCategory::Travel);
    EXPECT_EQ(CategorizeEmoji(u"🏫"), EmojiCategory::Travel);
    EXPECT_EQ(CategorizeEmoji(u"💡"), EmojiCategory::Objects);
    EXPECT_EQ(CategorizeEmoji(u"♻️"), EmojiCategory::Symbols);
    EXPECT_EQ(CategorizeEmoji(u"🇯🇵"), EmojiCategory::Flags);
}

class EmojiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        constexpr std::uint16_t kIds = 2; // 0 edge, 1 noun
        ConnectionMatrix matrix;
        matrix.size = kIds;
        matrix.costs.assign(kIds * kIds, 0);
        matrix.word_types = {WordType::Edge, WordType::Content};
        matrix.unknown_id = 1;
        matrix.unknown_cost = 5000;
        DictionaryBuilder builder(std::move(matrix));
        builder.Add({u"えもじ", u"絵文字", 1, 1, 0, 100});
        builder.Add({u"いぬ", u"犬", 1, 1, 0, 100});
        builder.Add({u"いぬ", u"🐶", 1, 1, 0, 800});
        builder.Add({u"いぬ", u"🐕", 1, 1, 0, 900});
        builder.Add({u"こいぬ", u"🐶", 1, 1, 0, 900});
        builder.Add({u"ねこ", u"🐱", 1, 1, 0, 900});
        builder.Add({u"にっこり", u"😄", 1, 1, 0, 900});
        builder.Add({u"えがお", u"😊", 1, 1, 0, 900});
        builder.Add({u"はーと", u"❤️", 1, 1, 0, 900});
        builder.Add({u"くるま", u"🚗", 1, 1, 0, 900});
        builder.Add({u"くるま", u"車", 1, 1, 0, 100});
        bytes_ = builder.Build();
        dictionary_ = SystemDictionary::Open(bytes_);
        ASSERT_TRUE(dictionary_);
        catalog_ = EmojiCatalog::FromDictionary(*dictionary_);
        converter_.emplace(*dictionary_);
        session_.SetConverter(&*converter_);
        session_.SetEmojiCatalog([this] { return &catalog_; });
    }

    void Type(std::u16string_view keys)
    {
        for (char16_t c : keys) {
            session_.Handle({KeyKind::Character, c});
        }
    }

    SessionOutput Press(KeyKind kind, bool shift = false) { return session_.Handle({kind, 0, shift}); }

    std::vector<std::byte> bytes_;
    std::optional<SystemDictionary> dictionary_;
    EmojiCatalog catalog_;
    std::optional<Converter> converter_;
    InputSession session_{RomajiTable::Default(), CharacterSettings{}};
};

TEST_F(EmojiTest, CatalogCollectsTheDictionaryEmoji)
{
    EXPECT_EQ(catalog_.size(), 7u) << "🐶 is listed once";
    std::vector<std::u16string> nature;
    for (std::size_t index : catalog_.InCategory(EmojiCategory::Nature)) {
        nature.push_back(catalog_.at(index).text);
    }
    EXPECT_EQ(nature, (std::vector<std::u16string>{u"🐕", u"🐱", u"🐶"})) << "code point order";
    EXPECT_TRUE(catalog_.InCategory(EmojiCategory::Recent).empty());
}

// T-B13-3
TEST_F(EmojiTest, SearchMatchesReadingPrefixesFirst)
{
    std::vector<std::u16string> found;
    for (std::size_t index : catalog_.Search(u"いぬ", 10)) {
        found.push_back(catalog_.at(index).text);
    }
    EXPECT_EQ(found, (std::vector<std::u16string>{u"🐶", u"🐕"}));
    EXPECT_TRUE(catalog_.Search(u"ぞう", 10).empty());
    EXPECT_EQ(catalog_.Search(u"い", 1).size(), 1u);
}

// T-B13-1: えもじ offers the palette, and Space still converts it.
TEST_F(EmojiTest, EmojiOffersThePaletteAndSpaceStillConverts)
{
    Type(u"emoj");
    EXPECT_FALSE(session_.EmojiPaletteOffered());
    Type(u"i");
    ASSERT_TRUE(session_.EmojiPaletteOffered());
    const EmojiPaletteView offered = session_.EmojiPalette();
    EXPECT_FALSE(offered.active);
    EXPECT_EQ(offered.selected, kNoEmojiSelection);
    EXPECT_EQ(offered.category, EmojiCategory::Smileys) << "no history yet";
    EXPECT_EQ(offered.items, (std::vector<std::u16string>{u"❤️", u"😄", u"😊"}));

    Press(KeyKind::Space);
    EXPECT_TRUE(session_.Converting());
    EXPECT_EQ(session_.CompositionText(), u"絵文字");
    EXPECT_FALSE(session_.EmojiPaletteOffered());
}

// T-B13-2: Tab opens it; arrows and Tab move; Enter commits; Esc goes back.
TEST_F(EmojiTest, KeysNavigateTheGridAndCategories)
{
    Type(u"emoji");
    Press(KeyKind::Tab);
    ASSERT_TRUE(session_.EmojiPaletteActive());
    EXPECT_TRUE(session_.Predictions().empty());
    EXPECT_EQ(session_.CompositionText(), u"えもじ");
    EmojiPaletteView view = session_.EmojiPalette();
    EXPECT_TRUE(view.active);
    EXPECT_EQ(view.selected, 0u);

    Press(KeyKind::Right);
    EXPECT_EQ(session_.EmojiPalette().selected, 1u);
    Press(KeyKind::Right);
    Press(KeyKind::Right);
    EXPECT_EQ(session_.EmojiPalette().selected, 2u) << "stops at the last emoji";
    Press(KeyKind::Left);
    EXPECT_EQ(session_.EmojiPalette().selected, 1u);

    Press(KeyKind::Tab); // People: none in this dictionary
    EXPECT_EQ(session_.EmojiPalette().category, EmojiCategory::People);
    EXPECT_TRUE(session_.EmojiPalette().items.empty());
    EXPECT_FALSE(Press(KeyKind::Enter).composition_changed) << "nothing to pick";
    Press(KeyKind::Tab);
    view = session_.EmojiPalette();
    EXPECT_EQ(view.category, EmojiCategory::Nature);
    EXPECT_EQ(view.selected, 0u);
    Press(KeyKind::Tab, true);
    Press(KeyKind::Tab, true);
    EXPECT_EQ(session_.EmojiPalette().category, EmojiCategory::Smileys);

    Press(KeyKind::Escape);
    EXPECT_FALSE(session_.EmojiPaletteActive());
    EXPECT_EQ(session_.CompositionText(), u"えもじ");
    Press(KeyKind::Down);
    ASSERT_TRUE(session_.EmojiPaletteActive()) << "Down opens it too";
    Press(KeyKind::Right);
    const SessionOutput committed = Press(KeyKind::Enter);
    EXPECT_EQ(committed.commit, u"😄");
    EXPECT_TRUE(committed.recent_emoji_changed);
    EXPECT_FALSE(session_.Composing());
    EXPECT_FALSE(session_.EmojiPaletteActive());
}

// T-B13-3: typing inside the palette searches by reading.
TEST_F(EmojiTest, TypingSearchesInsideThePalette)
{
    Type(u"emoji");
    Press(KeyKind::Tab);
    Type(u"in");
    EmojiPaletteView view = session_.EmojiPalette();
    EXPECT_EQ(view.query, u"いn");
    EXPECT_TRUE(view.items.size() >= 2u) << "い matches 🐶 🐕 and more";
    Type(u"u");
    view = session_.EmojiPalette();
    EXPECT_EQ(view.query, u"いぬ");
    EXPECT_EQ(view.items, (std::vector<std::u16string>{u"🐶", u"🐕"}));
    EXPECT_EQ(session_.CompositionText(), u"えもじ") << "the search does not touch the document text";

    Press(KeyKind::Backspace);
    EXPECT_EQ(session_.EmojiPalette().query, u"い");
    Press(KeyKind::Backspace);
    EXPECT_TRUE(session_.EmojiPalette().query.empty());
    EXPECT_TRUE(session_.EmojiPaletteActive());
    Press(KeyKind::Backspace);
    EXPECT_FALSE(session_.EmojiPaletteActive()) << "Backspace on an empty search closes it";

    Press(KeyKind::Tab);
    Type(u"inu");
    EXPECT_EQ(Press(KeyKind::Enter).commit, u"🐶");
}

// T-B13-4: picked emoji become the history, newest first.
TEST_F(EmojiTest, PickedEmojiAreRememberedAsHistory)
{
    session_.SetRecentEmoji({u"🚗"});
    Type(u"emoji");
    EXPECT_EQ(session_.EmojiPalette().category, EmojiCategory::Recent);
    EXPECT_EQ(session_.EmojiPalette().items, (std::vector<std::u16string>{u"🚗"}));

    ASSERT_TRUE(session_.SelectEmojiCategory(EmojiCategory::Nature)) << "a click opens the palette";
    EXPECT_TRUE(session_.EmojiPaletteActive());
    const SessionOutput picked = session_.PickEmoji(1);
    EXPECT_EQ(picked.commit, u"🐱");
    EXPECT_TRUE(picked.recent_emoji_changed);
    EXPECT_EQ(session_.RecentEmoji(), (std::vector<std::u16string>{u"🐱", u"🚗"}));

    Type(u"emoji");
    Press(KeyKind::Tab);
    Press(KeyKind::Right);
    EXPECT_EQ(Press(KeyKind::Enter).commit, u"🚗");
    EXPECT_EQ(session_.RecentEmoji(), (std::vector<std::u16string>{u"🚗", u"🐱"})) << "moved to the front";

    std::vector<std::u16string> many(40, u"x");
    session_.SetRecentEmoji(many);
    EXPECT_EQ(session_.RecentEmoji().size(), InputSession::kMaxRecentEmoji);
}

TEST_F(EmojiTest, NoPaletteWithoutACatalog)
{
    session_.SetEmojiCatalog(nullptr);
    Type(u"emoji");
    EXPECT_FALSE(session_.EmojiPaletteOffered());
    Press(KeyKind::Tab);
    EXPECT_FALSE(session_.EmojiPaletteActive());
}

} // namespace
} // namespace astelio
