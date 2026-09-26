#include "astelio/input_session.h"

#include "astelio/kana_forms.h"
#include "astelio/typo_candidates.h"

#include <algorithm>
#include <utility>

namespace astelio {

InputSession::InputSession(const RomajiTable& table, CharacterSettings settings)
    : composer_(table, settings), settings_(settings), table_(&table), emoji_search_(table, settings)
{
}

SessionOutput InputSession::SetJapaneseMode(bool enabled)
{
    CloseEmojiPalette();
    SessionOutput output;
    if (!enabled && converting_) {
        output.commit = CommitConversion(output);
        output.composition_changed = true;
    } else if (!enabled && Composing()) {
        output.commit = composer_.Commit();
        output.composition_changed = true;
    }
    japanese_mode_ = enabled;
    predictions_.clear();
    ResetContext();
    return output;
}

void InputSession::AbandonComposition()
{
    CloseEmojiPalette();
    composer_.Clear();
    EndConversion();
    predictions_.clear();
    ResetContext();
}

std::u16string InputSession::CompositionText() const
{
    return converting_ ? ConvertedText() : composer_.Text();
}

std::size_t InputSession::CompositionCursor() const
{
    return converting_ ? ConvertedText().size() : composer_.Cursor();
}

bool InputSession::WillHandle(const KeyEvent& key) const
{
    if (!japanese_mode_) {
        return false;
    }
    switch (key.kind) {
    case KeyKind::Character:
    case KeyKind::Space:
        return true;
    case KeyKind::Enter:
    case KeyKind::Escape:
    case KeyKind::Backspace:
    case KeyKind::Delete:
    case KeyKind::Left:
    case KeyKind::Right:
    case KeyKind::Up:
    case KeyKind::Down:
    case KeyKind::PageUp:
    case KeyKind::PageDown:
    case KeyKind::Tab:
    case KeyKind::F6:
    case KeyKind::F7:
    case KeyKind::F8:
    case KeyKind::F9:
    case KeyKind::F10:
        return Composing();
    }
    return false;
}

SessionOutput InputSession::Handle(const KeyEvent& key)
{
    if (!WillHandle(key)) {
        return {};
    }
    if (emoji_active_) {
        SessionOutput output = HandleEmojiPalette(key);
        if (!Composing()) {
            typed_keys_.clear();
            typed_keys_valid_ = true;
        }
        UpdatePredictions();
        return output;
    }
    const bool was_converting = converting_;
    SessionOutput output = converting_ ? HandleConversion(key) : HandleComposition(key);
    if (key.kind == KeyKind::Character && !converting_) {
        if (was_converting) {
            typed_keys_.clear(); // the conversion was committed and a new composition started
            typed_keys_valid_ = true;
        }
        typed_keys_.push_back(key.character);
    } else if (!was_converting && (key.kind == KeyKind::Backspace || key.kind == KeyKind::Delete ||
                                   key.kind == KeyKind::Left || key.kind == KeyKind::Right)) {
        typed_keys_valid_ = false;
    }
    if (!Composing()) {
        typed_keys_.clear();
        typed_keys_valid_ = true;
    }
    UpdatePredictions();
    return output;
}

// B-05: F6-F10 turn the focused segment (or the whole kana) into one fixed form.
void InputSession::ConvertToForm(KeyKind key)
{
    if (!converting_) {
        reading_ = composer_.Commit();
        ConvertedSegment segment;
        segment.reading = reading_;
        segments_ = {std::move(segment)};
        selected_ = {0};
        focus_ = 0;
        converting_ = true;
    }
    ConvertedSegment& segment = segments_[focus_];
    std::u16string form;
    if (key == KeyKind::F6) {
        form = segment.reading;
    } else if (key == KeyKind::F7) {
        form = HiraganaToKatakana(segment.reading);
    } else if (key == KeyKind::F8) {
        form = ToHalfWidthKatakana(segment.reading);
    } else {
        const bool typed = segments_.size() == 1 && typed_keys_valid_ && !typed_keys_.empty();
        const std::u16string romaji = typed ? typed_keys_ : KanaToRomaji(segment.reading, *table_);
        form = key == KeyKind::F9 ? ToFullWidthAscii(romaji) : romaji;
    }
    const auto found = std::find(segment.candidates.begin(), segment.candidates.end(), form);
    selected_[focus_] = static_cast<std::size_t>(found - segment.candidates.begin());
    if (found == segment.candidates.end()) {
        segment.candidates.push_back(std::move(form));
    }
    candidate_list_visible_ = false;
}

void InputSession::UpdatePredictions()
{
    predictions_.clear();
    if (converting_ || emoji_active_ || converter_ == nullptr || composer_.Empty()) {
        return;
    }
    // Romaji still being typed ("arig") is not part of the reading yet.
    std::u16string reading = composer_.Text();
    while (!reading.empty() && reading.back() >= u'a' && reading.back() <= u'z') {
        reading.pop_back();
    }
    if (reading.size() >= kMinPredictionLength) {
        predictions_ = converter_->Predict(reading, kCandidatePageSize);
        if (learning_ != nullptr) {
            // Words chosen before come first.
            std::vector<std::u16string> learned = learning_->Predictions(reading, kCandidatePageSize);
            for (std::u16string& prediction : predictions_) {
                if (learned.size() >= kCandidatePageSize) {
                    break;
                }
                if (std::find(learned.begin(), learned.end(), prediction) == learned.end()) {
                    learned.push_back(std::move(prediction));
                }
            }
            predictions_ = std::move(learned);
        }
    }
}

void InputSession::StartPrediction()
{
    ConvertedSegment segment;
    segment.candidates = predictions_;
    reading_ = composer_.Commit();
    segment.reading = reading_;
    for (std::u16string form : {reading_, HiraganaToKatakana(reading_)}) {
        if (std::find(segment.candidates.begin(), segment.candidates.end(), form) == segment.candidates.end()) {
            segment.candidates.push_back(std::move(form));
        }
    }
    base_candidates_ = {segment.candidates};
    segments_ = {std::move(segment)};
    selected_ = {0};
    focus_ = 0;
    converting_ = true;
    predicting_ = true;
    candidate_list_visible_ = true;
}

SessionOutput InputSession::HandleComposition(const KeyEvent& key)
{
    SessionOutput output;
    output.composition_changed = true;
    switch (key.kind) {
    case KeyKind::Character:
        composer_.InsertKey(key.character);
        break;
    case KeyKind::Space:
        if (!Composing()) {
            output.commit = SpaceCharacter(settings_);
            output.composition_changed = false;
        } else if (converter_ == nullptr) {
            output.composition_changed = false;
        } else {
            reading_ = composer_.Commit();
            Convert({});
        }
        break;
    case KeyKind::Enter:
        output.commit = composer_.Commit();
        ResetContext();
        break;
    case KeyKind::Escape:
        composer_.Clear();
        break;
    case KeyKind::Backspace:
        composer_.Backspace();
        break;
    case KeyKind::Delete:
        composer_.Delete();
        break;
    case KeyKind::Left:
        composer_.MoveLeft();
        break;
    case KeyKind::Right:
        composer_.MoveRight();
        break;
    case KeyKind::Up:
    case KeyKind::PageUp:
    case KeyKind::PageDown:
        output.composition_changed = false;
        break;
    case KeyKind::Down:
    case KeyKind::Tab:
        if (EmojiPaletteOffered()) {
            OpenEmojiPalette();
        } else if (predictions_.empty()) {
            output.composition_changed = false;
        } else {
            StartPrediction();
        }
        break;
    case KeyKind::F6:
    case KeyKind::F7:
    case KeyKind::F8:
    case KeyKind::F9:
    case KeyKind::F10:
        ConvertToForm(key.kind);
        break;
    }
    return output;
}

bool InputSession::HandleCandidateList(const KeyEvent& key)
{
    const std::size_t count = segments_[focus_].candidates.size();
    std::size_t& selected = selected_[focus_];
    switch (key.kind) {
    case KeyKind::Space:
    case KeyKind::Down:
        selected = (selected + 1) % count;
        break;
    case KeyKind::Up:
        selected = (selected + count - 1) % count;
        break;
    case KeyKind::Tab:
        selected = key.shift ? (selected + count - 1) % count : (selected + 1) % count;
        break;
    case KeyKind::PageDown:
        selected = std::min(selected + kCandidatePageSize, count - 1);
        break;
    case KeyKind::PageUp:
        selected = selected >= kCandidatePageSize ? selected - kCandidatePageSize : 0;
        break;
    case KeyKind::Character: {
        // 1-9 pick from the current page and close the list.
        if (!candidate_list_visible_ || key.character < u'1' || key.character > u'9') {
            return false;
        }
        const std::size_t index =
            selected / kCandidatePageSize * kCandidatePageSize + static_cast<std::size_t>(key.character - u'1');
        if (index < count) {
            selected = index;
        }
        candidate_list_visible_ = false;
        return true;
    }
    default:
        return false;
    }
    candidate_list_visible_ = true;
    return true;
}

SessionOutput InputSession::HandleConversion(const KeyEvent& key)
{
    SessionOutput output;
    output.composition_changed = true;
    // D-05: Ctrl+Delete removes the selected candidate from the history.
    if (key.kind == KeyKind::Delete && key.control && candidate_list_visible_) {
        output.learning_changed = ForgetSelectedCandidate();
        output.composition_changed = output.learning_changed;
        return output;
    }
    if (HandleCandidateList(key)) {
        return output;
    }
    candidate_list_visible_ = false;
    switch (key.kind) {
    case KeyKind::Space:
    case KeyKind::Down:
    case KeyKind::Up:
    case KeyKind::PageUp:
    case KeyKind::PageDown:
    case KeyKind::Tab:
        break;
    case KeyKind::Left:
    case KeyKind::Right: {
        const bool forward = key.kind == KeyKind::Right;
        if (!key.shift) {
            if (forward && focus_ + 1 < segments_.size()) {
                ++focus_;
            } else if (!forward && focus_ > 0) {
                --focus_;
            }
            break;
        }
        // Resize the focused segment and convert the rest again, keeping the choices before it.
        std::vector<std::size_t> fixed;
        std::size_t before = 0;
        for (std::size_t i = 0; i < focus_; ++i) {
            fixed.push_back(segments_[i].reading.size());
            before += fixed.back();
        }
        const std::size_t length = segments_[focus_].reading.size();
        const std::size_t resized = forward ? length + 1 : length - 1;
        if (resized == 0 || before + resized > reading_.size()) {
            output.composition_changed = false;
            break;
        }
        fixed.push_back(resized);
        std::vector<std::u16string> chosen;
        for (std::size_t i = 0; i < focus_; ++i) {
            chosen.push_back(segments_[i].candidates[selected_[i]]);
        }
        Convert(std::move(fixed));
        segments_resized_ = true;
        for (std::size_t i = 0; i < chosen.size() && i < segments_.size(); ++i) {
            const std::vector<std::u16string>& candidates = segments_[i].candidates;
            for (std::size_t c = 0; c < candidates.size(); ++c) {
                if (candidates[c] == chosen[i]) {
                    selected_[i] = c;
                    break;
                }
            }
        }
        break;
    }
    case KeyKind::Enter:
        output.commit = CommitConversion(output);
        break;
    case KeyKind::Escape:
    case KeyKind::Backspace:
        // Back to the kana before conversion.
        composer_.SetText(reading_);
        EndConversion();
        break;
    case KeyKind::Character:
        output.commit = CommitConversion(output);
        composer_.InsertKey(key.character);
        break;
    case KeyKind::Delete:
        output.composition_changed = false;
        break;
    case KeyKind::F6:
    case KeyKind::F7:
    case KeyKind::F8:
    case KeyKind::F9:
    case KeyKind::F10:
        ConvertToForm(key.kind);
        break;
    }
    return output;
}

void InputSession::Convert(std::vector<std::size_t> fixed_lengths)
{
    // T-D04-2: a reading split by hand before is split the same way.
    segments_learned_ = false;
    if (fixed_lengths.empty() && learning_ != nullptr) {
        if (std::optional<std::vector<std::size_t>> learned = learning_->Segmentation(reading_)) {
            fixed_lengths = std::move(*learned);
            segments_learned_ = true;
        }
    }
    segments_ = converter_->Convert(reading_, fixed_lengths, context_right_id_);
    // A slipped key often leaves text the dictionary splits into pieces ("ゆーあー"); when the whole input is a
    // word one slip away, keep it as one segment so the word can be offered for all of it.
    if (typo_suggestions_ && segments_.size() > 1 && fixed_lengths.empty() && typed_keys_valid_ &&
        !typed_keys_.empty() && typed_keys_.size() <= kMaxTypoKeys &&
        SuggestTypoCorrection(typed_keys_, *table_, converter_->dictionary(), context_right_id_)) {
        const std::size_t whole[] = {reading_.size()};
        segments_ = converter_->Convert(reading_, whole, context_right_id_);
    }
    selected_.assign(segments_.size(), 0);
    predicting_ = false;
    if (segments_.empty()) {
        composer_.SetText(reading_);
        EndConversion();
        return;
    }
    base_candidates_.clear();
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        base_candidates_.push_back(segments_[i].candidates);
    }
    AddTypoSuggestions();
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        ApplyLearning(i);
    }
    converting_ = true;
    if (focus_ >= segments_.size()) {
        focus_ = segments_.size() - 1;
    }
    // Show the list at once, so the もしかして word is seen without a second Space.
    if (fixed_lengths.empty() && !typo_surfaces_[focus_].empty()) {
        candidate_list_visible_ = true;
    }
}

