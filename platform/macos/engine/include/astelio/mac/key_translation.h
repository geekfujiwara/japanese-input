#pragma once

#include "astelio/input_session.h"

#include <cstdint>
#include <optional>

namespace astelio::mac {

struct Modifiers {
    bool shift = false;
    bool control = false;
    bool option = false;
    bool command = false;
    bool caps_lock = false;
    bool function = false; // Fn: set by macOS for the arrow and page keys too, so it is not a shortcut by itself
};

// Maps a key-down (NSEvent keyCode) to an IME key using the US layout by key code, so the physical English keyboard
// is read the same way whatever keyboard layout macOS reports (ABC, U.S., Dvorak...).
// Returns std::nullopt for keys the IME never handles (Command and Option shortcuts, function keys, modifiers).
std::optional<KeyEvent> TranslateKey(std::uint16_t key_code, const Modifiers& modifiers);

// Key codes of the modifiers whose single press switches the mode (flagsChanged events).
inline constexpr std::uint16_t kLeftCommandKeyCode = 0x37;
inline constexpr std::uint16_t kRightCommandKeyCode = 0x36;
inline constexpr std::uint16_t kLeftOptionKeyCode = 0x3A;
inline constexpr std::uint16_t kRightOptionKeyCode = 0x3D;
// Shift: a single press ends the English text started with Shift+letter.
inline constexpr std::uint16_t kLeftShiftKeyCode = 0x38;
inline constexpr std::uint16_t kRightShiftKeyCode = 0x3C;

} // namespace astelio::mac
