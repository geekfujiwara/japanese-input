#pragma once

#include "astelio/character_rules.h"
#include "astelio/composer.h"
#include "astelio/romaji_table.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace astelio {

enum class KeyKind : std::uint8_t {
    Character, // printable US-keyboard character, Shift already applied
    Space,
    Enter,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
};

struct KeyEvent {
    KeyKind kind = KeyKind::Character;
    char16_t character = 0;
};

struct SessionOutput {
    std::u16string commit;
    bool composition_changed = false;
};

// Platform-independent key handling for one input context.
class InputSession {
public:
    // `table` must outlive the session.
    InputSession(const RomajiTable& table, CharacterSettings settings);

    bool JapaneseMode() const { return japanese_mode_; }
    // Leaving Japanese mode commits the uncommitted text.
    SessionOutput SetJapaneseMode(bool enabled);

    // Whether Handle() would consume the key (the app must not see it).
    bool WillHandle(const KeyEvent& key) const;
    SessionOutput Handle(const KeyEvent& key);

    void ExitTemporaryAlphanumeric() { composer_.ExitTemporaryAlphanumeric(); }
    // The app ended the composition on its own (focus change, mouse click).
    void AbandonComposition() { composer_.Clear(); }

    bool Composing() const { return !composer_.Empty(); }
    std::u16string CompositionText() const { return composer_.Text(); }
    std::size_t CompositionCursor() const { return composer_.Cursor(); }

private:
    Composer composer_;
    CharacterSettings settings_;
    bool japanese_mode_ = true;
};

} // namespace astelio
