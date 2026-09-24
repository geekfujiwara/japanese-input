#include "astelio/modifier_tap_tracker.h"

#include <gtest/gtest.h>

namespace astelio {
namespace {

class ModifierTapTrackerTest : public ::testing::TestWithParam<ModifierSide> {};

TEST_P(ModifierTapTrackerTest, PressAndReleaseIsATap)
{
    ModifierTapTracker tracker;
    tracker.Press(GetParam(), 0);
    EXPECT_TRUE(tracker.Release(GetParam(), 100));
}

TEST_P(ModifierTapTrackerTest, ChordIsNotATap)
{
    ModifierTapTracker tracker;
    tracker.Press(GetParam(), 0);
    tracker.MarkChordUsed();
    EXPECT_FALSE(tracker.Release(GetParam(), 100));
}

// T-R04-2
TEST_P(ModifierTapTrackerTest, LongHoldIsNotATap)
{
    ModifierTapTracker tracker(1000);
    tracker.Press(GetParam(), 0);
    EXPECT_FALSE(tracker.Release(GetParam(), 2000));
}

// T-R04-4
TEST_P(ModifierTapTrackerTest, TapAfterChordIsDetected)
{
    ModifierTapTracker tracker;
    tracker.Press(GetParam(), 0);
    tracker.MarkChordUsed();
    EXPECT_FALSE(tracker.Release(GetParam(), 100));

    tracker.Press(GetParam(), 200);
    EXPECT_TRUE(tracker.Release(GetParam(), 300));
}

INSTANTIATE_TEST_SUITE_P(BothSides, ModifierTapTrackerTest,
                         ::testing::Values(ModifierSide::Left, ModifierSide::Right));

TEST(ModifierTapTracker, RepeatedKeyDownDoesNotResetChordState)
{
    ModifierTapTracker tracker;
    tracker.Press(ModifierSide::Left, 0);
    tracker.MarkChordUsed();
    tracker.Press(ModifierSide::Left, 50);
    EXPECT_FALSE(tracker.Release(ModifierSide::Left, 100));
}

// T-R04-3
TEST(ModifierTapTracker, BothModifiersAreNotTaps)
{
    ModifierTapTracker tracker;
    tracker.Press(ModifierSide::Left, 0);
    tracker.Press(ModifierSide::Right, 10);
    EXPECT_FALSE(tracker.Release(ModifierSide::Right, 20));
    EXPECT_FALSE(tracker.Release(ModifierSide::Left, 30));
}

TEST(ModifierTapTracker, ReleaseWithoutPressIsNotATap)
{
    ModifierTapTracker tracker;
    EXPECT_FALSE(tracker.Release(ModifierSide::Left, 0));
}

} // namespace
} // namespace astelio
