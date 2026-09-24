#include "astelio/modifier_tap_tracker.h"

namespace astelio {

void ModifierTapTracker::Press(ModifierSide side, std::uint64_t time_ms)
{
    KeyState& state = State(side);
    if (state.down) {
        return; // auto-repeat
    }
    KeyState& other = Other(side);
    state.down = true;
    state.used = other.down;
    state.pressed_at = time_ms;
    other.used = other.used || other.down;
}

void ModifierTapTracker::MarkChordUsed()
{
    left_.used = left_.used || left_.down;
    right_.used = right_.used || right_.down;
}

bool ModifierTapTracker::Release(ModifierSide side, std::uint64_t time_ms)
{
    KeyState& state = State(side);
    const bool tap = state.down && !state.used && time_ms >= state.pressed_at &&
                     time_ms - state.pressed_at <= max_hold_ms_;
    state = KeyState{};
    return tap;
}

} // namespace astelio