// B-14: the もしかして word of each segment's head goes second, with the segment's particles after it.
void InputSession::AddTypoSuggestions()
{
    typo_surfaces_.assign(segments_.size(), std::u16string());
    if (!typo_suggestions_ || converter_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const ConvertedSegment& segment = segments_[i];
        const std::size_t head_length = segment.head_length == 0 ? segment.reading.size() : segment.head_length;
        const std::u16string head = segment.reading.substr(0, head_length);
        // The keys as typed show the slip best; after editing or for later segments, spell the kana.
        const bool typed = segments_.size() == 1 && head_length == segment.reading.size() && typed_keys_valid_ &&
                           !typed_keys_.empty();
        const std::u16string keys = typed ? typed_keys_ : KanaToRomaji(head, *table_);
        if (keys.size() > kMaxTypoKeys) {
            continue;
        }
        const std::optional<std::uint16_t> previous =
            i == 0 ? context_right_id_ : std::optional<std::uint16_t>(segments_[i - 1].right_id);
        const std::optional<TypoCandidate> suggestion =
            SuggestTypoCorrection(keys, *table_, converter_->dictionary(), previous);
        if (!suggestion) {
            continue;
        }
        std::u16string text = suggestion->surface + segment.tail;
        std::vector<std::u16string>& base = base_candidates_[i];
        if (std::find(base.begin(), base.end(), text) != base.end()) {
            continue; // the reading as typed converts to it anyway
        }
        base.insert(base.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(1, base.size())), text);
        typo_surfaces_[i] = std::move(text);
    }
}

