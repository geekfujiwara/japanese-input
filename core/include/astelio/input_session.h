#pragma once

#include "astelio/character_rules.h"
#include "astelio/composer.h"
#include "astelio/converter.h"
#include "astelio/emoji.h"
#include "astelio/learning.h"
#include "astelio/romaji_table.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
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
    Tab,
    F6,  // hiragana
    F7,  // full-width katakana
    F8,  // half-width katakana
    F9,  // full-width alphanumerics
    F10, // half-width alphanumerics
};

struct KeyEvent {
    KeyKind kind = KeyKind::Character;
    char16_t character = 0;
    bool shift = false; // for the arrow keys (Shift+Left/Right resizes a segment)
    // Ctrl+Delete forgets the selected candidate, Ctrl+Down commits up to the focused segment (B-06),
    // Ctrl+Backspace right after a commit brings the conversion back (B-08).
    bool control = false;
};

struct SessionOutput {
    std::u16string commit;
    bool composition_changed = false;
    bool recent_emoji_changed = false; // save RecentEmoji()
    bool learning_changed = false;     // save the LearningHistory
    // B-08: remove this text just before the caret (the commit being undone) before showing the composition.
    std::u16string undo_commit;
};

// B-13: what the emoji palette shows.
struct EmojiPaletteView {
    EmojiCategory category = EmojiCategory::Smileys;
    std::u16string query;               // search text typed inside the palette
    std::vector<std::u16string> items;  // search results, or the emoji of the category
    std::size_t selected = 0;           // index into items; kNoEmojiSelection while only offered
    bool active = false;                // false: offered (the kana is えもじ), Tab or Down opens it
};
inline constexpr std::size_t kNoEmojiSelection = static_cast<std::size_t>(-1);

// Platform-independent key handling for one input context.
class InputSession {
public:
    // `table` must outlive the session.
    InputSession(const RomajiTable& table, CharacterSettings settings);

    // Enables kana-kanji conversion with Space. `converter` must outlive the session; nullptr disables it.
    void SetConverter(const Converter* converter) { converter_ = converter; }

    // D-04: the words chosen before come first. `history` must outlive the session; nullptr turns the history off.
    // With recording off (password fields, secret mode) the history is used but nothing new is recorded.
    void SetLearning(LearningHistory* history) { learning_ = history; }
    void SetRecording(bool enabled) { recording_ = enabled; }
    // Whether candidate `index` of `segment` is shown because it was chosen before (marked in the window).
    bool IsLearnedCandidate(std::size_t segment, std::size_t index) const;

    // B-14: a word the user may have meant when a key slipped is added as the second candidate of a segment
    // (shown as もしかして). On by default.
    void SetTypoSuggestions(bool enabled) { typo_suggestions_ = enabled; }
    bool IsTypoCandidate(std::size_t segment, std::size_t index) const;
    // Longer words are not searched for slips (it takes about 0.1 ms per key).
    static constexpr std::size_t kMaxTypoKeys = 24;

    // T-B02-5: the last word committed is the context of the next conversion. The platform layer calls this
    // when the caret may have moved (keys the IME does not handle, focus changes).
    void ResetContext()
    {
        context_right_id_.reset();
        previous_surface_.clear();
        last_commit_.reset();
    }

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

    // B-04: candidates predicted from the kana typed so far (shown while typing; Tab or Down selects them).
    const std::vector<std::u16string>& Predictions() const { return predictions_; }
    static constexpr std::size_t kMinPredictionLength = 2;

