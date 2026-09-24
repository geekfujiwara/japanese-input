#pragma once

#include <cstdint>

namespace astelio {

enum class ModifierSide : std::uint8_t {
    Left,
    Right,
};

// Detects a single press of a left/right modifier (Alt on Windows, Command on macOS)
// with no other key in between and not held longer than the limit.
class ModifierTapTracker {
public:
    explicit ModifierTapTracker(std::uint32_t max_hold_ms = 1000) : max_hold_ms_(max_hold_ms) {}

    void Press(ModifierSide side, std::uint64_t time_ms);
    // Call when any other key goes down while a modifier is held.
    void MarkChordUsed();
    // Returns true when this release completes a tap.
    bool Release(ModifierSide side, std::uint64_t time_ms);

private:
    struct KeyState {
        bool down = false;
        bool used = false;
        std::uint64_t pressed_at = 0;
    };

    KeyState& State(ModifierSide side) { return side == ModifierSide::Left ? left_ : right_; }
    KeyState& Other(ModifierSide side) { return side == ModifierSide::Left ? right_ : left_; }

    std::uint32_t max_hold_ms_;
    KeyState left_;
    KeyState right_;
};

} // namespace astelio
