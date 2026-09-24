#include "astelio/utf.h"

#include <gtest/gtest.h>

namespace astelio {
namespace {

TEST(Utf, RoundTripsBmpAndSupplementaryCharacters)
{
    const std::u16string text = u"aあ𠮷😀";
    const std::string utf8 = Utf16ToUtf8(text);
    EXPECT_EQ(utf8, "a\xE3\x81\x82\xF0\xA0\xAE\xB7\xF0\x9F\x98\x80");
    EXPECT_EQ(Utf8ToUtf16(utf8), text);
}

TEST(Utf, RejectsMalformedUtf8)
{
    EXPECT_FALSE(Utf8ToUtf16("\xC0\xAF").has_value());         // overlong
    EXPECT_FALSE(Utf8ToUtf16("\xED\xA0\x80").has_value());     // surrogate
    EXPECT_FALSE(Utf8ToUtf16("\xE3\x81").has_value());         // truncated
    EXPECT_FALSE(Utf8ToUtf16("\xF4\x90\x80\x80").has_value()); // above U+10FFFF
    EXPECT_FALSE(Utf8ToUtf16("\x80").has_value());             // stray continuation
}

TEST(Utf, WritesUnpairedSurrogatesAsReplacementCharacter)
{
    const std::u16string lone(1, static_cast<char16_t>(0xD800));
    EXPECT_EQ(Utf16ToUtf8(lone), "\xEF\xBF\xBD");
}

} // namespace
} // namespace astelio