bool InputSession::IsTypoCandidate(std::size_t segment, std::size_t index) const
{
    return segment < typo_surfaces_.size() && !typo_surfaces_[segment].empty() && segment < segments_.size() &&
           index < segments_[segment].candidates.size() &&
           segments_[segment].candidates[index] == typo_surfaces_[segment];
}

// The surfaces chosen before come first: after the same word before, then for the reading, most recent first.
void InputSession::ApplyLearning(std::size_t segment)
{
    if (segment >= base_candidates_.size()) {
        return;
    }
    std::vector<std::u16string> candidates;
    if (learning_ != nullptr && !predicting_) {
        candidates = learning_->Pairs(SegmentContext(segment), segments_[segment].reading);
        for (std::u16string& surface : learning_->Conversions(segments_[segment].reading)) {
            if (std::find(candidates.begin(), candidates.end(), surface) == candidates.end()) {
                candidates.push_back(std::move(surface));
            }
        }
    }
    for (const std::u16string& candidate : base_candidates_[segment]) {
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
            candidates.push_back(candidate);
        }
    }
    segments_[segment].candidates = std::move(candidates);
}

bool InputSession::IsLearnedCandidate(std::size_t segment, std::size_t index) const
{
    if (learning_ == nullptr || segment >= segments_.size() || index >= segments_[segment].candidates.size()) {
        return false;
    }
    const std::u16string& surface = segments_[segment].candidates[index];
    if (predicting_) {
        return learning_->Contains(LearningHistory::Kind::Prediction, segments_[segment].reading, surface);
    }
    return learning_->Contains(LearningHistory::Kind::Conversion, segments_[segment].reading, surface) ||
           learning_->Contains(LearningHistory::Kind::Pair, segments_[segment].reading, surface,
                               SegmentContext(segment));
}

