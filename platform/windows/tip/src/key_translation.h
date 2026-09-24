#pragma once

#include "astelio/input_session.h"

#include <cstdint>
#include <optional>

namespace astelio::tip {

struct Modifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool windows = false;
    bool caps_lock = false;
};

// Maps a key-down (virtual key + WM_KEYDOWN lParam) to an IME key using the US layout by scan code,
// so the physical English keyboard is interpreted the same way whatever layout Windows reports.
// Returns std::nullopt for keys the IME never handles (shortcuts, function keys, modifiers).
std::optional<KeyEvent> TranslateKey(std::uint32_t virtual_key, std::uint32_t lparam, const Modifiers& modifiers);

} // namespace astelio::tip
