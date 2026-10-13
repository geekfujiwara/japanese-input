#pragma once

#include "astelio/input_session.h"
#include "astelio/mac/engine.h"
#include "astelio/mac/key_translation.h"
#include "astelio/modifier_tap_tracker.h"
#include "astelio/special_candidates.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace astelio::mac {

// C-06: the modifiers whose single press switches the mode (left: English, right: Japanese).
enum class TapModifier : std::uint8_t {
    Command,
    Option,
};

struct Settings {
    TapModifier tap_modifier = TapModifier::Command;
    bool learning_enabled = true;  // D-04: use the history
    bool learning_paused = false;  // secret mode: use it, record nothing
    bool typo_suggestions = true;  // B-14
    bool list_number_period = true; // R-10
    bool auto_close_brackets = true; // R-11
    DateFormat date_format = DateFormat::SlashPadded; // C-13
};

// How a run of the uncommitted text is underlined.
enum class SpanStyle : std::uint8_t {
    Input,     // kana being typed: dotted
    Converted, // a converted segment: thin line
    Focused,   // the segment the candidates are for: thick line
};

struct Span {
    std::size_t start = 0; // UTF-16 units
    std::size_t length = 0;
    SpanStyle style = SpanStyle::Input;
};

struct Composition {
    std::u16string text;
    std::size_t cursor = 0;
    std::vector<Span> spans;
    // Where the candidate window points: the focused segment.
    std::size_t anchor = 0;
};

enum class CandidateMark : std::uint8_t {
    None,
    Learned, // chosen before
    Typo,    // もしかして
};

enum class CandidateKind : std::uint8_t {
    Hidden,
    Predictions, // while typing, in columns of 9 like the candidates; Tab or Down selects
    Candidates,  // B-03: the focused segment's candidates, columns of 9 and up to 3 columns at a time
    Emoji,       // B-13: the palette (offered when the kana is えもじ, opened with Tab or Down)
};

struct CandidateView {
    CandidateKind kind = CandidateKind::Hidden;
    std::vector<std::u16string> items;
    std::vector<CandidateMark> marks;
    std::size_t selected = kNoEmojiSelection; // index into items
    std::size_t first_index = 0;              // index of items[0] in the full list
    std::size_t total = 0;                    // size of the full list
    // Candidates: the columns shown (of 9 rows, numbered 1-9 each); Emoji: items per row.
    std::size_t columns = 1;
    // Emoji: the EmojiCategory shown, and whether the palette is open (false: only offered).
    std::size_t category = 0;
    bool active = false;
    std::u16string title;                     // Emoji: the category or the search text
    // B-03: emoji related to the focused segment, shown beside the candidates.
    std::vector<std::u16string> related;
    std::size_t related_selected = kNoEmojiSelection;
};

struct KeyResult {
    bool handled = false;
    SessionOutput output;
};

// One text field's input: an InputSession with this input method's settings, the shared engine, and the
// left/right modifier taps. Platform-independent so it is tested on every OS; the Objective-C++ bridge only
// converts types.
class MacSession {
public:
    // `engine` must outlive the session.
    explicit MacSession(Engine& engine);

    void ApplySettings(const Settings& settings);
    const Settings& settings() const { return settings_; }

    // Secure Input (password fields) or an app the user excluded: nothing is recorded.
    void SetRecordingAllowed(bool allowed);

    bool JapaneseMode() const { return session_.JapaneseMode(); }
    SessionOutput SetJapaneseMode(bool japanese);

    // A key went down. Not handled: the app gets the key.
    KeyResult KeyDown(std::uint16_t key_code, const Modifiers& modifiers);
    // A modifier went down or up (flagsChanged). Returns the mode to switch to when this completes a tap.
    std::optional<bool> ModifierChanged(std::uint16_t key_code, bool down, std::uint64_t time_ms);
    // A mouse click or anything else between the press and the release of a modifier.
    void MarkChordUsed()
    {
        taps_.MarkChordUsed();
        shift_taps_.MarkChordUsed();
    }

    // The app ended the composition (focus change, click).
    void Abandon();
    // The caret may have moved: the next word does not follow the last one.
    void ResetContext() { session_.ResetContext(); }
    // A single Shift press seen outside the input method (the switcher): leave the English text started with Shift.
    void ExitTemporaryEnglish() { session_.ExitTemporaryAlphanumeric(); }

    bool Composing() const { return session_.Composing(); }
    Composition CurrentComposition() const;
    CandidateView Candidates() const;

    // Mouse: picks items[index] of Candidates() (a prediction, a candidate or an emoji).
    SessionOutput Pick(std::size_t index);
    SessionOutput PickRelated(std::size_t index) { return Finish(session_.PickRelatedEmoji(index)); }
    // Mouse: a tab of the emoji palette (opens it when it is only offered).
    bool SelectCategory(EmojiCategory category) { return session_.SelectEmojiCategory(category); }

    InputSession& session() { return session_; }

private:
    SessionOutput Finish(SessionOutput output);
    void UpdateRecording();
    std::optional<ModifierSide> TapSide(std::uint16_t key_code) const;

    Engine& engine_;
    Settings settings_;
    std::optional<Converter> converter_;
    InputSession session_;
    ModifierTapTracker taps_;
    ModifierTapTracker shift_taps_; // a single Shift press ends temporary English
    bool recording_allowed_ = true;
};

} // namespace astelio::mac