std::u16string InputSession::SegmentContext(std::size_t segment) const
{
    if (segment == 0) {
        return previous_surface_;
    }
    return segment - 1 < selected_.size() ? segments_[segment - 1].candidates[selected_[segment - 1]]
                                          : std::u16string();
}

bool InputSession::ForgetSelectedCandidate()
{
    if (learning_ == nullptr) {
        return false;
    }
    ConvertedSegment& segment = segments_[focus_];
    const std::u16string surface = segment.candidates[selected_[focus_]];
    bool removed = false;
    if (predicting_) {
        // The history may hold the word for a longer reading; forget it for every reading.
        removed = learning_->RemoveSurface(LearningHistory::Kind::Prediction, surface);
        removed = learning_->Remove(LearningHistory::Kind::Conversion, segment.reading, surface) || removed;
    } else {
        removed = learning_->Remove(LearningHistory::Kind::Conversion, segment.reading, surface);
        removed = learning_->Remove(LearningHistory::Kind::Pair, segment.reading, surface, SegmentContext(focus_)) ||
                  removed;
    }
    if (!removed) {
        return false;
    }
    if (!predicting_) {
        ApplyLearning(focus_);
        const auto found = std::find(segment.candidates.begin(), segment.candidates.end(), surface);
        selected_[focus_] = found == segment.candidates.end()
                                ? 0
                                : static_cast<std::size_t>(found - segment.candidates.begin());
    }
    return true;
}

