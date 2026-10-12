#include "astelio/mac/mac_session.h"

#include "astelio/romaji_table.h"

#include <algorithm>

namespace astelio::mac {

MacSession::MacSession(Engine& engine) : engine_(engine), session_(RomajiTable::Default(), CharacterSettings{})
{
    if (const SystemDictionary* dictionary = engine_.dictionary()) {
        converter_.emplace(*dictionary);
        session_.SetConverter(&*converter_);
        session_.SetEmojiCatalog([&engine = engine_] { return engine.EmojiCatalogForDictionary(); });
    }
    session_.SetRecentEmoji(engine_.LoadRecentEmoji());
    // Many candidates at a glance: the list opens with the first Space, and the predictions fill 3 columns too.
    session_.SetListOnFirstConversion(true);
    session_.SetPredictionLimit(InputSession::kCandidateGridSize);
    ApplySettings(settings_);
}

void MacSession::ApplySettings(const Settings& settings)
{
    settings_ = settings;
    CharacterSettings characters;
    characters.list_number_period = settings.list_number_period;
    characters.auto_close_brackets = settings.auto_close_brackets;
    session_.SetCharacterSettings(characters);
    session_.SetTypoSuggestions(settings.typo_suggestions);
    if (converter_) {
        converter_->SetDateFormat(settings.date_format);
    }
    engine_.RefreshUserDictionary();
    if (converter_) {
        converter_->SetUserDictionary(engine_.user_dictionary());
    }
    session_.SetUserDictionary(engine_.user_dictionary());
    if (settings.learning_enabled) {
        engine_.RefreshLearning();
        session_.SetLearning(&engine_.learning());
    } else {
        session_.SetLearning(nullptr);
    }
    UpdateRecording();
}

void MacSession::SetRecordingAllowed(bool allowed)
{
    recording_allowed_ = allowed;
    UpdateRecording();
}

void MacSession::UpdateRecording()
{
    session_.SetRecording(recording_allowed_ && !settings_.learning_paused);
}

SessionOutput MacSession::SetJapaneseMode(bool japanese)
{
    return Finish(session_.SetJapaneseMode(japanese));
}

KeyResult MacSession::KeyDown(std::uint16_t key_code, const Modifiers& modifiers)
{
    MarkChordUsed();
    KeyResult result;
    const std::optional<KeyEvent> key = TranslateKey(key_code, modifiers);
    if (!key || !session_.WillHandle(*key)) {
        // Arrows, Home/End, clicks and shortcuts may move the caret.
        if (!session_.Composing()) {
            session_.ResetContext();
        }
        return result;
    }
    if (!session_.Composing()) {
        ApplySettings(settings_); // another app's menu or the files may have changed them
    }
    result.handled = true;
    result.output = Finish(session_.Handle(*key));
    return result;
}

std::optional<ModifierSide> MacSession::TapSide(std::uint16_t key_code) const
{
    if (settings_.tap_modifier == TapModifier::Command) {
        if (key_code == kLeftCommandKeyCode) {
            return ModifierSide::Left;
        }
        if (key_code == kRightCommandKeyCode) {
            return ModifierSide::Right;
        }
    } else {
        if (key_code == kLeftOptionKeyCode) {
            return ModifierSide::Left;
        }
        if (key_code == kRightOptionKeyCode) {
            return ModifierSide::Right;
        }
    }
    return std::nullopt;
}

std::optional<bool> MacSession::ModifierChanged(std::uint16_t key_code, bool down, std::uint64_t time_ms)
{
    if (key_code == kLeftShiftKeyCode || key_code == kRightShiftKeyCode) {
        const ModifierSide shift = key_code == kLeftShiftKeyCode ? ModifierSide::Left : ModifierSide::Right;
        if (down) {
            taps_.MarkChordUsed(); // Command+Shift is not a Command tap
            shift_taps_.Press(shift, time_ms);
        } else if (shift_taps_.Release(shift, time_ms)) {
            session_.ExitTemporaryAlphanumeric();
        }
        return std::nullopt;
    }
    if (down) {
        shift_taps_.MarkChordUsed();
    }
    const std::optional<ModifierSide> side = TapSide(key_code);
    if (!side) {
        // Shift, Control, Fn...: a chord such as Command+Shift is not a tap.
        if (down) {
            taps_.MarkChordUsed();
        }
        return std::nullopt;
    }
    if (down) {
        taps_.Press(*side, time_ms);
        return std::nullopt;
    }
    if (!taps_.Release(*side, time_ms)) {
        return std::nullopt;
    }
    // R-03: left switches to English, right to Japanese.
    return *side == ModifierSide::Right;
}

void MacSession::Abandon()
{
    session_.AbandonComposition();
    session_.ResetContext();
}

SessionOutput MacSession::Finish(SessionOutput output)
{
    if (output.learning_changed && settings_.learning_enabled) {
        engine_.SaveLearning();
    }
    if (output.recent_emoji_changed && !session_.RecentEmoji().empty()) {
        engine_.SaveRecentEmoji(session_.RecentEmoji());
    }
    return output;
}

Composition MacSession::CurrentComposition() const
{
    Composition composition;
    composition.text = session_.CompositionText();
    composition.cursor = std::min(session_.CompositionCursor(), composition.text.size());
    if (composition.text.empty()) {
        return composition;
    }
    if (!session_.Converting()) {
        composition.spans.push_back({0, composition.text.size(), SpanStyle::Input});
        return composition;
    }
    const std::vector<ConvertedSegment>& segments = session_.Segments();
    std::size_t offset = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const std::size_t length = segments[i].candidates[session_.SelectedCandidate(i)].size();
        const bool focused = i == session_.FocusedSegment();
        if (focused) {
            composition.anchor = offset;
        }
        composition.spans.push_back({offset, length, focused ? SpanStyle::Focused : SpanStyle::Converted});
        offset += length;
    }
    return composition;
}

