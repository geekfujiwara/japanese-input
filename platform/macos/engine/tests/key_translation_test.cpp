#include "astelio/mac/key_translation.h"

#include <gtest/gtest.h>

namespace astelio::mac {
namespace {

std::optional<KeyEvent> Key(std::uint16_t code, Modifiers modifiers = {})
{
    return TranslateKey(code, modifiers);
}

TEST(MacKeyTranslation, LettersFollowTheUsLayoutByKeyCode)
{
    ASSERT_TRUE(Key(0x00));
    EXPECT_EQ(Key(0x00)->kind, KeyKind::Character);
    EXPECT_EQ(Key(0x00)->character, u'a');
    EXPECT_EQ(Key(0x26)->character, u'j');
    EXPECT_EQ(Key(0x2C)->character, u'/');
    EXPECT_EQ(Key(0x1B)->character, u'-');
}

TEST(MacKeyTranslation, ShiftAndCapsLock)
{
    EXPECT_EQ(Key(0x00, {.shift = true})->character, u'A');
    EXPECT_EQ(Key(0x00, {.caps_lock = true})->character, u'A');
    EXPECT_EQ(Key(0x00, {.shift = true, .caps_lock = true})->character, u'a');
    EXPECT_EQ(Key(0x12, {.caps_lock = true})->character, u'1');
    EXPECT_EQ(Key(0x12, {.shift = true})->character, u'!');
}

TEST(MacKeyTranslation, EditingKeys)
{
    EXPECT_EQ(Key(0x31)->kind, KeyKind::Space);
    EXPECT_EQ(Key(0x24)->kind, KeyKind::Enter);
    EXPECT_EQ(Key(0x4C)->kind, KeyKind::Enter);
    EXPECT_EQ(Key(0x33)->kind, KeyKind::Backspace);
    EXPECT_EQ(Key(0x75)->kind, KeyKind::Delete);
    EXPECT_EQ(Key(0x35)->kind, KeyKind::Escape);
    EXPECT_EQ(Key(0x30)->kind, KeyKind::Tab);
    EXPECT_EQ(Key(0x61)->kind, KeyKind::F6);
    EXPECT_EQ(Key(0x6D)->kind, KeyKind::F10);
    const std::optional<KeyEvent> left = Key(0x7B, {.shift = true, .function = true});
    ASSERT_TRUE(left);
    EXPECT_EQ(left->kind, KeyKind::Left);
    EXPECT_TRUE(left->shift);
}

TEST(MacKeyTranslation, ShortcutsAreLeftToTheApp)
{
    EXPECT_FALSE(Key(0x08, {.command = true}));
    EXPECT_FALSE(Key(0x00, {.option = true}));
    EXPECT_FALSE(Key(0x00, {.control = true}));
    EXPECT_FALSE(Key(0x37)); // Command itself
}

TEST(MacKeyTranslation, ControlKeysTheSessionUses)
{
    const std::optional<KeyEvent> forget = Key(0x75, {.control = true});
    ASSERT_TRUE(forget);
    EXPECT_EQ(forget->kind, KeyKind::Delete);
    EXPECT_TRUE(forget->control);
    EXPECT_EQ(Key(0x7D, {.control = true})->kind, KeyKind::Down);
    EXPECT_EQ(Key(0x33, {.control = true})->kind, KeyKind::Backspace);
}

TEST(MacKeyTranslation, Keypad)
{
    EXPECT_EQ(Key(0x52)->character, u'0');
    EXPECT_EQ(Key(0x5C)->character, u'9');
    EXPECT_EQ(Key(0x41)->character, u'.');
}

} // namespace
} // namespace astelio::mac