    // B-13: the emoji palette, offered when the kana is えもじ. The catalog is built on first use.
    void SetEmojiCatalog(std::function<const EmojiCatalog*()> provider) { emoji_provider_ = std::move(provider); }
    void SetRecentEmoji(std::vector<std::u16string> recent);
    const std::vector<std::u16string>& RecentEmoji() const { return recent_emoji_; }
    bool EmojiPaletteOffered() const;
    bool EmojiPaletteActive() const { return emoji_active_; }
    EmojiPaletteView EmojiPalette() const;
    // Mouse: commit items[index] / show a category (opens the palette when it is offered).
    SessionOutput PickEmoji(std::size_t index);
    bool SelectEmojiCategory(EmojiCategory category);
    static constexpr std::size_t kEmojiColumns = 8;
    static constexpr std::size_t kEmojiRows = 5;
    static constexpr std::size_t kMaxRecentEmoji = 32;
    static constexpr std::size_t kMaxEmojiSearchResults = 200;

private:
    SessionOutput HandleEmojiPalette(const KeyEvent& key);
    void OpenEmojiPalette();
    void CloseEmojiPalette();
    void RefreshEmojiItems();
    SessionOutput CommitEmoji(std::size_t index);
    const EmojiCatalog* Catalog() const;
    SessionOutput HandleComposition(const KeyEvent& key);
    SessionOutput HandleConversion(const KeyEvent& key);
    bool HandleCandidateList(const KeyEvent& key);
    void StartPrediction();
    void UpdatePredictions();
    void ConvertToForm(KeyKind key);
    void Convert(std::vector<std::size_t> fixed_lengths);
    std::u16string ConvertedText() const;
    // Records the choices, then returns the text to commit and ends the conversion.
    std::u16string CommitConversion(SessionOutput& output);
    // Records the choices of segments [0, end) and makes the last of them the context of what follows.
    void RecordChoices(SessionOutput& output, std::size_t end);
    SessionOutput CommitUpToFocus();
    SessionOutput UndoCommit();
    void ApplyLearning(std::size_t segment);
    // The word before segment `segment`: the previous segment as chosen, or the last word committed.
    std::u16string SegmentContext(std::size_t segment) const;
    void AddTypoSuggestions();
    bool ForgetSelectedCandidate();
    void EndConversion();

    Composer composer_;
    CharacterSettings settings_;
    const RomajiTable* table_;
    const Converter* converter_ = nullptr;
    LearningHistory* learning_ = nullptr;
    bool recording_ = true;
    bool japanese_mode_ = true;
    bool converting_ = false;
    bool predicting_ = false; // the conversion is the list of predictions (Tab or Down while typing)
    std::u16string reading_;
    std::vector<ConvertedSegment> segments_;
    // The candidates of each segment before the history reordered them.
    std::vector<std::vector<std::u16string>> base_candidates_;
    bool typo_suggestions_ = true;
    std::vector<std::u16string> typo_surfaces_; // per segment; empty when there is no suggestion
    std::optional<std::uint16_t> context_right_id_;
    std::u16string previous_surface_; // the last segment committed, for the pairs of words (D-04)
    bool segments_resized_ = false;   // Shift+Left/Right changed the segments
    bool segments_learned_ = false;   // the segments came from the history
    struct CommittedConversion {
        std::u16string text;
        std::u16string reading;
        std::vector<std::size_t> lengths;
        std::vector<std::u16string> chosen;
        std::size_t focus; // no default initializer: clang then rejects std::optional of it in this class
        std::optional<std::uint16_t> context_right_id;
        std::u16string previous_surface;
    };
    std::optional<CommittedConversion> last_commit_; // until the next key or caret move
    std::vector<std::size_t> selected_;
    std::size_t focus_ = 0;
    bool candidate_list_visible_ = false;
    std::vector<std::u16string> predictions_;
    // Keys typed for the current composition, for F9/F10; cleared when the text is edited in the middle.
    std::u16string typed_keys_;
    bool typed_keys_valid_ = true;
    std::function<const EmojiCatalog*()> emoji_provider_;
    std::vector<std::u16string> recent_emoji_;
    bool emoji_active_ = false;
    EmojiCategory emoji_category_ = EmojiCategory::Smileys;
    Composer emoji_search_;
    std::vector<std::u16string> emoji_items_;
    std::size_t emoji_selected_ = 0;
};

} // namespace astelio