CandidateView MacSession::Candidates() const
{
    CandidateView view;
    if (session_.EmojiPaletteActive() || session_.EmojiPaletteOffered()) {
        const EmojiPaletteView palette = session_.EmojiPalette();
        constexpr std::size_t page = InputSession::kEmojiColumns * InputSession::kEmojiRows;
        const std::size_t selected = palette.selected == kNoEmojiSelection ? 0 : palette.selected;
        view.kind = CandidateKind::Emoji;
        view.columns = InputSession::kEmojiColumns;
        view.total = palette.items.size();
        view.first_index = selected / page * page;
        const std::size_t end = std::min(view.first_index + page, palette.items.size());
        for (std::size_t i = view.first_index; i < end; ++i) {
            view.items.push_back(palette.items[i]);
            view.marks.push_back(CandidateMark::None);
        }
        if (palette.selected != kNoEmojiSelection) {
            view.selected = palette.selected - view.first_index;
        }
        view.title = palette.query;
        view.category = static_cast<std::size_t>(palette.category);
        view.active = palette.active;
        return view;
    }
    if (session_.Converting() && session_.CandidateListVisible()) {
        const std::size_t focus = session_.FocusedSegment();
        const std::vector<std::u16string>& candidates = session_.Segments()[focus].candidates;
        const std::size_t selected = session_.SelectedCandidate(focus);
        view.kind = CandidateKind::Candidates;
        view.total = candidates.size();
        // B-03: 27 at a time, in columns of 9; the digits pick from the column of the selection.
        view.first_index = selected / InputSession::kCandidateGridSize * InputSession::kCandidateGridSize;
        const std::size_t end = std::min(view.first_index + InputSession::kCandidateGridSize, candidates.size());
        view.columns = (end - view.first_index + InputSession::kCandidatePageSize - 1) / InputSession::kCandidatePageSize;
        for (std::size_t i = view.first_index; i < end; ++i) {
            view.items.push_back(candidates[i]);
            view.marks.push_back(session_.IsTypoCandidate(focus, i)      ? CandidateMark::Typo
                                 : session_.IsLearnedCandidate(focus, i) ? CandidateMark::Learned
                                                                         : CandidateMark::None);
        }
        if (session_.RelatedEmojiSelection() == kNoEmojiSelection) {
            view.selected = selected - view.first_index;
        }
        view.related = session_.RelatedEmoji();
        view.related_selected = session_.RelatedEmojiSelection();
        return view;
    }
    if (!session_.Converting() && !session_.Predictions().empty()) {
        const std::vector<std::u16string>& predictions = session_.Predictions();
        view.kind = CandidateKind::Predictions;
        view.total = predictions.size();
        const std::size_t end = std::min(InputSession::kCandidateGridSize, predictions.size());
        view.columns = (end + InputSession::kCandidatePageSize - 1) / InputSession::kCandidatePageSize;
        for (std::size_t i = 0; i < end; ++i) {
            view.items.push_back(predictions[i]);
            view.marks.push_back(session_.IsTypoPrediction(i) ? CandidateMark::Typo : CandidateMark::None);
        }
    }
    return view;
}

SessionOutput MacSession::Pick(std::size_t index)
{
    const CandidateView view = Candidates();
    if (index >= view.items.size()) {
        return {};
    }
    switch (view.kind) {
    case CandidateKind::Emoji: return Finish(session_.PickEmoji(view.first_index + index));
    case CandidateKind::Candidates:
    case CandidateKind::Predictions: {
        std::size_t first_index = view.first_index;
        if (view.kind == CandidateKind::Predictions) {
            // Tab turns the predictions into the candidate list (the first one selected), in the same order.
            Finish(session_.Handle(KeyEvent{KeyKind::Tab, 0}));
            if (!session_.Converting()) {
                return {};
            }
            first_index = 0;
        }
        // The digits pick from the focused column: move there with Left / Right first.
        const std::size_t focus = session_.FocusedSegment();
        const std::size_t target = (first_index + index) / InputSession::kCandidatePageSize;
        for (std::size_t step = 0; step < InputSession::kCandidateColumns; ++step) {
            const std::size_t column = session_.SelectedCandidate(focus) / InputSession::kCandidatePageSize;
            if (column == target) {
                break;
            }
            Finish(session_.Handle(KeyEvent{column < target ? KeyKind::Right : KeyKind::Left, 0}));
        }
        const std::size_t digit = index % InputSession::kCandidatePageSize;
        KeyEvent key{KeyKind::Character, static_cast<char16_t>(u'1' + digit)};
        SessionOutput output = Finish(session_.Handle(key));
        output.composition_changed = true;
        return output;
    }
    case CandidateKind::Hidden: break;
    }
    return {};
}

} // namespace astelio::mac
