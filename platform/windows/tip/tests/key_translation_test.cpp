#include "key_translation.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace astelio::tip {
namespace {

std::uint32_t LParam(std::uint8_t scan_code, bool extended = false)
{
    return 1u | (static_cast<std::uint32_t>(scan_code) << 16) | (extended ? 1u << 24 : 0u);
}

char16_t Character(std::uint32_t vk, std::uint8_t scan_code, Modifiers modifiers = {})
{
    const std::optional<KeyEvent> key = TranslateKey(vk, LParam(scan_code), modifiers);
    return key && key->kind == KeyKind::Character ? key->character : 0;
}

TEST(KeyTranslation, LettersFollowShiftAndCapsLock)
{
    EXPECT_EQ(Character('A', 0x1E), u'a');
    EXPECT_EQ(Character('A', 0x1E, {.shift = true}), u'A');
    EXPECT_EQ(Character('A', 0x1E, {.caps_lock = true}), u'A');
    EXPECT_EQ(Character('A', 0x1E, {.shift = true, .caps_lock = true}), u'a');
}

// T-R02-1: the US layout is read from the scan code, whatever virtual key Windows reports.
TEST(KeyTranslation, ShiftedSymbolsUseTheUsLayout)
{
    EXPECT_EQ(Character(VK_OEM_PLUS, 0x0D, {.shift = true}), u'+');
    EXPECT_EQ(Character('9', 0x0A, {.shift = true}), u'(');
    EXPECT_EQ(Character('0', 0x0B, {.shift = true}), u')');
    EXPECT_EQ(Character(VK_OEM_2, 0x35, {.shift = true}), u'?');
    EXPECT_EQ(Character(VK_OEM_7, 0x28), u'\'');
    EXPECT_EQ(Character('2', 0x03, {.shift = true}), u'@');
    EXPECT_EQ(Character('2', 0x03, {.caps_lock = true}), u'2');
}

TEST(KeyTranslation, NumpadProducesDigitsAndOperators)
{
    EXPECT_EQ(TranslateKey(VK_NUMPAD7, LParam(0x47), {})->character, u'7');
    EXPECT_EQ(TranslateKey(VK_DIVIDE, LParam(0x35, true), {})->character, u'/');
    EXPECT_EQ(TranslateKey(VK_ADD, LParam(0x4E), {})->character, u'+');
}

TEST(KeyTranslation, EditingKeys)
{
    EXPECT_EQ(TranslateKey(VK_RETURN, LParam(0x1C), {})->kind, KeyKind::Enter);
    EXPECT_EQ(TranslateKey(VK_ESCAPE, LParam(0x01), {})->kind, KeyKind::Escape);
    EXPECT_EQ(TranslateKey(VK_BACK, LParam(0x0E), {})->kind, KeyKind::Backspace);
    EXPECT_EQ(TranslateKey(VK_SPACE, LParam(0x39), {})->kind, KeyKind::Space);
    EXPECT_EQ(TranslateKey(VK_LEFT, LParam(0x4B, true), {})->kind, KeyKind::Left);
    EXPECT_FALSE(TranslateKey(VK_LEFT, LParam(0x4B, true), {})->shift);
    EXPECT_TRUE(TranslateKey(VK_RIGHT, LParam(0x4D, true), {.shift = true})->shift);
    EXPECT_EQ(TranslateKey(VK_UP, LParam(0x48, true), {})->kind, KeyKind::Up);
    EXPECT_EQ(TranslateKey(VK_DOWN, LParam(0x50, true), {})->kind, KeyKind::Down);
    EXPECT_EQ(TranslateKey(VK_NEXT, LParam(0x51, true), {})->kind, KeyKind::PageDown);
    EXPECT_EQ(TranslateKey(VK_PRIOR, LParam(0x49, true), {})->kind, KeyKind::PageUp);
    EXPECT_EQ(TranslateKey(VK_TAB, LParam(0x0F), {})->kind, KeyKind::Tab);
    EXPECT_EQ(TranslateKey(VK_F6, LParam(0x40), {})->kind, KeyKind::F6);
    EXPECT_EQ(TranslateKey(VK_F10, LParam(0x44), {})->kind, KeyKind::F10);
    EXPECT_TRUE(TranslateKey(VK_TAB, LParam(0x0F), {.shift = true})->shift);
}

TEST(KeyTranslation, ShortcutsAndOtherKeysAreNotHandled)
{
    EXPECT_FALSE(TranslateKey('C', LParam(0x2E), {.control = true}).has_value());
    EXPECT_FALSE(TranslateKey('F', LParam(0x21), {.alt = true}).has_value());
    EXPECT_FALSE(TranslateKey('R', LParam(0x13), {.windows = true}).has_value());
    EXPECT_FALSE(TranslateKey(VK_F5, LParam(0x3F), {}).has_value());
    EXPECT_FALSE(TranslateKey(VK_SHIFT, LParam(0x2A), {}).has_value());
    EXPECT_FALSE(TranslateKey(VK_HOME, LParam(0x47, true), {}).has_value());
}

} // namespace
} // namespace astelio::tip
