#include "astelio/composer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

namespace astelio {
namespace {

// F-01 (always-on part): long random key sequences keep the composer consistent.
TEST(ComposerRobustness, RandomKeySequencesKeepTheCursorInRange)
{
    std::mt19937 random(20260924);
    std::uniform_int_distribution<int> operation(0, 99);
    std::uniform_int_distribution<int> key(0x20, 0x7F);

    Composer composer(RomajiTable::Default(), CharacterSettings{});
    for (int i = 0; i < 10000; ++i) {
        const int op = operation(random);
        if (op < 80) {
            composer.InsertKey(static_cast<char16_t>(key(random)));
        } else if (op < 85) {
            composer.Backspace();
        } else if (op < 89) {
            composer.Delete();
        } else if (op < 93) {
            composer.MoveLeft();
        } else if (op < 97) {
            composer.MoveRight();
        } else if (op < 98) {
            composer.ExitTemporaryAlphanumeric();
        } else {
            composer.Commit();
        }
        ASSERT_LE(composer.Cursor(), composer.Text().size());
        ASSERT_EQ(composer.Empty(), composer.Text().empty());
    }
}

} // namespace
} // namespace astelio
