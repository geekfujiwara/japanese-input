#pragma once

#include "astelio/character_rules.h"
#include "astelio/composer.h"
#include "astelio/converter.h"
#include "astelio/romaji_table.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    Up,
    Down,
    PageUp,
    PageDown,
};

struct KeyEvent {
    KeyKind kind = KeyKind::Character;
    char16_t character = 0;
    bool shift = false; // for the arrow keys (Shift+Left/Right resizes a segment)
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

    // Enables kana-kanji conversion with Space. `converter` must outlive the session; nullptr disables it.
    void SetConverter(const Converter* converter) { converter_ = converter; }

    bool JapaneseMode() const { return japanese_mode_; }
    // Leaving Japanese mode commits the uncommitted text.
    SessionOutput SetJapaneseMode(bool enabled);

    // Whether Handle() would consume the key (the app must not see it).
    bool WillHandle(const KeyEvent& key) const;
    SessionOutput Handle(const KeyEvent& key);

    void ExitTemporaryAlphanumeric() { composer_.ExitTemporaryAlphanumeric(); }
    // The app ended the composition on its own (focus change, mouse click).
    void AbandonComposition();

    bool Composing() const { return converting_ || !composer_.Empty(); }
    // The uncommitted text as shown: the kana being typed, or the selected candidates while converting.
    std::u16string CompositionText() const;
    std::size_t CompositionCursor() const;

    bool Converting() const { return converting_; }
    const std::vector<ConvertedSegment>& Segments() const { return segments_; }
    std::size_t FocusedSegment() const { return focus_; }
    std::size_t SelectedCandidate(std::size_t segment) const { return selected_.at(segment); }
    // A second Space (or an arrow / page key) while converting opens the candidate list of the focused segment.
    bool CandidateListVisible() const { return candidate_list_visible_; }
    static constexpr std::size_t kCandidatePageSize = 9;

private:
    SessionOutput HandleConversion(const KeyEvent& key);
    bool HandleCandidateList(const KeyEvent& key);
    void Convert(std::vector<std::size_t> fixed_lengths);
    std::u16string ConvertedText() const;
    void EndConversion();

    Composer composer_;
    CharacterSettings settings_;
    const Converter* converter_ = nullptr;
    bool japanese_mode_ = true;
    bool converting_ = false;
    std::u16string reading_;
    std::vector<ConvertedSegment> segments_;
    std::vector<std::size_t> selected_;
    std::size_t focus_ = 0;
    bool candidate_list_visible_ = false;
};

} // namespace astelio