std::u16string InputSession::CommitConversion(SessionOutput& output)
{
    std::u16string text = ConvertedText();
    if (learning_ != nullptr && recording_) {
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            const ConvertedSegment& segment = segments_[i];
            const std::u16string& chosen = segment.candidates[selected_[i]];
            if (predicting_) {
                if (chosen != segment.reading &&
                    learning_->Record(LearningHistory::Kind::Prediction, segment.reading, chosen)) {
                    output.learning_changed = true;
                }
                continue;
            }
            // Learn a choice that differs from the dictionary's first candidate, and refresh one learned before.
            const bool differs = i < base_candidates_.size() && !base_candidates_[i].empty() &&
                                 base_candidates_[i].front() != chosen;
            const std::u16string context = SegmentContext(i);
            if (!differs && learning_->Conversions(segment.reading).empty() &&
                learning_->Pairs(context, segment.reading).empty()) {
                continue;
            }
            if (learning_->Record(LearningHistory::Kind::Conversion, segment.reading, chosen)) {
                output.learning_changed = true;
            }
            // D-04: the pair of words, so the same reading after the same word gets the same choice.
            if (!context.empty() &&
                learning_->Record(LearningHistory::Kind::Pair, segment.reading, chosen, context)) {
                output.learning_changed = true;
            }
        }
        // T-D04-2: segments split by hand (or taken from the history) are kept for the reading.
        if (!predicting_ && (segments_resized_ || segments_learned_)) {
            std::vector<std::u16string> readings;
            for (const ConvertedSegment& segment : segments_) {
                readings.push_back(segment.reading);
            }
            if (learning_->Record(LearningHistory::Kind::Segmentation, reading_,
                                  LearningHistory::JoinSegments(readings))) {
                output.learning_changed = true;
            }
        }
    }
    previous_surface_ = segments_.empty() ? std::u16string() : segments_.back().candidates[selected_.back()];
    // The next conversion continues from the last word, when it is the dictionary's best one (its id is known).
    context_right_id_.reset();
    if (!predicting_ && !segments_.empty() && base_candidates_.size() == segments_.size() &&
        !base_candidates_.back().empty() &&
        segments_.back().candidates[selected_.back()] == base_candidates_.back().front()) {
        context_right_id_ = segments_.back().right_id;
    }
    EndConversion();
    return text;
}

std::u16string InputSession::ConvertedText() const
{
    std::u16string text;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        text += segments_[i].candidates[selected_[i]];
    }
    return text;
}

void InputSession::EndConversion()
{
    converting_ = false;
    predicting_ = false;
    reading_.clear();
    segments_.clear();
    base_candidates_.clear();
    typo_surfaces_.clear();
    segments_resized_ = false;
    segments_learned_ = false;
    selected_.clear();
    focus_ = 0;
    candidate_list_visible_ = false;
}

const EmojiCatalog* InputSession::Catalog() const
{
    return emoji_provider_ ? emoji_provider_() : nullptr;
}

void InputSession::SetRecentEmoji(std::vector<std::u16string> recent)
{
    if (recent.size() > kMaxRecentEmoji) {
        recent.resize(kMaxRecentEmoji);
    }
    recent_emoji_ = std::move(recent);
    if (emoji_active_) {
        RefreshEmojiItems();
    }
}

bool InputSession::EmojiPaletteOffered() const
{
    return japanese_mode_ && !converting_ && !emoji_active_ && composer_.Text() == u"えもじ" && Catalog() != nullptr;
}

EmojiPaletteView InputSession::EmojiPalette() const
{
    EmojiPaletteView view;
    if (emoji_active_) {
        view.active = true;
        view.category = emoji_category_;
        view.query = emoji_search_.Text();
        view.items = emoji_items_;
        view.selected = emoji_items_.empty() ? kNoEmojiSelection : emoji_selected_;
        return view;
    }
    if (!EmojiPaletteOffered()) {
        return view;
    }
    view.category = recent_emoji_.empty() ? EmojiCategory::Smileys : EmojiCategory::Recent;
    if (view.category == EmojiCategory::Recent) {
        view.items = recent_emoji_;
    } else if (const EmojiCatalog* catalog = Catalog()) {
        for (std::size_t index : catalog->InCategory(view.category)) {
            view.items.push_back(catalog->at(index).text);
        }
    }
    view.selected = kNoEmojiSelection;
    return view;
}

void InputSession::OpenEmojiPalette()
{
    emoji_active_ = true;
    emoji_category_ = recent_emoji_.empty() ? EmojiCategory::Smileys : EmojiCategory::Recent;
    emoji_search_.Clear();
    predictions_.clear();
    RefreshEmojiItems();
}

void InputSession::CloseEmojiPalette()
{
    emoji_active_ = false;
    emoji_search_.Clear();
    emoji_items_.clear();
    emoji_selected_ = 0;
}

void InputSession::RefreshEmojiItems()
{
    emoji_items_.clear();
    emoji_selected_ = 0;
    const EmojiCatalog* catalog = Catalog();
    // Romaji still being typed ("in") is not part of the query yet.
    std::u16string query = emoji_search_.Text();
    while (!query.empty() && query.back() >= u'a' && query.back() <= u'z') {
        query.pop_back();
    }
    if (!emoji_search_.Empty()) {
        if (catalog != nullptr && !query.empty()) {
            for (std::size_t index : catalog->Search(query, kMaxEmojiSearchResults)) {
                emoji_items_.push_back(catalog->at(index).text);
            }
        }
        return;
    }
    if (emoji_category_ == EmojiCategory::Recent) {
        emoji_items_ = recent_emoji_;
    } else if (catalog != nullptr) {
        for (std::size_t index : catalog->InCategory(emoji_category_)) {
            emoji_items_.push_back(catalog->at(index).text);
        }
    }
}

SessionOutput InputSession::CommitEmoji(std::size_t index)
{
    SessionOutput output;
    if (index >= emoji_items_.size()) {
        return output;
    }
    std::u16string emoji = emoji_items_[index];
    output.commit = emoji;
    output.composition_changed = true;
    output.recent_emoji_changed = true;
    std::erase(recent_emoji_, emoji);
    recent_emoji_.insert(recent_emoji_.begin(), std::move(emoji));
    if (recent_emoji_.size() > kMaxRecentEmoji) {
        recent_emoji_.resize(kMaxRecentEmoji);
    }
    CloseEmojiPalette();
    composer_.Clear();
    typed_keys_.clear();
    typed_keys_valid_ = true;
    return output;
}

SessionOutput InputSession::PickEmoji(std::size_t index)
{
    if (EmojiPaletteOffered()) {
        OpenEmojiPalette();
    }
    if (!emoji_active_) {
        return {};
    }
    return CommitEmoji(index);
}

bool InputSession::SelectEmojiCategory(EmojiCategory category)
{
    if (EmojiPaletteOffered()) {
        OpenEmojiPalette();
    }
    if (!emoji_active_) {
        return false;
    }
    emoji_category_ = category;
    emoji_search_.Clear();
    RefreshEmojiItems();
    return true;
}

SessionOutput InputSession::HandleEmojiPalette(const KeyEvent& key)
{
    SessionOutput output;
    output.composition_changed = true;
    const std::size_t count = emoji_items_.size();
    const std::size_t last = count == 0 ? 0 : count - 1;
    constexpr std::size_t page = kEmojiColumns * kEmojiRows;
    switch (key.kind) {
    case KeyKind::Character:
        emoji_search_.InsertKey(key.character);
        RefreshEmojiItems();
        break;
    case KeyKind::Backspace:
        if (emoji_search_.Empty()) {
            CloseEmojiPalette();
        } else {
            emoji_search_.Backspace();
            RefreshEmojiItems();
        }
        break;
    case KeyKind::Escape:
        CloseEmojiPalette(); // back to えもじ
        break;
    case KeyKind::Enter:
        if (count == 0) {
            output.composition_changed = false;
        } else {
            output = CommitEmoji(emoji_selected_);
        }
        break;
    case KeyKind::Right:
    case KeyKind::Space:
        emoji_selected_ = std::min(emoji_selected_ + 1, last);
        break;
    case KeyKind::Left:
        emoji_selected_ = emoji_selected_ > 0 ? emoji_selected_ - 1 : 0;
        break;
    case KeyKind::Down:
        if (emoji_selected_ + kEmojiColumns <= last) {
            emoji_selected_ += kEmojiColumns;
        } else if (emoji_selected_ / kEmojiColumns < last / kEmojiColumns) {
            emoji_selected_ = last; // the last row is shorter
        }
        break;
    case KeyKind::Up:
        if (emoji_selected_ >= kEmojiColumns) {
            emoji_selected_ -= kEmojiColumns;
        }
        break;
    case KeyKind::PageDown:
        emoji_selected_ = std::min(emoji_selected_ + page, last);
        break;
    case KeyKind::PageUp:
        emoji_selected_ = emoji_selected_ >= page ? emoji_selected_ - page : 0;
        break;
    case KeyKind::Tab: {
        const std::size_t current = static_cast<std::size_t>(emoji_category_);
        const std::size_t next = key.shift ? (current + kEmojiCategoryCount - 1) % kEmojiCategoryCount
                                           : (current + 1) % kEmojiCategoryCount;
        emoji_category_ = static_cast<EmojiCategory>(next);
        emoji_search_.Clear();
        RefreshEmojiItems();
        break;
    }
    case KeyKind::Delete:
    case KeyKind::F6:
    case KeyKind::F7:
    case KeyKind::F8:
    case KeyKind::F9:
    case KeyKind::F10:
        output.composition_changed = false;
        break;
    }
    return output;
}

} // namespace astelio
